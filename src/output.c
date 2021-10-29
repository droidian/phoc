#define G_LOG_DOMAIN "phoc-output"

#include "phoc-config.h"
#include "phoc-tracing.h"

#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/drm.h>
#include <wlr/config.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/android.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/region.h>

#include "anim/animatable.h"
#include "bling.h"
#include "cursor.h"
#include "cutouts-overlay.h"
#include "settings.h"
#include "layer-shell.h"
#include "layer-shell-effects.h"
#include "output.h"
#include "output-shield.h"
#include "render.h"
#include "render-private.h"
#include "seat.h"
#include "server.h"
#include "input-method-relay.h"
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
  PhocRenderer            *renderer;
  PhocOutputShield        *shield;

  GSList                  *frame_callbacks;
  gint                     frame_callback_next_id;
  gint64                   last_frame_us;

  PhocCutoutsOverlay      *cutouts;
  gulong                   render_cutouts_id;
  struct wlr_texture      *cutouts_texture;

  gboolean shell_revealed;
  gboolean force_shell_reveal;

  struct wl_listener     damage;
  struct wl_listener     frame;
  struct wl_listener     needs_frame;
  struct wl_listener     request_state;

  PhocOutputScaleFilter  scale_filter;
  gboolean               gamma_lut_changed;

  GQueue                *layer_surfaces[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY + 1];
} PhocOutputPrivate;

static void phoc_output_initable_iface_init (GInitableIface *iface);

static void phoc_output_animatable_interface_init (PhocAnimatableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocOutput, phoc_output, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (PhocOutput)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, phoc_output_initable_iface_init)
                         G_IMPLEMENT_INTERFACE (PHOC_TYPE_ANIMATABLE,
                                                phoc_output_animatable_interface_init))

#define PHOC_OUTPUT_SELF(p) PHOC_PRIV_CONTAINER(PHOC_OUTPUT, PhocOutput, (p))

static void phoc_output_for_each_surface (PhocOutput          *self,
                                          PhocSurfaceIterator  iterator,
                                          void                *user_data,
                                          gboolean             visible_only);

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
  float                scale;
} PhocOutputSurfaceIteratorData;


static void
phoc_output_frame_callback_info_free (PhocOutputFrameCallbackInfo *cb_info)
{
  if (cb_info->notify && cb_info->user_data)
    cb_info->notify (cb_info->user_data);
  g_free (cb_info);
}

/**
 * get_surface_box:
 *
 * Returns: `true` if the resulting box intersects with the output
 */
static bool
get_surface_box (PhocOutputSurfaceIteratorData *data,
                 struct wlr_surface            *wlr_surface,
                 int                            sx,
                 int                            sy,
                 struct wlr_box                *surface_box)
{
  PhocOutput *self = data->output;

  if (!wlr_surface_has_buffer (wlr_surface))
    return false;

  struct wlr_box box = {
    .x = floor (data->ox + sx),
    .y = floor (data->oy + sy),
    .width = wlr_surface->current.width,
    .height = wlr_surface->current.height,
  };

  if (surface_box != NULL)
    *surface_box = box;

  struct wlr_box output_box = {0};
  wlr_output_effective_resolution (self->wlr_output, &output_box.width, &output_box.height);
  phoc_utils_scale_box (&output_box, 1 / data->scale);

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

/* {{{ Animatable Interface */

static guint
_phoc_output_add_frame_callback (PhocAnimatable    *iface,
                                 PhocFrameCallback  callback,
                                 gpointer           user_data,
                                 GDestroyNotify     notify)
{
  PhocOutput *self = PHOC_OUTPUT (iface);

  g_assert (PHOC_IS_OUTPUT (self));

  return phoc_output_add_frame_callback (self, iface, callback, user_data, notify);
}


static void
_phoc_output_remove_frame_callback (PhocAnimatable *iface, guint id)
{
  PhocOutput *self = PHOC_OUTPUT (iface);

  g_assert (PHOC_IS_OUTPUT (self));

  phoc_output_remove_frame_callback (self, id);
}


static void
phoc_output_animatable_interface_init (PhocAnimatableInterface *iface)
{
  iface->add_frame_callback = _phoc_output_add_frame_callback;
  iface->remove_frame_callback = _phoc_output_remove_frame_callback;
}

/* }}} */

static void
phoc_output_init (PhocOutput *self)
{
  PhocServer *server = phoc_server_get_default ();
  PhocOutputPrivate *priv = phoc_output_get_instance_private(self);

  priv->frame_callback_next_id = 1;
  priv->last_frame_us = g_get_monotonic_time ();
  priv->shield = phoc_output_shield_new (self);

  self->debug_touch_points = NULL;
  wl_list_init (&self->layer_surfaces);

  priv->scale_filter = PHOC_OUTPUT_SCALE_FILTER_AUTO;

  priv->renderer = g_object_ref (phoc_server_get_renderer (server));
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
  struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create ();
  PhocOutput *output;

  wl_list_for_each (output, &desktop->outputs, link) {
    struct wlr_output_configuration_head_v1 *config_head;
    struct wlr_box output_box;

    config_head = wlr_output_configuration_head_v1_create (config, output->wlr_output);
    wlr_output_layout_get_box (output->desktop->layout, output->wlr_output, &output_box);
    if (!wlr_box_empty (&output_box)) {
      config_head->state.x = output_box.x;
      config_head->state.y = output_box.y;
    }
  }

  wlr_output_manager_v1_set_configuration (desktop->output_manager_v1, config);
}

static void
phoc_output_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, output_destroy);

  if (self->fullscreen_view)
    phoc_view_set_fullscreen (self->fullscreen_view, false, NULL);

  g_signal_emit (self, signals[OUTPUT_DESTROY], 0);
}


static void
render_cutouts (PhocOutput *self, PhocRenderContext *ctx)
{
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);

  g_assert (PHOC_IS_OUTPUT (self));

  if (priv->cutouts_texture) {
    struct wlr_texture *texture = priv->cutouts_texture;

    wlr_render_pass_add_texture (ctx->render_pass, &(struct wlr_render_texture_options) {
        .texture = texture,
        .transform = WL_OUTPUT_TRANSFORM_NORMAL,
        .filter_mode = phoc_output_get_texture_filter_mode (ctx->output),
    });
  }
}


static void
phoc_output_handle_damage (struct wl_listener *listener, void *user_data)
{
  PhocOutputPrivate *priv = wl_container_of (listener, priv, damage);
  PhocOutput *self = PHOC_OUTPUT_SELF (priv);
  struct wlr_output_event_damage *event = user_data;

  if (wlr_damage_ring_add (&self->damage_ring, event->damage))
    wlr_output_schedule_frame (self->wlr_output);
}


static void
phoc_output_set_gamma_lut (PhocOutput *self, struct wlr_output_state *pending)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);
  struct wlr_gamma_control_v1 *gamma_control;

  gamma_control = wlr_gamma_control_manager_v1_get_control (desktop->gamma_control_manager_v1,
                                                            self->wlr_output);
  priv->gamma_lut_changed = FALSE;

  if (!wlr_gamma_control_v1_apply (gamma_control, pending))
    return;

  if (!wlr_output_test_state (self->wlr_output, pending)) {
    wlr_output_state_finish (pending);
    wlr_gamma_control_v1_send_failed_and_destroy (gamma_control);
    *pending = (struct wlr_output_state){0};
  }
}


static void
surface_send_frame_done_iterator (PhocOutput         *output,
                                  struct wlr_surface *wlr_surface,
                                  struct wlr_box     *box,
                                  float               scale,
                                  void               *data)
{
  struct timespec *when = data;

  wlr_surface_send_frame_done (wlr_surface, when);
}


static void
count_surface_iterator (PhocOutput         *output,
                        struct wlr_surface *wlr_surface,
                        struct wlr_box     *box,
                        float               scale,
                        void               *data)
{
  size_t *n = data;

  (*n)++;
}


PHOC_TRACE_NO_INLINE static bool
scan_out_fullscreen_view (PhocOutput *self, PhocView *view, struct wlr_output_state *pending)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  struct wlr_output *wlr_output = self->wlr_output;
  size_t n_surfaces = 0;
  struct wlr_surface *wlr_surface;

  g_assert (PHOC_IS_VIEW (view));

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);
    PhocDragIcon *drag_icon;

    g_assert (PHOC_IS_SEAT (seat));
    drag_icon = seat->drag_icon;

    if (phoc_drag_icon_is_mapped (drag_icon))
      return false;
  }

  if (phoc_output_has_shell_revealed (self))
    return false;

  if (phoc_output_has_layer (self, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY))
    return false;

  if (!phoc_view_is_mapped (view))
    return false;

  phoc_output_view_for_each_surface (self, view, count_surface_iterator, &n_surfaces);
  if (n_surfaces > 1)
    return false;

#ifdef PHOC_XWAYLAND
  if (PHOC_IS_XWAYLAND_SURFACE (view)) {
    struct wlr_xwayland_surface *xsurface =
      phoc_xwayland_surface_get_wlr_surface (PHOC_XWAYLAND_SURFACE (view));
    if (!wl_list_empty (&xsurface->children)) {
      return false;
    }
  }
#endif

  wlr_surface = view->wlr_surface;
  if (wlr_surface->buffer == NULL)
    return false;

  if ((float)wlr_surface->current.scale != wlr_output->scale ||
      wlr_surface->current.transform != wlr_output->transform) {
    return false;
  }

  if (!wlr_output_is_direct_scanout_allowed (wlr_output))
    return false;

  wlr_output_state_set_buffer (pending, &wlr_surface->buffer->base);
  if (!wlr_output_test_state (wlr_output, pending))
    return false;

  wlr_presentation_surface_scanned_out_on_output (self->desktop->presentation,
                                                  wlr_surface,
                                                  wlr_output);

  return wlr_output_commit_state (wlr_output, pending);
}


static void
get_frame_damage (PhocOutput *self, pixman_region32_t *frame_damage)
{
  PhocServer *server = phoc_server_get_default ();
  int width, height;
  enum wl_output_transform transform;

  wlr_output_transformed_resolution (self->wlr_output, &width, &height);

  pixman_region32_init (frame_damage);

  transform = wlr_output_transform_invert (self->wlr_output->transform);
  wlr_region_transform (frame_damage, &self->damage_ring.current, transform, width, height);

  if (G_UNLIKELY (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_DAMAGE_TRACKING))) {
    pixman_region32_union_rect (frame_damage, frame_damage,
                                0, 0, self->wlr_output->width, self->wlr_output->height);

  }
}


PHOC_TRACE_NO_INLINE static void
phoc_output_draw (PhocOutput *self)
{
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);
  struct wlr_output *wlr_output = self->wlr_output;
  bool needs_frame, scanned_out = false;
  pixman_region32_t buffer_damage;
  int buffer_age;
  PhocRenderContext render_context;
  struct wlr_buffer *buffer;
  struct wlr_render_pass *render_pass;
  struct wlr_output_state pending = { 0 };

  if (!wlr_output->enabled)
    return;

  needs_frame = wlr_output->needs_frame;
  needs_frame |= pixman_region32_not_empty (&self->damage_ring.current);
  needs_frame |= priv->gamma_lut_changed;

  if (!needs_frame)
    return;

  if (G_UNLIKELY (priv->gamma_lut_changed))
    phoc_output_set_gamma_lut (self, &pending);

  pending.committed |= WLR_OUTPUT_STATE_DAMAGE;
  get_frame_damage (self, &pending.damage);

  /* Check if we can delegate the fullscreen surface to the output */
  if (phoc_output_has_fullscreen_view (self))
    scanned_out = scan_out_fullscreen_view (self, self->fullscreen_view, &pending);

  if (scanned_out)
    goto out;

  if (!wlr_output_configure_primary_swapchain (wlr_output, &pending, &wlr_output->swapchain))
    goto  out;

  buffer = wlr_swapchain_acquire (wlr_output->swapchain, &buffer_age);
  if (!buffer)
    goto out;

  if (wlr_renderer_is_android(wlr_output->renderer))
    buffer_age = wlr_renderer_get_buffer_age (wlr_output->renderer, buffer);

  render_pass = wlr_renderer_begin_buffer_pass_for_output (wlr_output->renderer, buffer, NULL, (void*)wlr_output);
  if (!render_pass) {
    wlr_buffer_unlock (buffer);
    goto out;
  }

  pixman_region32_init (&buffer_damage);
  wlr_damage_ring_get_buffer_damage (&self->damage_ring, buffer_age, &buffer_damage);

  render_context = (PhocRenderContext){
    .output = self,
    .damage = &buffer_damage,
    .alpha = 1.0,
    .render_pass = render_pass,
  };
  phoc_renderer_render_output (priv->renderer, self, &render_context);

  pixman_region32_fini (&buffer_damage);

  if (!wlr_render_pass_submit (render_pass)) {
    wlr_buffer_unlock (buffer);
    goto out;
  }

  wlr_output_state_set_buffer (&pending, buffer);
  wlr_buffer_unlock (buffer);

  if (!wlr_output_commit_state (wlr_output, &pending))
    goto out;

  wlr_damage_ring_rotate (&self->damage_ring);

 out:
  wlr_output_state_finish (&pending);
}


static void
phoc_output_handle_frame (struct wl_listener *listener, void *data)
{
  PhocOutputPrivate *priv = wl_container_of (listener, priv, frame);
  PhocOutput *self = PHOC_OUTPUT_SELF (priv);
  struct timespec now;

  /* Process all registered frame callbacks */
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

  /* Ensure the cutouts are drawn */
  if (G_UNLIKELY (priv->cutouts_texture)) {
    struct wlr_box box = { 0, 0, priv->cutouts_texture->width, priv->cutouts_texture->height };
    if (wlr_damage_ring_add_box (&self->damage_ring, &box))
      wlr_output_schedule_frame (self->wlr_output);
  }

  /* Repaint the output */
  phoc_output_draw (self);

  /* Send frame done events to all visible surfaces */
  clock_gettime (CLOCK_MONOTONIC, &now);
  phoc_output_for_each_surface (self, surface_send_frame_done_iterator, &now, true);

  /* Want frame clock ticking as long as we have frame callbacks */
  if (priv->frame_callbacks)
    wlr_output_schedule_frame (self->wlr_output);
}


static void
phoc_output_handle_needs_frame (struct wl_listener *listener, void *user_data)
{
  PhocOutputPrivate *priv = wl_container_of (listener, priv, needs_frame);
  PhocOutput *self = PHOC_OUTPUT_SELF (priv);

  wlr_output_schedule_frame (self->wlr_output);
}


static void
update_output_scale_iterator (PhocOutput         *self,
                              struct wlr_surface *wlr_surface,
                              struct wlr_box     *box,
                              float               scale,
                              void               *user_data)


{
  phoc_utils_wlr_surface_update_scales (wlr_surface);
}


static void
phoc_output_handle_commit (struct wl_listener *listener, void *data)
{
  PhocOutput *self = wl_container_of (listener, self, commit);
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);
  struct wlr_output_event_commit *event = data;

  if (event->state->committed & (WLR_OUTPUT_STATE_MODE |
                                 WLR_OUTPUT_STATE_SCALE |
                                 WLR_OUTPUT_STATE_TRANSFORM)) {
    phoc_layer_shell_arrange (self);
  }

  if (event->state->committed & (WLR_OUTPUT_STATE_ENABLED |
                                 WLR_OUTPUT_STATE_MODE |
                                 WLR_OUTPUT_STATE_SCALE |
                                 WLR_OUTPUT_STATE_TRANSFORM)) {
    update_output_manager_config (self->desktop);
  }

  if (event->state->committed & (WLR_OUTPUT_STATE_MODE |
                                 WLR_OUTPUT_STATE_TRANSFORM)) {
    int width, height;
    wlr_output_transformed_resolution (self->wlr_output, &width, &height);
    wlr_damage_ring_set_bounds (&self->damage_ring, width, height);
    wlr_output_schedule_frame (self->wlr_output);
  }

  if (event->state->committed & WLR_OUTPUT_STATE_ENABLED && self->wlr_output->enabled) {
    priv->gamma_lut_changed = TRUE;
    wlr_output_schedule_frame (self->wlr_output);
  }

  if (event->state->committed & WLR_OUTPUT_STATE_SCALE)
    phoc_output_for_each_surface (self, update_output_scale_iterator, NULL, FALSE);
}


static void
handle_request_state (struct wl_listener *listener, void *data)
{
  PhocOutputPrivate *priv = wl_container_of (listener, priv, request_state);
  PhocOutput *self = PHOC_OUTPUT_SELF (priv);
  const struct wlr_output_event_request_state *event = data;

  wlr_output_commit_state (self->wlr_output, event->state);
}


static float
phoc_output_compute_scale (PhocOutput *self, struct wlr_output_state *pending)
{
  int32_t width = 0, height = 0;

  if (!self->wlr_output->phys_width || !self->wlr_output->phys_height) {
    g_message ("Output '%s' has invalid physical size, "
               "using default scale", self->wlr_output->name);
    return 1;
  }

  /*  Use the pending mode if any */
  if (pending->committed & WLR_OUTPUT_STATE_MODE) {
    switch (pending->mode_type) {
    case WLR_OUTPUT_STATE_MODE_FIXED:
      width = pending->mode->width;
      height = pending->mode->height;
      break;
    case WLR_OUTPUT_STATE_MODE_CUSTOM:
      width = pending->custom_mode.width;
      height = pending->custom_mode.height;
      break;
    default:
      break;
    }
  /* Fall back to current mode */
  } else if (self->wlr_output->current_mode) {
    width = self->wlr_output->current_mode->width;
    height = self->wlr_output->current_mode->height;
  }

  if (!width || !height) {
    g_message ("No valid mode set for output '%s', "
               "using default scale", self->wlr_output->name);
    return 1;
  }

  return phoc_utils_compute_scale (self->wlr_output->phys_width,
                                   self->wlr_output->phys_height,
                                   width, height);
}

static void
phoc_output_state_set_mode (PhocOutput *self, struct wlr_output_state *pending, PhocOutputConfig *oc)
{
  int mhz = (int)(oc->mode.refresh_rate * 1000);

  if (wl_list_empty (&self->wlr_output->modes)) {
    /* Output has no mode, try setting a custom one */
    wlr_output_state_set_custom_mode (pending, oc->mode.width, oc->mode.height, mhz);
    return;
  }

  struct wlr_output_mode *mode, *best = NULL;

  wl_list_for_each (mode, &self->wlr_output->modes, link) {
    if (mode->width == oc->mode.width && mode->height == oc->mode.height) {
      if (mode->refresh == mhz) {
        best = mode;
        break;
      }
      best = mode;
    }
  }
  if (!best) {
    g_warning ("Configured mode for %s not available", self->wlr_output->name);
  } else {
    g_debug ("Assigning configured mode to %s", self->wlr_output->name);
    wlr_output_state_set_mode (pending, best);
  }
}

/**
 * adjust_frac_scale:
 * @scale: The input scale
 *
 * factional-scale-v1 sends increments of 120 to the client so we want
 * the output scale to match that exactly as we otherwise use slightly
 * different scales.
 *
 * Returns: The (possibly adjusted) output scale
 */
static double
adjust_frac_scale (double scale)
{
  double adjusted_scale;

  adjusted_scale = round (scale * 120) / 120;

  if (!G_APPROX_VALUE (scale, adjusted_scale, DBL_EPSILON))
    g_warning ("Adjusting output scale from %f to %f", scale, adjusted_scale);

  return adjusted_scale;
}


static void
phoc_output_fill_state (PhocOutput              *self,
                        PhocOutputConfig        *output_config,
                        struct wlr_output_state *pending)
{
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);
  struct wlr_output_mode *preferred_mode = wlr_output_preferred_mode (self->wlr_output);
  gboolean enable = FALSE;

  wlr_output_state_init (pending);
  wlr_output_state_set_enabled (pending, false);
  if (!output_config || output_config->enable) {
    wlr_output_state_set_enabled (pending, true);
    enable = TRUE;
  }

  if (output_config && enable) {
    enum wl_output_transform transform = output_config->transform;
    double scale;

    if (wlr_output_is_drm (self->wlr_output)) {
      if (output_config->drm_panel_orientation)
        transform = wlr_drm_connector_get_panel_orientation (self->wlr_output);

      for (GSList *l = output_config->modes; l; l = l->next) {
        PhocOutputModeConfig *mode_config = l->data;
        wlr_drm_connector_add_mode (self->wlr_output, &mode_config->info);
      }
    } else if (output_config->modes != NULL) {
      g_warning ("Can only add modes for DRM backend");
    }

    if (output_config->phys_width)
      self->wlr_output->phys_width = output_config->phys_width;

    if (output_config->phys_height)
      self->wlr_output->phys_height = output_config->phys_height;

    if (output_config->mode.width)
      phoc_output_state_set_mode (self, pending, output_config);
    else if (preferred_mode != NULL)
      wlr_output_state_set_mode (pending, preferred_mode);

    if (output_config->scale)
      scale = output_config->scale;
    else
      scale = phoc_output_compute_scale (self, pending);

    wlr_output_state_set_scale (pending, adjust_frac_scale (scale));

    wlr_output_state_set_transform (pending, transform);
    priv->scale_filter = output_config->scale_filter;
  } else if (enable) {
    enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;

    if (preferred_mode != NULL) {
      g_debug ("Using preferred mode for %s", self->wlr_output->name);
      wlr_output_state_set_mode (pending, preferred_mode);
    }

    if (!wlr_output_test_state (self->wlr_output, pending)) {
      g_debug ("Preferred mode rejected for %s falling back to another mode",
               self->wlr_output->name);
      struct wlr_output_mode *mode;
      wl_list_for_each (mode, &self->wlr_output->modes, link) {
        if (mode == preferred_mode)
          continue;

        wlr_output_state_set_mode (pending, mode);
        if (wlr_output_test_state (self->wlr_output, pending))
          break;
      }
    }

    if (wlr_output_is_drm (self->wlr_output))
      transform = wlr_drm_connector_get_panel_orientation (self->wlr_output);

    wlr_output_state_set_scale (pending, phoc_output_compute_scale (self, pending));
    wlr_output_state_set_transform (pending, transform);
  }

  if (output_config && output_config->x > 0 && output_config->y > 0) {
    wlr_output_layout_add (self->desktop->layout, self->wlr_output, output_config->x,
                           output_config->y);
  } else {
    wlr_output_layout_add_auto (self->desktop->layout, self->wlr_output);
  }
}


static gboolean
phoc_output_initable_init (GInitable    *initable,
                           GCancellable *cancellable,
                           GError      **error)
{
  PhocServer *server = phoc_server_get_default ();
  PhocInput *input = phoc_server_get_input (server);
  PhocRenderer *renderer = phoc_server_get_renderer (server);
  PhocOutput *self = PHOC_OUTPUT (initable);
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);
  struct wlr_box output_box;
  int width, height;

  PhocConfig *config = phoc_server_get_config (phoc_server_get_default ());

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

  wlr_damage_ring_init (&self->damage_ring);

  self->output_destroy.notify = phoc_output_handle_destroy;
  wl_signal_add (&self->wlr_output->events.destroy, &self->output_destroy);

  self->commit.notify = phoc_output_handle_commit;
  wl_signal_add (&self->wlr_output->events.commit, &self->commit);

  priv->damage.notify = phoc_output_handle_damage;
  wl_signal_add (&self->wlr_output->events.damage, &priv->damage);

  priv->frame.notify = phoc_output_handle_frame;
  wl_signal_add (&self->wlr_output->events.frame, &priv->frame);

  priv->needs_frame.notify = phoc_output_handle_needs_frame;
  wl_signal_add (&self->wlr_output->events.needs_frame, &priv->needs_frame);

  priv->request_state.notify = handle_request_state;
  wl_signal_add (&self->wlr_output->events.request_state, &priv->request_state);

  PhocOutputConfig *output_config = phoc_config_get_output (config, self);
  struct wlr_output_state pending;
  phoc_output_fill_state (self, output_config, &pending);

  wlr_output_commit_state (self->wlr_output, &pending);
  wlr_output_layout_get_box (self->desktop->layout, self->wlr_output, &output_box);
  self->lx = output_box.x;
  self->ly = output_box.y;

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    phoc_seat_configure_cursor (seat);
    phoc_cursor_configure_xcursor (seat->cursor);
  }

  phoc_layer_shell_arrange (self);
  phoc_layer_shell_update_focus ();
  phoc_output_damage_whole (self);

  update_output_manager_config (self->desktop);

  wlr_output_transformed_resolution (self->wlr_output, &width, &height);
  wlr_damage_ring_set_bounds (&self->damage_ring, width, height);

  if (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_CUTOUTS)) {
    priv->cutouts = phoc_cutouts_overlay_new (phoc_server_get_compatibles (server));
    if (priv->cutouts) {
      g_message ("Adding cutouts overlay");
      priv->cutouts_texture = phoc_cutouts_overlay_get_cutouts_texture (priv->cutouts, self);
      priv->render_cutouts_id = g_signal_connect_swapped (renderer, "render-end",
                                                          G_CALLBACK (render_cutouts),
                                                          self);
    } else {
      g_warning ("Could not create cutout overlay");
    }
  }

  wlr_output_state_finish (&pending);

  g_message ("Output '%s' added ('%s'/'%s'/'%s'), "
             "%" PRId32 "mm x %" PRId32 "mm",
             self->wlr_output->name,
             self->wlr_output->make,
             self->wlr_output->model,
             self->wlr_output->serial,
             self->wlr_output->phys_width,
             self->wlr_output->phys_height);

  return TRUE;
}

static void
phoc_output_finalize (GObject *object)
{
  PhocOutput *self = PHOC_OUTPUT (object);
  PhocOutputPrivate *priv = phoc_output_get_instance_private (self);

  self->wlr_output->data = NULL;
  self->wlr_output = NULL;

  wl_list_remove (&self->link);

  update_output_manager_config (self->desktop);

  wl_list_remove (&self->commit.link);
  wl_list_remove (&self->output_destroy.link);

  wl_list_remove (&priv->request_state.link);
  wl_list_remove (&priv->damage.link);
  wl_list_remove (&priv->frame.link);
  wl_list_remove (&priv->needs_frame.link);
  wlr_damage_ring_finish (&self->damage_ring);

  g_clear_list (&self->debug_touch_points, g_free);
  /* Remove all frame callbacks, this will also free associated user data */
  g_clear_slist (&priv->frame_callbacks,
                 (GDestroyNotify)phoc_output_frame_callback_info_free);

  wl_list_init (&self->layer_surfaces);
  for (int i = 0; i < G_N_ELEMENTS (priv->layer_surfaces); i++)
    g_clear_pointer (&priv->layer_surfaces[i], g_queue_free);

  g_clear_signal_handler (&priv->render_cutouts_id, priv->renderer);
  g_clear_object (&priv->renderer);
  g_clear_object (&priv->cutouts);
  g_clear_pointer (&priv->cutouts_texture, wlr_texture_destroy);
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
phoc_output_for_each_surface_iterator (struct wlr_surface *wlr_surface,
                                       int                 sx,
                                       int                 sy,
                                       void               *_data)
{
  PhocOutputSurfaceIteratorData *data = _data;

  struct wlr_box box;
  bool intersects = get_surface_box (data, wlr_surface, sx, sy, &box);

  if (!intersects) {
    return;
  }

  data->user_iterator (data->output, wlr_surface, &box, data->scale, data->user_data);
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
                                      struct wlr_surface  *wlr_surface,
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
    .width = wlr_surface->current.width,
    .height = wlr_surface->current.height,
    .scale = 1.0
  };

  wlr_surface_for_each_surface (wlr_surface,
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
  struct wlr_box output_box;
  wlr_output_layout_get_box (self->desktop->layout, self->wlr_output, &output_box);

  if (wlr_box_empty (&output_box))
    return;

  PhocOutputSurfaceIteratorData data = {
    .user_iterator = iterator,
    .user_data = user_data,
    .output = self,
    .ox = view->box.x - output_box.x,
    .oy = view->box.y - output_box.y,
    .width = view->box.width,
    .height = view->box.height,
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
  struct wlr_box output_box;
  wlr_output_layout_get_box (self->desktop->layout, self->wlr_output, &output_box);

  if (wlr_box_empty (&output_box))
    return;

  struct wlr_xwayland_surface *child;

  wl_list_for_each (child, &surface->children, parent_link) {
    if (child->surface && child->surface->mapped) {
      double ox = child->x - output_box.x;
      double oy = child->y - output_box.y;
      phoc_output_surface_for_each_surface (self, child->surface, ox, oy, iterator,
                                            user_data);
    }
    phoc_output_xwayland_children_for_each_surface (self, child,
                                                    iterator, user_data);
  }
}
#endif

/**
 * phoc_output_layer_surface_for_each_surface:
 * @self: the output
 * @layer_surface: The layer surface to iterate over
 * @iterator: (scope call): The callback invoked on each iteration
 * @user_data: Callback user data
 *
 * Iterate over a [type@LayerSurface] and it's popups.
 */
void
phoc_output_layer_surface_for_each_surface (PhocOutput          *self,
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
    popup_sx += popup->popup->current.geometry.x - popup->current.geometry.x;
    popup_sy = layer_surface->geo.y;
    popup_sy += popup->popup->current.geometry.y - popup->current.geometry.y;

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
 * Ordering matches `phoc_output_get_layer_surfaces_for_layer`.
 *
 * Iterate over [type@LayerSurface]s in a layer.
 */
static void
phoc_output_layer_for_each_surface (PhocOutput          *self,
                                    enum zwlr_layer_shell_v1_layer layer,
                                    PhocSurfaceIterator  iterator,
                                    void                *user_data)
{
  GQueue *layer_surfaces = phoc_output_get_layer_surfaces_for_layer (self, layer);

  for (GList *l = layer_surfaces->tail; l; l = l->prev) {
    PhocLayerSurface *layer_surface = PHOC_LAYER_SURFACE (l->data);

    phoc_output_layer_surface_for_each_surface (self, layer_surface, iterator, user_data);
  }
}


/**
 * phoc_output_get_layer_surfaces_for_layer:
 * @self: the output
 * @layer: The layer to get the surfaces for
 *
 * Get a list of [type@PhocLayerSurface]s on this output in the given
 * `layer` in rendering order.
 *
 * Returns:(transfer none): The layer surfaces of that layer
 */
GQueue *
phoc_output_get_layer_surfaces_for_layer (PhocOutput *self, enum zwlr_layer_shell_v1_layer layer)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocLayerSurface *layer_surface;
  PhocOutputPrivate *priv;
  g_autoptr (GQueue) queue = NULL;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  if (priv->layer_surfaces[layer])
    return priv->layer_surfaces[layer];

  queue = g_queue_new ();

  wl_list_for_each_reverse (layer_surface, &self->layer_surfaces, link) {
    if (layer_surface->layer != layer)
      continue;

    if (layer_surface->layer_surface->current.exclusive_zone > 0)
      g_queue_push_head (queue, layer_surface);
  }

  wl_list_for_each (layer_surface, &self->layer_surfaces, link) {
    if (layer_surface->layer != layer)
      continue;

    if (layer_surface->layer_surface->current.exclusive_zone <= 0)
      g_queue_push_head (queue, layer_surface);
  }

  GSList *stacks = phoc_desktop_get_layer_surface_stacks (desktop);
  for (GSList *s = stacks; s; s = s->next) {
    PhocStackedLayerSurface *stack = s->data;
    PhocLayerSurface *stacked, *target;
    GList *stacked_link, *target_link;

    if (phoc_stacked_layer_surface_get_layer (stack) != layer)
      continue;

    stacked = phoc_stacked_layer_surface_get_layer_surface (stack);
    if (!stacked)
      continue;

    if (phoc_layer_surface_get_output (stacked) != self)
      continue;

    target = phoc_stacked_layer_surface_get_target_layer_surface (stack);
    if (!target)
      continue;

    if (phoc_layer_surface_get_output (target) != self)
      continue;

    if (phoc_layer_surface_get_layer (target) != phoc_layer_surface_get_layer (stacked)) {
      g_critical ("Stacked surface %s and target %s surface not in same layer",
                  phoc_layer_surface_get_namespace (stacked),
                  phoc_layer_surface_get_namespace (target));
      continue;
    }

    stacked_link = g_queue_find (queue, stacked);
    g_assert (stacked_link);
    g_queue_unlink (queue, stacked_link);

    target_link = g_queue_find (queue, target);
    g_assert (target_link);

    switch (phoc_stacked_layer_surface_get_position (stack)) {
    case PHOC_STACKED_SURFACE_STACK_BELOW:
      g_debug ("Stacking '%s' below '%s'",
               PHOC_LAYER_SURFACE (stacked_link->data)->layer_surface->namespace,
               PHOC_LAYER_SURFACE (target_link->data)->layer_surface->namespace);
      g_queue_insert_before_link (queue, target_link, stacked_link);
      break;
    case PHOC_STACKED_SURFACE_STACK_ABOVE:
      g_debug ("Stacking '%s' above '%s'",
               PHOC_LAYER_SURFACE (stacked_link->data)->layer_surface->namespace,
               PHOC_LAYER_SURFACE (target_link->data)->layer_surface->namespace);
      g_queue_insert_after_link (queue, target_link, stacked_link);
      break;
    default:
      g_assert_not_reached ();
    }
  }

  g_clear_pointer (&priv->layer_surfaces[layer], g_queue_free);
  priv->layer_surfaces[layer] = g_steal_pointer (&queue);
  return priv->layer_surfaces[layer];
}

/**
 * phoc_output_set_layer_dirty:
 * @self: the output
 * @layer: The layer to marks as dirty
 *
 * Invalidate the ordering of layer surfaces.
 *
 * Moving a layer surface between layers or changing the exclusive zone
 * might affect a layer surfaces position in the stack. Calling this function
 * invalidates the current ordering and makes sure the ordering is recalculated
 * on next access.
 */
void
phoc_output_set_layer_dirty (PhocOutput *self, enum zwlr_layer_shell_v1_layer layer)
{
  PhocOutputPrivate *priv;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  g_clear_pointer (&priv->layer_surfaces[layer], g_queue_free);
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
  struct wlr_box output_box;
  wlr_output_layout_get_box (self->desktop->layout, self->wlr_output, &output_box);

  if (wlr_box_empty (&output_box))
    return;

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    PhocDragIcon *drag_icon = seat->drag_icon;
    if (!phoc_drag_icon_is_mapped (drag_icon))
      continue;

    double ox = phoc_drag_icon_get_x (drag_icon) - output_box.x;
    double oy = phoc_drag_icon_get_y (drag_icon) - output_box.y;
    phoc_output_surface_for_each_surface (self, phoc_drag_icon_get_wlr_surface (drag_icon),
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
static void
phoc_output_for_each_surface (PhocOutput          *self,
                              PhocSurfaceIterator  iterator,
                              void                *user_data,
                              gboolean             visible_only)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocDesktop *desktop = self->desktop;

  if (self->fullscreen_view != NULL) {
    PhocView *view = self->fullscreen_view;

    phoc_output_view_for_each_surface (self, view, iterator, user_data);

#ifdef PHOC_XWAYLAND
    if (PHOC_IS_XWAYLAND_SURFACE (view)) {
      struct wlr_xwayland_surface *xsurface =
        phoc_xwayland_surface_get_wlr_surface (PHOC_XWAYLAND_SURFACE (view));
      phoc_output_xwayland_children_for_each_surface (self, xsurface, iterator, user_data);
    }
#endif
  } else {
    for (GList *l = phoc_desktop_get_views (desktop)->tail; l; l = l->prev) {
      PhocView *view = PHOC_VIEW (l->data);

      if (!visible_only || phoc_desktop_view_is_visible (desktop, view))
        phoc_output_view_for_each_surface (self, view, iterator, user_data);
    }
  }

  phoc_output_drag_icons_for_each_surface (self, input, iterator, user_data);

  for (enum zwlr_layer_shell_v1_layer layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
       layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer++) {
    phoc_output_layer_for_each_surface (self, layer, iterator, user_data);
  }
}


void
phoc_output_damage_whole (PhocOutput *self)
{
  if (self == NULL || self->wlr_output == NULL)
    return;

  wlr_damage_ring_add_whole (&self->damage_ring);
  wlr_output_schedule_frame (self->wlr_output);
}


static bool
phoc_view_accept_damage (PhocOutput *self, PhocView  *view)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());

  if (!phoc_desktop_view_is_visible (desktop, view))
    return false;

  if (self->fullscreen_view == NULL)
    return true;

  if (self->fullscreen_view == view)
    return true;

#ifdef PHOC_XWAYLAND
  if (PHOC_IS_XWAYLAND_SURFACE (self->fullscreen_view) && PHOC_IS_XWAYLAND_SURFACE (view)) {
    /* Special case: accept damage from children */
    struct wlr_xwayland_surface *xsurface =
      phoc_xwayland_surface_get_wlr_surface (PHOC_XWAYLAND_SURFACE (view));
    struct wlr_xwayland_surface *fullscreen_xsurface =
      phoc_xwayland_surface_get_wlr_surface (PHOC_XWAYLAND_SURFACE (self->fullscreen_view));
    while (xsurface != NULL) {
      if (fullscreen_xsurface == xsurface)
        return true;

      xsurface = xsurface->parent;
    }
  }
#endif
  return false;
}

static void
damage_surface_iterator (PhocOutput *self, struct wlr_surface *wlr_surface, struct wlr_box *_box,
                         float scale, void *data)
{
  bool *whole = data;

  struct wlr_box box = *_box;

  phoc_utils_scale_box (&box, scale);
  phoc_utils_scale_box (&box, self->wlr_output->scale);

  pixman_region32_t damage;
  pixman_region32_init (&damage);
  wlr_surface_get_effective_damage (wlr_surface, &damage);
  wlr_region_scale (&damage, &damage, scale);
  wlr_region_scale (&damage, &damage, self->wlr_output->scale);
  if (ceil (self->wlr_output->scale) > wlr_surface->current.scale) {
    /* When scaling up a surface, it'll become blurry so we need to
     * expand the damage region */
    wlr_region_expand (&damage, &damage, ceil (self->wlr_output->scale) - wlr_surface->current.scale);
  }

  pixman_region32_translate (&damage, box.x, box.y);
  if (wlr_damage_ring_add (&self->damage_ring, &damage))
    wlr_output_schedule_frame (self->wlr_output);
  pixman_region32_fini (&damage);

  if (*whole) {
    if (wlr_damage_ring_add_box (&self->damage_ring, &box))
      wlr_output_schedule_frame (self->wlr_output);
  }

  if (!wl_list_empty (&wlr_surface->current.frame_callback_list))
    wlr_output_schedule_frame (self->wlr_output);
}


static void
damage_whole_view (PhocOutput *self, PhocView  *view)
{
  GSList *blings;
  struct wlr_box box;

  if (!phoc_view_is_mapped (view)) {
    return;
  }

  blings = phoc_view_get_blings (view);
  if (G_LIKELY (!blings))
    return;

  for (GSList *l = blings; l; l = l->next) {
    PhocBling *bling = PHOC_BLING (l->data);

    box = phoc_bling_get_box (bling);
    box.x -= self->lx;
    box.y -= self->ly;
    phoc_utils_scale_box (&box, self->wlr_output->scale);

    if (wlr_damage_ring_add_box (&self->damage_ring, &box))
      wlr_output_schedule_frame (self->wlr_output);
  }
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

  phoc_output_surface_for_each_surface (self,
                                        phoc_drag_icon_get_wlr_surface (icon),
                                        phoc_drag_icon_get_x (icon),
                                        phoc_drag_icon_get_y (icon),
                                        damage_surface_iterator, &whole);
}

void
phoc_output_damage_whole_surface (PhocOutput         *self,
                                  struct wlr_surface *surface,
                                  double              ox,
                                  double              oy)
{
  bool whole = true;

  phoc_output_surface_for_each_surface (self, surface, ox, oy,
                                        damage_surface_iterator, &whole);
}

void
phoc_output_damage_from_surface (PhocOutput         *self,
                                 struct wlr_surface *wlr_surface,
                                 double              ox,
                                 double              oy)
{
  bool whole = false;

  phoc_output_surface_for_each_surface (self, wlr_surface, ox, oy,
                                        damage_surface_iterator, &whole);
}


static void
output_manager_apply_config (PhocDesktop                        *desktop,
                             struct wlr_output_configuration_v1 *config,
                             gboolean                            test_only)

{
  struct wlr_output_configuration_head_v1 *config_head;
  gboolean ok = TRUE;

  /* First disable outputs we need to disable */
  wl_list_for_each (config_head, &config->heads, link) {
    struct wlr_output *wlr_output = config_head->state.output;
    struct wlr_output_state pending;

    wlr_output_state_init (&pending);
    if (config_head->state.enabled)
      continue;

    if (!wlr_output->enabled)
      continue;

    wlr_output_state_set_enabled (&pending, false);
    if (test_only) {
      ok &= wlr_output_test_state (wlr_output, &pending);
    } else {
      wlr_output_layout_remove (desktop->layout, wlr_output);
      ok &= wlr_output_commit_state (wlr_output, &pending);
    }
    wlr_output_state_finish (&pending);
  }

  /* Then enable outputs that need to */
  wl_list_for_each (config_head, &config->heads, link) {
    struct wlr_output *wlr_output = config_head->state.output;
    PhocOutput *output = PHOC_OUTPUT (wlr_output->data);
    struct wlr_output_state pending;
    struct wlr_box output_box;

    if (!config_head->state.enabled)
      continue;

    wlr_output_state_init (&pending);
    wlr_output_state_set_enabled (&pending, true);
    if (config_head->state.mode != NULL) {
      wlr_output_state_set_mode (&pending, config_head->state.mode);
    } else {
      wlr_output_state_set_custom_mode (&pending,
                                        config_head->state.custom_mode.width,
                                        config_head->state.custom_mode.height,
                                        config_head->state.custom_mode.refresh);
    }
    wlr_output_state_set_transform (&pending, config_head->state.transform);
    wlr_output_state_set_scale (&pending, adjust_frac_scale (config_head->state.scale));

    if (test_only) {
      ok &= wlr_output_test_state (wlr_output, &pending);
    } else {
      wlr_output_layout_add (desktop->layout,
                             wlr_output,
                             config_head->state.x,
                             config_head->state.y);
      ok &= wlr_output_commit_state (wlr_output, &pending);

      if (output->fullscreen_view)
        phoc_view_set_fullscreen (output->fullscreen_view, true, output);

      wlr_output_layout_get_box (output->desktop->layout, output->wlr_output, &output_box);
      output->lx = output_box.x;
      output->ly = output_box.y;
    }

    wlr_output_state_finish (&pending);
  }

  if (ok)
    wlr_output_configuration_v1_send_succeeded (config);
  else
    wlr_output_configuration_v1_send_failed (config);

  wlr_output_configuration_v1_destroy (config);

  if (!test_only)
    update_output_manager_config (desktop);
}


void
phoc_handle_output_manager_apply (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = wl_container_of (listener, desktop, output_manager_apply);
  struct wlr_output_configuration_v1 *config = data;

  output_manager_apply_config (desktop, config, FALSE);
}


void
phoc_handle_output_manager_test (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = wl_container_of (listener, desktop, output_manager_apply);
  struct wlr_output_configuration_v1 *config = data;

  output_manager_apply_config (desktop, config, TRUE);
}


void
phoc_output_handle_output_power_manager_set_mode (struct wl_listener *listener, void *data)
{
  struct wlr_output_power_v1_set_mode_event *event = data;
  struct wlr_output_state pending;
  PhocOutput *self;
  bool enable = true;
  bool current;

  g_return_if_fail (event && event->output && event->output->data);

  self = event->output->data;
  g_debug ("Request to set output power mode of %p to %d", self->wlr_output->name, event->mode);
  switch (event->mode) {
  case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
    enable = false;
    break;
  case ZWLR_OUTPUT_POWER_V1_MODE_ON:
    enable = true;
    break;
  default:
    g_warning ("Unhandled power state %d for %p", event->mode, self->wlr_output->name);
    return;
  }

  current = self->wlr_output->enabled;
  if (enable == current)
    return;

  wlr_output_state_init (&pending);
  wlr_output_state_set_enabled (&pending, enable);

  if (!wlr_output_commit_state (self->wlr_output, &pending)) {
    g_warning ("Failed to commit power mode change to %d for %p", enable, self);
    wlr_output_state_finish (&pending);
    return;
  }

  wlr_output_state_finish (&pending);
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
  else if (g_str_has_prefix (output->wlr_output->name, "DPI-"))
    return TRUE;
  /* HWCOMPOSER-1 is guaranteed to be the primary display */
  else if (g_strcmp0 (output->wlr_output->name, "HWCOMPOSER-1") == 0)
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

  return phoc_view_is_mapped (self->fullscreen_view);
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
phoc_output_remove_frame_callbacks_by_animatable (PhocOutput *self, PhocAnimatable *animatable)
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

/**
 * phoc_output_has_frame_callbacks:
 * @self: The output to look at
 *
 * Returns: `true` if the output has any frame callbacks attached
 */
bool
phoc_output_has_frame_callbacks (PhocOutput *self)
{
  PhocOutputPrivate *priv;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  return !!priv->frame_callbacks;
}

/**
 * phoc_output_lower_shield:
 * @self: The output to lower the shield for
 *
 * Lowers an output shield that is in place to hide
 * the outputs current content.
 */
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

/**
 * phoc_output_raise_shield:
 * @self: The output to raise the shield for
 *
 * Raise an output shield will be put in place to hide the outputs
 * current content.
 */
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
  PhocServer *server = phoc_server_get_default();
  PhocDesktop *desktop = phoc_server_get_desktop (server);
  PhocInput *input = phoc_server_get_input (server);
  PhocOutputPrivate *priv;
  PhocLayerSurface *layer_surface;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);
    /* is our layer-surface focused on some seat? */
    if (seat->focused_layer && seat->focused_layer->output == self->wlr_output) {
      return true;
    }

    /* is OSK displayed because of our fullscreen view? */
    if (phoc_view_is_mapped (self->fullscreen_view) &&
        phoc_input_method_relay_is_enabled (&seat->im_relay, self->fullscreen_view->wlr_surface)) {
      PhocLayerSurface *osk = phoc_layer_shell_find_osk (self);
      if (osk && phoc_layer_surface_get_mapped (osk))
        return true;
    }
  }

  /* is some draggable surface unfolded, being dragged or animated? */
  wl_list_for_each (layer_surface, &self->layer_surfaces, link) {
    PhocDraggableLayerSurface *draggable;

    draggable = phoc_desktop_get_draggable_layer_surface (desktop, layer_surface);
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

  if (self == NULL)
    return;

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
  PhocServer *server = phoc_server_get_default ();
  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  if (G_UNLIKELY (phoc_server_check_debug_flags (server,
                                                 PHOC_SERVER_DEBUG_FLAG_FORCE_SHELL_REVEAL))) {
      force = TRUE;
  }

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


const char *
phoc_output_get_name (PhocOutput *self)
{
  g_assert (PHOC_IS_OUTPUT (self));
  g_assert (self->wlr_output);

  return self->wlr_output->name;
}


void
phoc_output_handle_gamma_control_set_gamma (struct wl_listener *listener, void *data)
{
  const struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;
  PhocOutput *self = PHOC_OUTPUT (event->output->data);
  PhocOutputPrivate *priv = phoc_output_get_instance_private(self);

  if (!self)
    return;

  priv->gamma_lut_changed = TRUE;
  wlr_output_schedule_frame (self->wlr_output);
}


/**
 * phoc_output_transform_damage:
 * @self: The output to transform for
 * @damage:(inout): The damaged area
 *
 * Transforms the given damage region according to the output's transform.
 */
void
phoc_output_transform_damage (PhocOutput *self, pixman_region32_t *damage)
{
  int width, height;
  enum wl_output_transform transform;

  g_assert (PHOC_IS_OUTPUT (self));

  wlr_output_transformed_resolution (self->wlr_output, &width, &height);
  transform = wlr_output_transform_invert (self->wlr_output->transform);
  wlr_region_transform (damage, damage, transform, width, height);
}

/**
 * phoc_output_transform_box:
 * @self: The output to transform for
 * @box:(inout): The box to transform
 *
 * Transforms the given box according to the output's transform.
 */
void
phoc_output_transform_box (PhocOutput *self, struct wlr_box *box)
{
  int ow, oh;
  enum wl_output_transform transform;

  g_assert (PHOC_IS_OUTPUT (self));

  wlr_output_transformed_resolution (self->wlr_output, &ow, &oh);
  transform = wlr_output_transform_invert (self->wlr_output->transform);
  wlr_box_transform (box, box, transform, ow, oh);
}


enum wlr_scale_filter_mode
phoc_output_get_texture_filter_mode (PhocOutput *self)
{
  PhocOutputPrivate *priv;

  g_assert (PHOC_IS_OUTPUT (self));
  priv = phoc_output_get_instance_private (self);

  switch (priv->scale_filter) {
  case PHOC_OUTPUT_SCALE_FILTER_BILINEAR:
    return WLR_SCALE_FILTER_BILINEAR;
  case PHOC_OUTPUT_SCALE_FILTER_NEAREST:
    return WLR_SCALE_FILTER_NEAREST;
  case PHOC_OUTPUT_SCALE_FILTER_AUTO:
    break;
  default:
    g_assert_not_reached ();
  }

  if (ceilf(self->wlr_output->scale) == self->wlr_output->scale)
    return WLR_SCALE_FILTER_NEAREST;

  return WLR_SCALE_FILTER_BILINEAR;
}


struct wlr_output *
phoc_output_get_wlr_output (PhocOutput *self)
{
  g_assert (PHOC_IS_OUTPUT (self));

  return self->wlr_output;
}
