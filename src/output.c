#define G_LOG_DOMAIN "phoc-output"

#include "phoc-config.h"

#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/drm.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/region.h>

#include "anim/animatable.h"
#include "cutouts-overlay.h"
#include "settings.h"
#include "layers.h"
#include "layer-shell-effects.h"
#include "output.h"
#include "output-shield.h"
#include "render.h"
#include "render-private.h"
#include "seat.h"
#include "server.h"
#include "utils.h"
#include "xwayland-surface.h"

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

typedef struct _PhocOutputPrivate {
  PhocOutputShield *shield;

  GSList *frame_callbacks;
  gint    frame_callback_next_id;
  gint64  last_frame_us;

  PhocCutoutsOverlay *cutouts;
  gulong              render_cutouts_id;
  struct wlr_texture *cutouts_texture;

  gboolean shell_revealed;
  gboolean force_shell_reveal;
} PhocOutputPrivate;

static void phoc_output_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocOutput, phoc_output, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (PhocOutput)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, phoc_output_initable_iface_init));

typedef struct {
  PhocAnimatable    *animatable;
  PhocFrameCallback  callback;
  gpointer           user_data;
  GDestroyNotify     notify;
  guint              id;
} PhocOutputFrameCallbackInfo;


typedef struct {
  PhocSurfaceIterator  user_iterator;
  void                *user_data;

  PhocOutput          *output;
  double               ox, oy;
  int                  width, height;
  float                rotation, scale;
} PhocOutputSurfaceIteratorData;


static void
phoc_output_frame_callback_info_free (PhocOutputFrameCallbackInfo *cb_info)
{
  if (cb_info->notify && cb_info->user_data)
    cb_info->notify (cb_info->user_data);
  g_free (cb_info);
}

static bool
get_surface_box (PhocOutputSurfaceIteratorData *data,
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

  phoc_utils_rotated_bounds (&rotated_box, &box, data->rotation);

  struct wlr_box output_box = {0};

  wlr_output_effective_resolution (self->wlr_output,
                                   &output_box.width, &output_box.height);
  phoc_output_scale_box (self, &output_box, 1 / data->scale);

  struct wlr_box intersection;

  return wlr_box_intersection (&intersection, &output_box, &box);
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
    self->desktop = g_value_dup_object (value);
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
    g_value_set_object (value, self->desktop);
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
  PhocOutputPrivate *priv = phoc_output_get_instance_private(self);

  priv->frame_callback_next_id = 1;
  priv->last_frame_us = g_get_monotonic_time ();
  priv->shield = phoc_output_shield_new (self);

  self->debug_touch_points = NULL;
  wl_list_init (&self->layer_surfaces);
}

PhocOutput *
phoc_output_new (PhocDesktop *desktop, struct wlr_output *wlr_output, GError **error)
{
  return g_initable_new (PHOC_TYPE_OUTPUT, NULL, error,
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
render_cutouts (PhocRenderer *renderer, PhocOutput *self)
{
  struct wlr_output *wlr_output = self->wlr_output;
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);

  g_assert (PHOC_IS_OUTPUT (self));
  g_assert (PHOC_IS_RENDERER (renderer));

  if (priv->cutouts_texture) {
    float matrix[9];
    struct wlr_box box;
    struct wlr_texture *texture = priv->cutouts_texture;

    box = (struct wlr_box){ 0, 0, texture->width, texture->height };
    wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL,
                           0, wlr_output->transform_matrix);
    wlr_render_texture_with_matrix (phoc_renderer_get_wlr_renderer (renderer),  texture, matrix, 1.0);
  }
}


static void
phoc_output_damage_handle_frame (struct wl_listener *listener,
                                 void               *data)
{
  PhocOutput *self = wl_container_of (listener, self, damage_frame);
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);
  PhocServer *server = phoc_server_get_default ();
  PhocRenderer *renderer = phoc_server_get_renderer (server);

  GSList *l = priv->frame_callbacks;
  while (l != NULL) {
    GSList *next = l->next;
    PhocOutputFrameCallbackInfo *cb_info = l->data;
    gboolean ret;

    ret = cb_info->callback(cb_info->animatable, priv->last_frame_us, cb_info->user_data);
    if (ret == G_SOURCE_REMOVE) {
      phoc_output_frame_callback_info_free (cb_info);
      priv->frame_callbacks = g_slist_delete_link (priv->frame_callbacks, l);
    }
    l = next;
  }
  priv->last_frame_us = g_get_monotonic_time ();

  if (priv->cutouts_texture) {
    struct wlr_box box = { 0, 0, priv->cutouts_texture->width, priv->cutouts_texture->height };
    wlr_output_damage_add_box (self->damage, &box);
  }

  phoc_renderer_render_output (renderer, self);

  /* Want frame clock ticking as long as we have frame callbacks */
  if (priv->frame_callbacks)
    wlr_output_schedule_frame(self->wlr_output);
}


static void
phoc_output_damage_handle_destroy (struct wl_listener *listener,
                                   void               *data)
{
  PhocOutput *self = wl_container_of (listener, self, damage_destroy);

  g_assert (PHOC_IS_OUTPUT (self));

  if (self->fullscreen_view)
    phoc_view_set_fullscreen (self->fullscreen_view, false, NULL);

  wl_list_remove (&self->damage_frame.link);
  wl_list_remove (&self->damage_destroy.link);
}

static void
phoc_output_handle_mode (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, mode);

  phoc_layer_shell_arrange (self);
  update_output_manager_config (self->desktop);
}

static void
phoc_output_handle_commit (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, commit);

  phoc_layer_shell_arrange (self);
}

static float
phoc_output_compute_scale (struct wlr_output *output)
{
  int32_t width = 0, height = 0;

  if (!output->phys_width || !output->phys_height) {
    g_message ("Output '%s' has invalid physical size, "
               "using default scale", output->name);
    return 1;
  }

  // Use the pending mode if any
  if (output->pending.committed & WLR_OUTPUT_STATE_MODE) {
    switch (output->pending.mode_type) {
    case WLR_OUTPUT_STATE_MODE_FIXED:
      width = output->pending.mode->width;
      height = output->pending.mode->height;
      break;
    case WLR_OUTPUT_STATE_MODE_CUSTOM:
      width = output->pending.custom_mode.width;
      height = output->pending.custom_mode.height;
      break;
    default:
      break;
    }
  // Fall back to current mode
  } else if (output->current_mode) {
    width = output->current_mode->width;
    height = output->current_mode->height;
  }

  if (!width || !height) {
    g_message ("No valid mode set for output '%s', "
               "using default scale", output->name);
    return 1;
  }

  return phoc_utils_compute_scale (output->phys_width, output->phys_height,
                                   width, height);
}

static void
phoc_output_set_mode (struct wlr_output *output, PhocOutputConfig *oc)
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
    g_warning ("Configured mode for %s not available", output->name);
  } else {
    g_debug ("Assigning configured mode to %s",
             output->name);
    wlr_output_set_mode (output, best);
  }
}


static gboolean
phoc_output_initable_init (GInitable    *initable,
                           GCancellable *cancellable,
                           GError      **error)
{
  PhocOutput *self = PHOC_OUTPUT (initable);
  PhocServer *server = phoc_server_get_default ();
  PhocRenderer *renderer = phoc_server_get_renderer (server);
  PhocInput *input = server->input;

  g_assert (PHOC_IS_DESKTOP (server->desktop));

  PhocConfig *config = self->desktop->config;

  g_message ("Output '%s' added ('%s'/'%s'/'%s'), "
             "%" PRId32 "mm x %" PRId32 "mm",
             self->wlr_output->name,
             self->wlr_output->make,
             self->wlr_output->model,
             self->wlr_output->serial,
             self->wlr_output->phys_width,
             self->wlr_output->phys_height);

  self->wlr_output->data = self;
  wl_list_insert (&self->desktop->outputs, &self->link);

  if (!wlr_output_init_render (self->wlr_output,
                               phoc_renderer_get_wlr_allocator (renderer),
                               phoc_renderer_get_wlr_renderer (renderer))) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "Could not create renderer");
    return FALSE;
  }

  self->damage = wlr_output_damage_create (self->wlr_output);

  self->output_destroy.notify = phoc_output_handle_destroy;
  wl_signal_add (&self->wlr_output->events.destroy, &self->output_destroy);
  self->enable.notify = phoc_output_handle_enable;
  wl_signal_add (&self->wlr_output->events.enable, &self->enable);
  self->mode.notify = phoc_output_handle_mode;
  wl_signal_add (&self->wlr_output->events.mode, &self->mode);
  self->commit.notify = phoc_output_handle_commit;
  wl_signal_add (&self->wlr_output->events.commit, &self->commit);

  self->damage_frame.notify = phoc_output_damage_handle_frame;
  wl_signal_add (&self->damage->events.frame, &self->damage_frame);
  self->damage_destroy.notify = phoc_output_damage_handle_destroy;
  wl_signal_add (&self->damage->events.destroy, &self->damage_destroy);

  PhocOutputConfig *output_config = phoc_config_get_output (config, self->wlr_output);

  struct wlr_output_mode *preferred_mode =
    wlr_output_preferred_mode (self->wlr_output);

  if (output_config) {
    if (output_config->enable) {
      if (wlr_output_is_drm (self->wlr_output)) {
        PhocOutputModeConfig *mode_config;
        wl_list_for_each (mode_config, &output_config->modes, link) {
          wlr_drm_connector_add_mode (self->wlr_output, &mode_config->info);
        }
      } else if (!wl_list_empty (&output_config->modes)) {
        g_warning ("Can only add modes for DRM backend");
      }

      if (output_config->mode.width) {
        phoc_output_set_mode (self->wlr_output, output_config);
      } else if (preferred_mode != NULL) {
        wlr_output_set_mode (self->wlr_output, preferred_mode);
      }

      if (!output_config->scale) {
        wlr_output_set_scale (self->wlr_output,
                              phoc_output_compute_scale (self->wlr_output));
      } else {
        wlr_output_set_scale (self->wlr_output, output_config->scale);
      }
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
    wlr_output_set_scale (self->wlr_output,
                          phoc_output_compute_scale (self->wlr_output));
    wlr_output_enable (self->wlr_output, true);
    wlr_output_layout_add_auto (self->desktop->layout, self->wlr_output);
  }
  wlr_output_commit (self->wlr_output);

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    phoc_seat_configure_cursor (seat);
    phoc_seat_configure_xcursor (seat);
  }

  phoc_layer_shell_arrange (self);
  phoc_layer_shell_update_focus ();
  phoc_output_damage_whole (self);

  update_output_manager_config (self->desktop);

  if (server->debug_flags & PHOC_SERVER_DEBUG_FLAG_CUTOUTS) {
    PhocOutputPrivate *priv = phoc_output_get_instance_private (self);

    priv->cutouts = phoc_cutouts_overlay_new (phoc_server_get_compatibles (server));
    if (priv->cutouts) {
      priv->cutouts_texture = phoc_cutouts_overlay_get_cutouts_texture (priv->cutouts, self);
      priv->render_cutouts_id =  g_signal_connect (renderer, "render-end",
                                                   G_CALLBACK (render_cutouts),
                                                   self);
    } else {
      g_warning ("Could't create cutout overlay");
    }
  }

  return TRUE;
}

static void
phoc_output_finalize (GObject *object)
{
  PhocOutput *self = PHOC_OUTPUT (object);
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);

  wl_list_remove (&self->link);
  wl_list_remove (&self->enable.link);
  wl_list_remove (&self->mode.link);
  wl_list_remove (&self->commit.link);
  wl_list_remove (&self->output_destroy.link);
  g_clear_list (&self->debug_touch_points, g_free);
  /* Remove all frame callbacks, this will also free associated user data */
  g_clear_slist (&priv->frame_callbacks,
                 (GDestroyNotify)phoc_output_frame_callback_info_free);

  wl_list_init (&self->layer_surfaces);

  g_clear_object (&priv->cutouts);
  g_clear_pointer (&priv->cutouts_texture, wlr_texture_destroy);
  g_clear_signal_handler (&priv->render_cutouts_id, self);
  g_clear_object (&priv->shield);
  g_clear_object (&self->desktop);

  G_OBJECT_CLASS (phoc_output_parent_class)->finalize (object);
}

static void
phoc_output_initable_iface_init (GInitableIface *iface)
{
  iface->init = phoc_output_initable_init;
}

static void
phoc_output_class_init (PhocOutputClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->set_property = phoc_output_set_property;
  object_class->get_property = phoc_output_get_property;

  object_class->finalize = phoc_output_finalize;

  props[PROP_DESKTOP] =
    g_param_spec_object (
      "desktop",
      "Desktop",
      "The desktop object",
      PHOC_TYPE_DESKTOP,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_WLR_OUTPUT] =
    g_param_spec_pointer (
      "wlr-output",
      "wlr-output",
      "The wlroots output object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[OUTPUT_DESTROY] = g_signal_new ("output-destroyed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL, NULL, G_TYPE_NONE, 0);

}

static void
phoc_output_for_each_surface_iterator (struct wlr_surface *surface,
                                       int                 sx,
                                       int                 sy,
                                       void               *_data)
{
  PhocOutputSurfaceIteratorData *data = _data;

  struct wlr_box box;
  bool intersects = get_surface_box (data, surface, sx, sy, &box);

  if (!intersects) {
    return;
  }

  data->user_iterator (data->output, surface, &box, data->rotation,
                       data->scale, data->user_data);
}

/**
 * phoc_output_surface_for_each_surface:
 * @self: the output
 * @surface: The wlr_surface
 * @ox: x position in output coordinates
 * @oy: y position in output coordinates
 * @iterator: (scope call): The callback invoked on each iteration
 * @user_data: Callback user data
 *
 * Iterate over surfaces in a `wlr_surface`s surface tree.
 */
void
phoc_output_surface_for_each_surface (PhocOutput          *self,
                                      struct wlr_surface  *surface,
                                      double               ox,
                                      double               oy,
                                      PhocSurfaceIterator  iterator,
                                      void                *user_data)
{
  PhocOutputSurfaceIteratorData data = {
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

/**
 * phoc_output_xdg_surface_for_each_surface:
 * @self: the output
 * @xdg_surface: The wlr_xdg_surface
 * @ox: x position in output coordinates
 * @oy: y position in output coordinates
 * @iterator: (scope call): The callback invoked on each iteration
 * @user_data: Callback user data
 *
 * Iterate over surfaces in a `wlr_xdg_surface`s surface tree.
 */
void
phoc_output_xdg_surface_for_each_surface (PhocOutput             *self,
                                          struct wlr_xdg_surface *xdg_surface,
                                          double                  ox,
                                          double                  oy,
                                          PhocSurfaceIterator     iterator,
                                          void                   *user_data)
{
  PhocOutputSurfaceIteratorData data = {
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

/**
 * phoc_output_view_for_each_surface:
 * @self: the output
 * @view: The [type@View]
 * @iterator: (scope call): The callback invoked on each iteration
 * @user_data: Callback user data
 *
 * Iterate over surfaces in a [type@View]s surface tree.
 */
void
phoc_output_view_for_each_surface (PhocOutput          *self,
                                   PhocView            *view,
                                   PhocSurfaceIterator  iterator,
                                   void                *user_data)
{
  struct wlr_box *output_box =
    wlr_output_layout_get_box (self->desktop->layout, self->wlr_output);

  if (!output_box) {
    return;
  }

  PhocOutputSurfaceIteratorData data = {
    .user_iterator = iterator,
    .user_data = user_data,
    .output = self,
    .ox = view->box.x - output_box->x,
    .oy = view->box.y - output_box->y,
    .width = view->box.width,
    .height = view->box.height,
    .rotation = 0,
    .scale = phoc_view_get_scale (view)
  };

  phoc_view_for_each_surface (view, phoc_output_for_each_surface_iterator, &data);
}

#ifdef PHOC_XWAYLAND
/**
 * phoc_output_xwayland_children_for_each_surface:
 * @self: the output
 * @surface: The wlr_xwayland_surface
 * @iterator: (scope call): The callback invoked on each iteration
 * @user_data: Callback user data
 *
 * Iterate over children of a `wlr_xwayland_surface`.
 */
void
phoc_output_xwayland_children_for_each_surface (PhocOutput                  *self,
                                                struct wlr_xwayland_surface *surface,
                                                PhocSurfaceIterator          iterator,
                                                void                        *user_data)
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
phoc_output_layer_handle_surface (PhocOutput          *self,
                                  PhocLayerSurface    *layer_surface,
                                  PhocSurfaceIterator  iterator,
                                  void                *user_data)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface_v1 = layer_surface->layer_surface;

  phoc_output_surface_for_each_surface (self, wlr_layer_surface_v1->surface,
                                        layer_surface->geo.x,
                                        layer_surface->geo.y, iterator,
                                        user_data);

  struct wlr_xdg_popup *state;
  wl_list_for_each (state, &wlr_layer_surface_v1->popups, link) {
    struct wlr_xdg_surface *popup = state->base;
    if (!popup->configured)
      continue;

    double popup_sx, popup_sy;
    popup_sx = layer_surface->geo.x;
    popup_sx += popup->popup->geometry.x - popup->current.geometry.x;
    popup_sy = layer_surface->geo.y;
    popup_sy += popup->popup->geometry.y - popup->current.geometry.y;

    phoc_output_xdg_surface_for_each_surface (self, popup,
                                              popup_sx, popup_sy, iterator, user_data);
  }
}

/**
 * phoc_output_layer_for_each_surface:
 * @self: the output
 * @layer: The layer that should be iterated over
 * @iterator: (scope call): The callback invoked on each iteration
 * @user_data: Callback user data
 *
 * Iterate over [type@LayerSurface]s in a layer.
 */
void
phoc_output_layer_for_each_surface (PhocOutput          *self,
                                    enum zwlr_layer_shell_v1_layer layer,
                                    PhocSurfaceIterator  iterator,
                                    void                *user_data)
{
  PhocLayerSurface *layer_surface;

  wl_list_for_each_reverse (layer_surface, &self->layer_surfaces, link) {
    if (layer_surface->layer != layer)
      continue;

    if (layer_surface->layer_surface->current.exclusive_zone <= 0)
      phoc_output_layer_handle_surface (self, layer_surface, iterator, user_data);
  }
  wl_list_for_each (layer_surface, &self->layer_surfaces, link) {
    if (layer_surface->layer != layer)
      continue;

    if (layer_surface->layer_surface->current.exclusive_zone > 0)
      phoc_output_layer_handle_surface (self, layer_surface, iterator, user_data);
  }
}

/**
 * phoc_output_drag_icons_for_each_surface:
 * @self: the output
 * @input: a [type@Input]
 * @iterator: (scope call): The iterator
 * @user_data: Callback user data
 *
 * Iterate over the surface tree of the drag icon's surface of an input's seats.
 */
void
phoc_output_drag_icons_for_each_surface (PhocOutput          *self,
                                         PhocInput           *input,
                                         PhocSurfaceIterator  iterator,
                                         void                *user_data)
{
  struct wlr_box *output_box =
    wlr_output_layout_get_box (self->desktop->layout, self->wlr_output);

  if (!output_box) {
    return;
  }

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    PhocDragIcon *drag_icon = seat->drag_icon;
    if (!drag_icon || !drag_icon->wlr_drag_icon->mapped) {
      continue;
    }

    double ox = drag_icon->x - output_box->x;
    double oy = drag_icon->y - output_box->y;
    phoc_output_surface_for_each_surface (self, drag_icon->wlr_drag_icon->surface,
                                          ox, oy, iterator, user_data);
  }
}

/**
 * phoc_output_for_each_surface:
 * @self: the output
 * @iterator: (scope call): The iterator
 * @user_data: Callback user data
 * @visible_only: Whether to only iterate over visible surfaces
 *
 * Iterate over surfaces on the output.
 */
void
phoc_output_for_each_surface (PhocOutput          *self,
                              PhocSurfaceIterator  iterator,
                              void                *user_data,
                              gboolean             visible_only)
{
  PhocDesktop *desktop = self->desktop;
  PhocServer *server = phoc_server_get_default ();

  if (self->fullscreen_view != NULL) {
    PhocView *view = self->fullscreen_view;

    phoc_output_view_for_each_surface (self, view, iterator, user_data);

#ifdef PHOC_XWAYLAND
    if (PHOC_IS_XWAYLAND_SURFACE (view)) {
      PhocXWaylandSurface *xwayland_surface =
        phoc_xwayland_surface_from_view (view);
      phoc_output_xwayland_children_for_each_surface (self,
                                                      xwayland_surface->xwayland_surface,
                                                      iterator, user_data);
    }
#endif
  } else {
    PhocView *view;
    wl_list_for_each_reverse (view, &desktop->views, link)
    {
      if (!visible_only || phoc_desktop_view_is_visible (desktop, view)) {
        phoc_output_view_for_each_surface (self, view, iterator, user_data);
      }
    }
  }

  phoc_output_drag_icons_for_each_surface (self, server->input,
                                           iterator, user_data);

  for (enum zwlr_layer_shell_v1_layer layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
       layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer++) {
    phoc_output_layer_for_each_surface (self, layer, iterator, user_data);
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
phoc_output_get_decoration_box (PhocOutput *self, PhocView *view, struct wlr_box *box)
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
phoc_view_accept_damage (PhocOutput *self, PhocView  *view)
{
  PhocServer *server = phoc_server_get_default ();

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
  if (PHOC_IS_XWAYLAND_SURFACE (self->fullscreen_view) && PHOC_IS_XWAYLAND_SURFACE (view)) {
    // Special case: accept damage from children
    struct wlr_xwayland_surface *xsurface =
      phoc_xwayland_surface_from_view (view)->xwayland_surface;
    struct wlr_xwayland_surface *fullscreen_xsurface =
      phoc_xwayland_surface_from_view (self->fullscreen_view)->xwayland_surface;
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

  if (*whole) {
    phoc_utils_rotated_bounds (&box, &box, rotation);
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
damage_whole_view (PhocOutput *self, PhocView  *view)
{
  if (!phoc_view_is_mapped (view)) {
    return;
  }

  struct wlr_box box;

  phoc_output_get_decoration_box (self, view, &box);

  wlr_output_damage_add_box (self->damage, &box);
}


/**
 * phoc_output_damage_from_view:
 * @self: The output to add damage to
 * @view: The view providing the damage
 * @whole: Whether
 *
 * Adds a [type@PhocView]'s damage to the damaged area of @self. If
 * @whole is %TRUE the whole view is damaged (including any window
 * decorations if they exist). If @whole is %FALSE only buffer damage
 * is taken into account.
 * Also schedules a new frame.
 */
void
phoc_output_damage_from_view (PhocOutput *self, PhocView  *view, bool whole)
{
  if (!phoc_view_accept_damage (self, view)) {
    return;
  }

  if (whole)
    damage_whole_view (self, view);

  phoc_output_view_for_each_surface (self, view, damage_surface_iterator, &whole);
}

void
phoc_output_damage_whole_drag_icon (PhocOutput *self, PhocDragIcon *icon)
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
      phoc_view_set_fullscreen (output->fullscreen_view, true, wlr_output);
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
  bool current;

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

  current = self->wlr_output->enabled;
  if (self->wlr_output->pending.committed & WLR_OUTPUT_STATE_ENABLED)
    current = self->wlr_output->pending.enabled;

  if (enable == current)
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

  return FALSE;
}

/**
 * phoc_output_is_match:
 * @self: The output
 * @make: The make / vendor name
 * @model: The model / product name
 * @serial: The serial number
 *
 * Checks if an output matches the given vendor/product/serial information.
 * This is usually used to match on an outputs EDID information.
 *
 * Returns: %TRUE if the output matches the given information, otherwise %FALSE.
 */
gboolean
phoc_output_is_match (PhocOutput *self,
                      const char *make,
                      const char *model,
                      const char *serial)
{
  gboolean match;

  g_assert (PHOC_IS_OUTPUT (self));

  match = (g_strcmp0 (self->wlr_output->make, make) == 0 &&
           g_strcmp0 (self->wlr_output->model, model) == 0 &&
           g_strcmp0 (self->wlr_output->serial, serial) == 0);

  return match;
}

/**
 * phoc_output_has_fullscreen_view:
 * @self: The #PhocOutput
 *
 * Returns: %TRUE if the output has a fullscreen view attached,
 *          %FALSE otherwise.
 */
gboolean
phoc_output_has_fullscreen_view (PhocOutput *self)
{
  g_assert (PHOC_IS_OUTPUT (self));

  return self->fullscreen_view != NULL && self->fullscreen_view->wlr_surface != NULL;
}


guint
phoc_output_add_frame_callback  (PhocOutput        *self,
                                 PhocAnimatable    *animatable,
                                 PhocFrameCallback  callback,
                                 gpointer           user_data,
                                 GDestroyNotify     notify)
{
  PhocOutputPrivate *priv;
  PhocOutputFrameCallbackInfo *cb_info = g_new0 (PhocOutputFrameCallbackInfo, 1);

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  *cb_info = (PhocOutputFrameCallbackInfo) {
    .animatable = animatable,
    .callback = callback,
    .user_data = user_data,
    .notify = notify,
    .id = priv->frame_callback_next_id,
  };

  if (priv->frame_callbacks == NULL) {
    priv->last_frame_us = g_get_monotonic_time ();
    /* No other frame callbacks so need to schedule a frame to keep
     * frame clock ticking */
    wlr_output_schedule_frame (self->wlr_output);
  }

  priv->frame_callbacks = g_slist_prepend (priv->frame_callbacks, cb_info);
  return priv->frame_callback_next_id++;
}


void
phoc_output_remove_frame_callback  (PhocOutput *self, guint id)
{
  PhocOutputPrivate *priv;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  for (GSList *elem = priv->frame_callbacks; elem; elem = elem->next) {
    PhocOutputFrameCallbackInfo *cb_info = elem->data;

    if (cb_info->id == id) {
      phoc_output_frame_callback_info_free (cb_info);
      priv->frame_callbacks = g_slist_delete_link (priv->frame_callbacks, elem);
      return;
    }
  }
  g_return_if_reached();
}


static gint
find_cb_by_animatable (PhocOutputFrameCallbackInfo *info,  PhocAnimatable *animatable)
{
  g_assert (PHOC_IS_ANIMATABLE (animatable));

  return !(info->animatable == animatable);
}

/**
 * phoc_output_remove_frame_callbacks_by_animatable:
 * @self: The output to remove the frame callbacks from
 * @animatable: The animatable to remove
 *
 * Remove all frame callbacks for the given #PhocAnimatable.
 */
void
phoc_output_remove_frame_callbacks_by_animatable  (PhocOutput *self, PhocAnimatable *animatable)
{
  PhocOutputPrivate *priv;
  GSList *found;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  do {
    found = g_slist_find_custom (priv->frame_callbacks, animatable,
                                 (GCompareFunc)find_cb_by_animatable);
    if (found)
      priv->frame_callbacks = g_slist_delete_link (priv->frame_callbacks, found);

  } while (found);
}


bool
phoc_output_has_frame_callbacks  (PhocOutput *self)
{
  PhocOutputPrivate *priv;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  return !!priv->frame_callbacks;
}


void
phoc_output_lower_shield (PhocOutput *self)
{
  PhocOutputPrivate *priv;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  if (priv->shield == NULL)
    return;

  phoc_output_shield_lower (priv->shield);
}


void
phoc_output_raise_shield (PhocOutput *self)
{
  PhocOutputPrivate *priv;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  if (priv->shield == NULL)
    priv->shield = phoc_output_shield_new (self);

  phoc_output_shield_raise (priv->shield);
}

/**
 * phoc_output_has_layer:
 * @self: The #PhocOutput
 *
 * Returns: %TRUE if the output has a Phoc.LayerSurface in the given layer.
 *          %FALSE otherwise.
 */
gboolean
phoc_output_has_layer (PhocOutput *self, enum zwlr_layer_shell_v1_layer layer)
{
  PhocLayerSurface *layer_surface;

  g_assert (PHOC_IS_OUTPUT (self));

  wl_list_for_each (layer_surface, &self->layer_surfaces, link) {
    if (layer_surface->layer == layer)
      return TRUE;
  }

  return FALSE;
}


static gboolean
should_reveal_shell (PhocOutput *self)
{
  PhocOutputPrivate *priv;
  PhocServer *server = phoc_server_get_default();
  PhocLayerSurface *layer_surface;
  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  /* is our layer-surface focused on some seat? */
  for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);
    if (seat->focused_layer && seat->focused_layer->output == self->wlr_output) {
      return true;
    }
  }

  /* is some draggable surface unfolded, being dragged or animated? */
  wl_list_for_each (layer_surface, &self->layer_surfaces, link) {
    PhocDraggableLayerSurface *draggable =
        phoc_desktop_get_draggable_layer_surface (server->desktop, layer_surface);
    if (draggable &&
        (phoc_draggable_layer_surface_get_state (draggable) != PHOC_DRAGGABLE_SURFACE_STATE_NONE ||
         phoc_draggable_layer_surface_is_unfolded (draggable))) {
      return true;
    }
  }

  /* is shell reveal forced by user gesture? */
  return priv->force_shell_reveal;
}


/**
 * phoc_output_update_shell_reveal:
 * @self: The #PhocOutput
 *
 * Updates shell reveal status of given output.
 */
void
phoc_output_update_shell_reveal (PhocOutput *self)
{
  PhocOutputPrivate *priv;
  gboolean old;
  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  old = priv->shell_revealed;
  priv->shell_revealed = should_reveal_shell (self);

  if (priv->shell_revealed != old) {
    phoc_output_damage_whole (self);
  }
}


/**
 * phoc_output_force_shell_reveal:
 * @self: The #PhocOutput
 * @force: %TRUE to forcefully reveal shell; %FALSE otherwise.
 *
 * Allows to force shell reveal status regardless of whether
 * other conditions are being fulfilled.
 */
void
phoc_output_force_shell_reveal (PhocOutput *self, gboolean force)
{
  PhocOutputPrivate *priv;
  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  priv->force_shell_reveal = force;
  phoc_output_update_shell_reveal (self);
}


/**
 * phoc_output_has_shell_revealed:
 * @self: The #PhocOutput
 *
 * Returns: %TRUE if layer-shell's TOP layer should appear on top
 *          of fullscreen windows on this output.
 *          %FALSE otherwise.
 */
gboolean
phoc_output_has_shell_revealed (PhocOutput *self)
{
  PhocOutputPrivate *priv;
  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);
  return priv->shell_revealed;
}


float
phoc_output_get_scale (PhocOutput *self)
{
  g_assert (PHOC_IS_OUTPUT (self));
  g_assert (self->wlr_output);

  return self->wlr_output->scale;
}
