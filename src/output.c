#define G_LOG_DOMAIN "phoc-output"

#include "config.h"

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/drm.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "settings.h"
#include "layers.h"
#include "output.h"
#include "render.h"
#include "server.h"
#include "utils.h"


G_DEFINE_TYPE (PhocOutput, phoc_output, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_DESKTOP,
  PROP_WLR_OUTPUT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  OUTPUT_DESTROY,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

struct surface_iterator_data {
  roots_surface_iterator_func_t user_iterator;
  void                         *user_data;

  PhocOutput                   *output;
  double                        ox, oy;
  int                           width, height;
  float                         rotation, scale;
};

static bool
get_surface_box (struct surface_iterator_data *data,
                 struct wlr_surface *surface, int sx, int sy,
                 struct wlr_box *surface_box)
{
  PhocOutput *self = data->output;

  if (!wlr_surface_has_buffer (surface)) {
    return false;
  }

  int sw = surface->current.width;
  int sh = surface->current.height;

  double _sx = sx + surface->sx;
  double _sy = sy + surface->sy;

  phoc_utils_rotate_child_position (&_sx, &_sy, sw, sh, data->width,
                                    data->height, data->rotation);

  struct wlr_box box = {
    .x = data->ox + _sx,
    .y = data->oy + _sy,
    .width = sw,
    .height = sh,
  };

  if (surface_box != NULL) {
    *surface_box = box;
  }

  struct wlr_box rotated_box;

  wlr_box_rotated_bounds (&rotated_box, &box, data->rotation);

  struct wlr_box output_box = {0};

  wlr_output_effective_resolution (self->wlr_output,
                                   &output_box.width, &output_box.height);
  phoc_output_scale_box (self, &output_box, 1 / data->scale);

  struct wlr_box intersection;

  return wlr_box_intersection (&intersection, &output_box, &rotated_box);
}


static void
phoc_output_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  PhocOutput *self = PHOC_OUTPUT (object);

  switch (property_id) {
  case PROP_DESKTOP:
    self->desktop = g_value_get_pointer (value);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DESKTOP]);
    break;
  case PROP_WLR_OUTPUT:
    self->wlr_output = g_value_get_pointer (value);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WLR_OUTPUT]);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_output_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  PhocOutput *self = PHOC_OUTPUT (object);

  switch (property_id) {
  case PROP_DESKTOP:
    g_value_set_pointer (value, self->desktop);
    break;
  case PROP_WLR_OUTPUT:
    g_value_set_pointer (value, self->wlr_output);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_output_init (PhocOutput *self)
{
}

PhocOutput *
phoc_output_new (PhocDesktop *desktop, struct wlr_output *wlr_output)
{
  return g_object_new (PHOC_TYPE_OUTPUT,
                       "desktop", desktop,
                       "wlr-output", wlr_output,
                       NULL);
}

static void
update_output_manager_config (PhocDesktop *desktop)
{
  struct wlr_output_configuration_v1 *config =
    wlr_output_configuration_v1_create ();

  PhocOutput *output;

  wl_list_for_each (output, &desktop->outputs, link) {
    struct wlr_output_configuration_head_v1 *config_head =
      wlr_output_configuration_head_v1_create (config, output->wlr_output);
    struct wlr_box *output_box = wlr_output_layout_get_box (
      output->desktop->layout, output->wlr_output);
    if (output_box) {
      config_head->state.x = output_box->x;
      config_head->state.y = output_box->y;
    }
  }

  wlr_output_manager_v1_set_configuration (desktop->output_manager_v1, config);
}

static void
phoc_output_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, output_destroy);

  update_output_manager_config (self->desktop);

  g_signal_emit (self, signals[OUTPUT_DESTROY], 0);
}

static void
phoc_output_handle_enable (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, enable);

  update_output_manager_config (self->desktop);
}

static void
phoc_output_damage_handle_frame (struct wl_listener *listener,
                                 void               *data)
{
  PhocOutput *self = wl_container_of (listener, self, damage_frame);

  output_render (self);
}

static void
phoc_output_damage_handle_destroy (struct wl_listener *listener,
                                   void               *data)
{
  PhocOutput *self = wl_container_of (listener, self, damage_destroy);

  g_signal_emit (self, signals[OUTPUT_DESTROY], 0);
}

static void
phoc_output_handle_mode (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, mode);

  arrange_layers (self);
  update_output_manager_config (self->desktop);
}

static void
phoc_output_handle_transform (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, transform);

  arrange_layers (self);
}

static void
phoc_output_set_mode (struct wlr_output *output, struct roots_output_config *oc)
{
  int mhz = (int)(oc->mode.refresh_rate * 1000);

  if (wl_list_empty (&output->modes)) {
    // Output has no mode, try setting a custom one
    wlr_output_set_custom_mode (output, oc->mode.width,
                                oc->mode.height, mhz);
    return;
  }

  struct wlr_output_mode *mode, *best = NULL;

  wl_list_for_each (mode, &output->modes, link) {
    if (mode->width == oc->mode.width && mode->height == oc->mode.height) {
      if (mode->refresh == mhz) {
        best = mode;
        break;
      }
      best = mode;
    }
  }
  if (!best) {
    wlr_log (WLR_ERROR, "Configured mode for %s not available",
             output->name);
  } else {
    wlr_log (WLR_DEBUG, "Assigning configured mode to %s",
             output->name);
    wlr_output_set_mode (output, best);
  }
}

static void
phoc_output_constructed (GObject *object)
{
  PhocOutput *self = PHOC_OUTPUT (object);
  PhocServer *server = phoc_server_get_default ();
  PhocInput *input = server->input;

  g_debug ("Initializing roots output");
  assert (server->desktop);

  struct roots_config *config = self->desktop->config;

  wlr_log (WLR_DEBUG, "Output '%s' added", self->wlr_output->name);
  wlr_log (WLR_DEBUG, "'%s %s %s' %" PRId32 "mm x %" PRId32 "mm",
           self->wlr_output->make, self->wlr_output->model,
           self->wlr_output->serial, self->wlr_output->phys_width,
           self->wlr_output->phys_height);

  clock_gettime (CLOCK_MONOTONIC, &self->last_frame);
  self->wlr_output->data = self;
  wl_list_insert (&self->desktop->outputs, &self->link);

  self->damage = wlr_output_damage_create (self->wlr_output);

  self->debug_touch_points = NULL;

  self->output_destroy.notify = phoc_output_handle_destroy;
  wl_signal_add (&self->wlr_output->events.destroy, &self->output_destroy);
  self->enable.notify = phoc_output_handle_enable;
  wl_signal_add (&self->wlr_output->events.enable, &self->enable);
  self->mode.notify = phoc_output_handle_mode;
  wl_signal_add (&self->wlr_output->events.mode, &self->mode);
  self->transform.notify = phoc_output_handle_transform;
  wl_signal_add (&self->wlr_output->events.transform, &self->transform);

  self->damage_frame.notify = phoc_output_damage_handle_frame;
  wl_signal_add (&self->damage->events.frame, &self->damage_frame);
  self->damage_destroy.notify = phoc_output_damage_handle_destroy;
  wl_signal_add (&self->damage->events.destroy, &self->damage_destroy);

  size_t len = sizeof(self->layers) / sizeof(self->layers[0]);

  for (size_t i = 0; i < len; ++i) {
    wl_list_init (&self->layers[i]);
  }

  struct roots_output_config *output_config =
    roots_config_get_output (config, self->wlr_output);

  struct wlr_output_mode *preferred_mode =
    wlr_output_preferred_mode (self->wlr_output);

  if (output_config) {
    if (output_config->enable) {
      if (wlr_output_is_drm (self->wlr_output)) {
        struct roots_output_mode_config *mode_config;
        wl_list_for_each (mode_config, &output_config->modes, link) {
          wlr_drm_connector_add_mode (self->wlr_output, &mode_config->info);
        }
      } else if (!wl_list_empty (&output_config->modes)) {
        wlr_log (WLR_ERROR, "Can only add modes for DRM backend");
      }

      if (output_config->mode.width) {
        phoc_output_set_mode (self->wlr_output, output_config);
      } else if (preferred_mode != NULL) {
        wlr_output_set_mode (self->wlr_output, preferred_mode);
      }

      wlr_output_set_scale (self->wlr_output, output_config->scale);
      wlr_output_set_transform (self->wlr_output, output_config->transform);
      wlr_output_layout_add (self->desktop->layout, self->wlr_output,
                             output_config->x, output_config->y);
    } else {
      wlr_output_enable (self->wlr_output, false);
    }
  } else {
    if (preferred_mode != NULL) {
      wlr_output_set_mode (self->wlr_output, preferred_mode);
    }
    wlr_output_enable (self->wlr_output, true);
    wlr_output_layout_add_auto (self->desktop->layout, self->wlr_output);
  }
  wlr_output_commit (self->wlr_output);

  struct roots_seat *seat;

  wl_list_for_each (seat, &input->seats, link) {
    roots_seat_configure_cursor (seat);
    roots_seat_configure_xcursor (seat);
  }

  arrange_layers (self);
  phoc_output_damage_whole (self);

  update_output_manager_config (self->desktop);

  G_OBJECT_CLASS (phoc_output_parent_class)->constructed (object);

}

static void
phoc_output_finalize (GObject *object)
{
  PhocOutput *self = PHOC_OUTPUT (object);

  wl_list_remove (&self->link);
  wl_list_remove (&self->output_destroy.link);
  wl_list_remove (&self->enable.link);
  wl_list_remove (&self->mode.link);
  wl_list_remove (&self->transform.link);
  wl_list_remove (&self->damage_frame.link);
  wl_list_remove (&self->damage_destroy.link);
  g_list_free_full (self->debug_touch_points, g_free);

  size_t len = sizeof (self->layers) / sizeof (self->layers[0]);
  for (size_t i = 0; i < len; ++i) {
    wl_list_remove (&self->layers[i]);
  }

  G_OBJECT_CLASS (phoc_output_parent_class)->finalize (object);
}

static void
phoc_output_dispose (GObject *object)
{
  G_OBJECT_CLASS (phoc_output_parent_class)->dispose (object);
}

static void
phoc_output_class_init (PhocOutputClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->set_property = phoc_output_set_property;
  object_class->get_property = phoc_output_get_property;

  object_class->constructed = phoc_output_constructed;
  object_class->dispose = phoc_output_dispose;
  object_class->finalize = phoc_output_finalize;

  props[PROP_DESKTOP] =
    g_param_spec_pointer (
      "desktop",
      "Desktop",
      "The desktop object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_WLR_OUTPUT] =
    g_param_spec_pointer (
      "wlr-output",
      "wlr-output",
      "The wlroots output object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[OUTPUT_DESTROY] = g_signal_new ("output-destroyed",
                                          G_TYPE_OBJECT,
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL, NULL, G_TYPE_NONE, 0);

}

static void
phoc_output_for_each_surface_iterator (struct wlr_surface *surface,
                                       int sx, int sy, void *_data)
{
  struct surface_iterator_data *data = _data;

  struct wlr_box box;
  bool intersects = get_surface_box (data, surface, sx, sy, &box);

  if (!intersects) {
    return;
  }

  data->user_iterator (data->output, surface, &box, data->rotation,
                       data->scale, data->user_data);
}

void
phoc_output_surface_for_each_surface (PhocOutput *self, struct wlr_surface
                                      *surface, double ox, double oy,
                                      roots_surface_iterator_func_t iterator,
                                      void *user_data)
{
  struct surface_iterator_data data = {
    .user_iterator = iterator,
    .user_data = user_data,
    .output = self,
    .ox = ox,
    .oy = oy,
    .width = surface->current.width,
    .height = surface->current.height,
    .rotation = 0,
    .scale = 1.0
  };

  wlr_surface_for_each_surface (surface,
                                phoc_output_for_each_surface_iterator, &data);
}

void
phoc_output_xdg_surface_for_each_surface (PhocOutput *self, struct
                                          wlr_xdg_surface *xdg_surface, double
                                          ox, double oy,
                                          roots_surface_iterator_func_t
                                          iterator, void *user_data)
{
  struct surface_iterator_data data = {
    .user_iterator = iterator,
    .user_data = user_data,
    .output = self,
    .ox = ox,
    .oy = oy,
    .width = xdg_surface->surface->current.width,
    .height = xdg_surface->surface->current.height,
    .rotation = 0,
    .scale = 1.0
  };

  wlr_xdg_surface_for_each_surface (xdg_surface,
                                    phoc_output_for_each_surface_iterator, &data);
}

void
phoc_output_view_for_each_surface (PhocOutput *self, struct roots_view *view,
                                   roots_surface_iterator_func_t iterator, void
                                   *user_data)
{
  struct wlr_box *output_box =
    wlr_output_layout_get_box (self->desktop->layout, self->wlr_output);

  if (!output_box) {
    return;
  }

  struct surface_iterator_data data = {
    .user_iterator = iterator,
    .user_data = user_data,
    .output = self,
    .ox = view->box.x - output_box->x,
    .oy = view->box.y - output_box->y,
    .width = view->box.width,
    .height = view->box.height,
    .rotation = 0,
    .scale = view->scale
  };

  view_for_each_surface (view, phoc_output_for_each_surface_iterator, &data);
}

#ifdef PHOC_XWAYLAND
void
phoc_output_xwayland_children_for_each_surface (PhocOutput *self, struct
                                                wlr_xwayland_surface *surface,
                                                roots_surface_iterator_func_t
                                                iterator, void *user_data)
{
  struct wlr_box *output_box =
    wlr_output_layout_get_box (self->desktop->layout, self->wlr_output);

  if (!output_box) {
    return;
  }

  struct wlr_xwayland_surface *child;

  wl_list_for_each (child, &surface->children, parent_link) {
    if (child->mapped) {
      double ox = child->x - output_box->x;
      double oy = child->y - output_box->y;
      phoc_output_surface_for_each_surface (self, child->surface, ox, oy, iterator,
                                            user_data);
    }
    phoc_output_xwayland_children_for_each_surface (self, child,
                                                    iterator, user_data);
  }
}
#endif

static void
phoc_output_layer_handle_surface (PhocOutput *self, struct roots_layer_surface *layer_surface,
                                  roots_surface_iterator_func_t iterator, void
                                  *user_data)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
    layer_surface->layer_surface;

  phoc_output_surface_for_each_surface (self, wlr_layer_surface_v1->surface,
                                        layer_surface->geo.x,
                                        layer_surface->geo.y, iterator,
                                        user_data);

  struct wlr_xdg_popup *state;

  wl_list_for_each (state, &wlr_layer_surface_v1->popups, link) {
    struct wlr_xdg_surface *popup = state->base;
    if (!popup->configured) {
      continue;
    }

    double popup_sx, popup_sy;
    popup_sx = layer_surface->geo.x;
    popup_sx += popup->popup->geometry.x - popup->geometry.x;
    popup_sy = layer_surface->geo.y;
    popup_sy += popup->popup->geometry.y - popup->geometry.y;

    phoc_output_xdg_surface_for_each_surface (self, popup,
                                              popup_sx, popup_sy, iterator, user_data);
  }
}

void
phoc_output_layer_for_each_surface (PhocOutput                   *self,
                                    struct wl_list               *layer_surfaces,
                                    roots_surface_iterator_func_t iterator,
                                    void                         *user_data)
{
  struct roots_layer_surface *layer_surface;

  wl_list_for_each_reverse (layer_surface, layer_surfaces, link)
  {
    if (layer_surface->layer_surface->current.exclusive_zone <= 0) {
      phoc_output_layer_handle_surface (self, layer_surface, iterator, user_data);
    }
  }
  wl_list_for_each (layer_surface, layer_surfaces, link) {
    if (layer_surface->layer_surface->current.exclusive_zone > 0) {
      phoc_output_layer_handle_surface (self, layer_surface, iterator, user_data);
    }
  }
}

void
phoc_output_drag_icons_for_each_surface (PhocOutput *self, PhocInput *input,
                                         roots_surface_iterator_func_t
                                         iterator, void *user_data)
{
  struct wlr_box *output_box =
    wlr_output_layout_get_box (self->desktop->layout, self->wlr_output);

  if (!output_box) {
    return;
  }

  struct roots_seat *seat;

  wl_list_for_each (seat, &input->seats, link) {
    struct roots_drag_icon *drag_icon = seat->drag_icon;
    if (!drag_icon || !drag_icon->wlr_drag_icon->mapped) {
      continue;
    }

    double ox = drag_icon->x - output_box->x;
    double oy = drag_icon->y - output_box->y;
    phoc_output_surface_for_each_surface (self, drag_icon->wlr_drag_icon->surface,
                                          ox, oy, iterator, user_data);
  }
}

void
phoc_output_for_each_surface (PhocOutput *self, roots_surface_iterator_func_t iterator, void
                              *user_data, gboolean visible_only)
{
  PhocDesktop *desktop = self->desktop;
  PhocServer *server = phoc_server_get_default ();

  if (self->fullscreen_view != NULL) {
    struct roots_view *view = self->fullscreen_view;

    phoc_output_view_for_each_surface (self, view, iterator, user_data);

#ifdef PHOC_XWAYLAND
    if (view->type == ROOTS_XWAYLAND_VIEW) {
      struct roots_xwayland_surface *xwayland_surface =
        roots_xwayland_surface_from_view (view);
      phoc_output_xwayland_children_for_each_surface (self,
                                                      xwayland_surface->xwayland_surface,
                                                      iterator, user_data);
    }
#endif
  } else {
    struct roots_view *view;
    wl_list_for_each_reverse (view, &desktop->views, link)
    {
      if (!visible_only || phoc_desktop_view_is_visible (desktop, view)) {
        phoc_output_view_for_each_surface (self, view, iterator, user_data);
      }
    }
  }

  phoc_output_drag_icons_for_each_surface (self, server->input,
                                           iterator, user_data);

  size_t len = sizeof(self->layers) / sizeof(self->layers[0]);
  for (size_t i = 0; i < len; ++i) {
    phoc_output_layer_for_each_surface (self, &self->layers[i],
                                        iterator, user_data);
  }
}

static int
scale_length (int length, int offset, float scale)
{
  return round ((offset + length) * scale) - round (offset * scale);
}

void
phoc_output_scale_box (PhocOutput *self, struct wlr_box *box, float scale)
{
  box->width = scale_length (box->width, box->x, scale);
  box->height = scale_length (box->height, box->y, scale);
  box->x = round (box->x * scale);
  box->y = round (box->y * scale);
}

void
phoc_output_get_decoration_box (PhocOutput *self, struct roots_view *view,
                                struct wlr_box *box)
{
  struct wlr_box deco_box;

  view_get_deco_box (view, &deco_box);

  double x = deco_box.x;
  double y = deco_box.y;

  wlr_output_layout_output_coords (self->desktop->layout,
                                   self->wlr_output, &x, &y);

  box->x = x * self->wlr_output->scale;
  box->y = y * self->wlr_output->scale;
  box->width = deco_box.width * self->wlr_output->scale;
  box->height = deco_box.height * self->wlr_output->scale;
}

void
phoc_output_damage_whole (PhocOutput *self)
{
  wlr_output_damage_add_whole (self->damage);
}

static bool
phoc_view_accept_damage (PhocOutput *self, struct roots_view   *view)
{
  PhocServer *server = phoc_server_get_default ();
  if (view->wlr_surface == NULL) {
    return false;
  }
  if (!phoc_desktop_view_is_visible (server->desktop, view)) {
    return false;
  }
  if (self->fullscreen_view == NULL) {
    return true;
  }
  if (self->fullscreen_view == view) {
    return true;
  }
#ifdef PHOC_XWAYLAND
  if (self->fullscreen_view->type == ROOTS_XWAYLAND_VIEW &&
      view->type == ROOTS_XWAYLAND_VIEW) {
    // Special case: accept damage from children
    struct wlr_xwayland_surface *xsurface =
      roots_xwayland_surface_from_view (view)->xwayland_surface;
    struct wlr_xwayland_surface *fullscreen_xsurface =
      roots_xwayland_surface_from_view (self->fullscreen_view)->xwayland_surface;
    while (xsurface != NULL) {
      if (fullscreen_xsurface == xsurface) {
        return true;
      }
      xsurface = xsurface->parent;
    }
  }
#endif
  return false;
}

static void
damage_surface_iterator (PhocOutput *self, struct wlr_surface *surface, struct
                         wlr_box *_box, float rotation, float scale, void *data)
{
  bool *whole = data;

  struct wlr_box box = *_box;

  phoc_output_scale_box (self, &box, scale);
  phoc_output_scale_box (self, &box, self->wlr_output->scale);

  int center_x = box.x + box.width/2;
  int center_y = box.y + box.height/2;

  if (pixman_region32_not_empty (&surface->buffer_damage)) {
    pixman_region32_t damage;
    pixman_region32_init (&damage);
    wlr_surface_get_effective_damage (surface, &damage);
    wlr_region_scale (&damage, &damage, scale);
    wlr_region_scale (&damage, &damage, self->wlr_output->scale);
    if (ceil (self->wlr_output->scale) > surface->current.scale) {
      // When scaling up a surface, it'll become blurry so we need to
      // expand the damage region
      wlr_region_expand (&damage, &damage,
                         ceil (self->wlr_output->scale) - surface->current.scale);
    }
    pixman_region32_translate (&damage, box.x, box.y);
    wlr_region_rotated_bounds (&damage, &damage, rotation,
                               center_x, center_y);
    wlr_output_damage_add (self->damage, &damage);
    pixman_region32_fini (&damage);
  }

  if (*whole) {
    wlr_box_rotated_bounds (&box, &box, rotation);
    wlr_output_damage_add_box (self->damage, &box);
  }

  wlr_output_schedule_frame (self->wlr_output);
}

void
phoc_output_damage_whole_local_surface (PhocOutput *self, struct wlr_surface *surface, double ox,
                                        double oy)
{
  bool whole = true;

  phoc_output_surface_for_each_surface (self, surface, ox, oy,
                                        damage_surface_iterator, &whole);
}

static void
damage_whole_decoration (PhocOutput *self, struct roots_view   *view)
{
  if (!view->decorated || view->wlr_surface == NULL) {
    return;
  }

  struct wlr_box box;

  phoc_output_get_decoration_box (self, view, &box);

  wlr_output_damage_add_box (self->damage, &box);
}

void
phoc_output_damage_whole_view (PhocOutput *self, struct roots_view   *view)
{
  if (!phoc_view_accept_damage (self, view)) {
    return;
  }

  damage_whole_decoration (self, view);

  bool whole = true;

  phoc_output_view_for_each_surface (self, view, damage_surface_iterator, &whole);
}


void
phoc_output_damage_from_view (PhocOutput *self, struct roots_view *view)
{
  if (!phoc_view_accept_damage (self, view)) {
    return;
  }

  bool whole = false;

  phoc_output_view_for_each_surface (self, view, damage_surface_iterator, &whole);

}

void
phoc_output_damage_whole_drag_icon (PhocOutput *self, struct roots_drag_icon
                                    *icon)
{
  bool whole = true;

  phoc_output_surface_for_each_surface (self, icon->wlr_drag_icon->surface,
                                        icon->x, icon->y,
                                        damage_surface_iterator, &whole);
}

void
phoc_output_damage_from_local_surface (PhocOutput *self, struct wlr_surface
                                       *surface, double ox, double oy)
{
  bool whole = false;

  phoc_output_surface_for_each_surface (self, surface, ox, oy,
                                        damage_surface_iterator, &whole);
}

void
handle_output_manager_apply (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop =
    wl_container_of (listener, desktop, output_manager_apply);
  struct wlr_output_configuration_v1 *config = data;

  bool ok = true;
  struct wlr_output_configuration_head_v1 *config_head;

  // First disable outputs we need to disable
  wl_list_for_each (config_head, &config->heads, link) {
    struct wlr_output *wlr_output = config_head->state.output;

    if (config_head->state.enabled)
      continue;

    if (!wlr_output->enabled)
      continue;

    wlr_output_enable (wlr_output, false);
    wlr_output_layout_remove (desktop->layout, wlr_output);
    ok &= wlr_output_commit (wlr_output);
  }

  // Then enable outputs that need to
  wl_list_for_each (config_head, &config->heads, link) {
    struct wlr_output *wlr_output = config_head->state.output;
    PhocOutput *output = wlr_output->data;

    if (!config_head->state.enabled)
      continue;

    wlr_output_enable (wlr_output, true);
    if (config_head->state.mode != NULL) {
      wlr_output_set_mode (wlr_output, config_head->state.mode);
    } else {
      wlr_output_set_custom_mode (wlr_output,
                                  config_head->state.custom_mode.width,
                                  config_head->state.custom_mode.height,
                                  config_head->state.custom_mode.refresh);
    }
    wlr_output_layout_add (desktop->layout, wlr_output,
                           config_head->state.x, config_head->state.y);
    wlr_output_set_transform (wlr_output, config_head->state.transform);
    wlr_output_set_scale (wlr_output, config_head->state.scale);
    ok &= wlr_output_commit (wlr_output);
    if (output->fullscreen_view) {
      view_set_fullscreen (output->fullscreen_view, true, wlr_output);
    }
  }

  if (ok) {
    wlr_output_configuration_v1_send_succeeded (config);
  } else {
    wlr_output_configuration_v1_send_failed (config);
  }
  wlr_output_configuration_v1_destroy (config);

  update_output_manager_config (desktop);
}

void
handle_output_manager_test (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop =
    wl_container_of (listener, desktop, output_manager_test);
  struct wlr_output_configuration_v1 *config = data;

  // TODO: implement test-only mode
  wlr_output_configuration_v1_send_succeeded (config);
  wlr_output_configuration_v1_destroy (config);
}

void
phoc_output_handle_output_power_manager_set_mode (struct wl_listener *listener, void *data)
{
  struct wlr_output_power_v1_set_mode_event *event = data;
  PhocOutput *self;
  bool enable = true;

  g_return_if_fail (event && event->output && event->output->data);
  self = event->output->data;
  g_debug ("Request to set output power mode of %p to %d",
           self, event->mode);
  switch (event->mode) {
  case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
    enable = false;
    break;
  case ZWLR_OUTPUT_POWER_V1_MODE_ON:
    enable = true;
    break;
  default:
    g_warning ("Unhandled power state %d for %p", event->mode, self);
    return;
  }

  if (enable == self->wlr_output->enabled)
    return;

  wlr_output_enable (self->wlr_output, enable);
  if (!wlr_output_commit (self->wlr_output)) {
    g_warning ("Failed to commit power mode change to %d for %p", enable, self);
    return;
  }

  if (enable)
    phoc_output_damage_whole (self);
}

/**
 * phoc_output_is_builtin:
 *
 * Returns: %TRUE if the output a built in panel (e.g. laptop panel or
 * phone LCD), %FALSE otherwise.
 */
gboolean
phoc_output_is_builtin (PhocOutput *output)
{
  g_return_val_if_fail (output, FALSE);
  g_return_val_if_fail (output->wlr_output, FALSE);
  g_return_val_if_fail (output->wlr_output->name, FALSE);

  if (g_str_has_prefix (output->wlr_output->name, "LVDS-"))
    return TRUE;
  else if (g_str_has_prefix (output->wlr_output->name, "eDP-"))
    return TRUE;
  else if (g_str_has_prefix (output->wlr_output->name, "DSI-"))
    return TRUE;
  /* HWCOMPOSER-1 is guaranteed to be the primary display */
  else if (g_strcmp0 (output->wlr_output->name, "HWCOMPOSER-1") == 0)
    return TRUE;

  return FALSE;
}
