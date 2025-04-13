/*
 * Copyright (C) 2021 Purism SPC
 * Copyright (C) 2023-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: The wlroots authors
 *          Sebastian Krzyszkowiak
 *          Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-render"

#include "phoc-config.h"
#include "bling.h"
#include "cursor.h"
#include "input.h"
#include "layer-shell.h"
#include "seat.h"
#include "server.h"
#include "render.h"
#include "render-private.h"
#include "xwayland-surface.h"
#include "utils.h"

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/config.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/gles2.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include <wlr/render/allocator.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define COLOR_BLACK                ((struct wlr_render_color){0.0f, 0.0f, 0.0f, 1.0f})
#define COLOR_MAGENTA_ALPHA(x)     ((struct wlr_render_color){0.5f, 0.0f, 0.5f, (x)})

/**
 * PhocRenderer:
 *
 * The renderer
 */

enum {
  RENDER_END,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

enum {
  PROP_0,
  PROP_WLR_BACKEND,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocRenderer {
  GObject               parent;

  struct wlr_backend   *wlr_backend;
  struct wlr_renderer  *wlr_renderer;
  struct wlr_allocator *wlr_allocator;
};

static void phoc_renderer_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocRenderer, phoc_renderer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, phoc_renderer_initable_iface_init));


struct view_render_data {
  PhocView *view;
  int width;
  int height;
};


static void
phoc_renderer_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PhocRenderer *self = PHOC_RENDERER (object);

  switch (property_id) {
  case PROP_WLR_BACKEND:
    self->wlr_backend = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_renderer_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PhocRenderer *self = PHOC_RENDERER (object);

  switch (property_id) {
  case PROP_WLR_BACKEND:
    g_value_set_pointer (value, self->wlr_backend);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
render_texture (PhocOutput               *output,
                struct wlr_texture       *texture,
                const struct wlr_fbox    *src_box,
                const struct wlr_box     *dst_box,
                const struct wlr_box     *clip_box,
                enum wl_output_transform  surface_transform,
                float                     alpha,
                PhocRenderContext        *ctx)
{
  pixman_region32_t damage;
  struct wlr_box proj_box = *dst_box;
  enum wl_output_transform transform;

  if (alpha == 0.0)
    return;

  if (!phoc_utils_is_damaged (&proj_box, ctx->damage, clip_box, &damage))
    goto buffer_damage_finish;

  phoc_output_transform_box (output, &proj_box);
  phoc_output_transform_damage (output, &damage);
  transform = wlr_output_transform_compose (surface_transform, output->wlr_output->transform);

  wlr_render_pass_add_texture (ctx->render_pass, &(struct wlr_render_texture_options) {
      .texture = texture,
      .src_box = *src_box,
      .dst_box = proj_box,
      .transform = transform,
      .alpha = &alpha,
      .clip = &damage,
      .filter_mode = phoc_output_get_texture_filter_mode (ctx->output),
    });

 buffer_damage_finish:
  pixman_region32_fini (&damage);
}


static void
render_surface_iterator (PhocOutput         *output,
                         struct wlr_surface *surface,
                         struct wlr_box     *box,
                         float               scale,
                         void               *data)
{
  PhocRenderContext *ctx = data;
  struct wlr_output *wlr_output = output->wlr_output;
  float alpha = ctx->alpha;
  const struct wlr_alpha_modifier_surface_v1_state *alpha_modifier_state;

  struct wlr_texture *texture = wlr_surface_get_texture (surface);
  if (!texture)
    return;

  struct wlr_fbox src_box;
  wlr_surface_get_buffer_source_box (surface, &src_box);

  struct wlr_box dst_box = *box;
  struct wlr_box clip_box = *box;

  phoc_utils_scale_box (&dst_box, scale);
  phoc_utils_scale_box (&dst_box, wlr_output->scale);
  phoc_utils_scale_box (&clip_box, scale);
  phoc_utils_scale_box (&clip_box, wlr_output->scale);

  alpha_modifier_state = wlr_alpha_modifier_v1_get_surface_state (surface);
  if (alpha_modifier_state)
    alpha *= (float)alpha_modifier_state->multiplier;

  render_texture (output,
                  texture,
                  &src_box,
                  &dst_box,
                  &clip_box,
                  surface->current.transform,
                  alpha,
                  ctx);

  wlr_presentation_surface_scanned_out_on_output (surface, wlr_output);
}


static void
render_blings (PhocOutput *output, PhocView *view, PhocRenderContext *ctx)
{
  GSList *blings;

  if (!phoc_view_is_mapped (view))
    return;

  blings = phoc_view_get_blings (view);
  if (!blings)
    return;

  for (GSList *l = blings; l; l = l->next) {
    PhocBling *bling = PHOC_BLING (l->data);

    phoc_bling_render (bling, ctx);
  }
}


static void
render_view (PhocOutput *output, PhocView *view, PhocRenderContext *ctx)
{
  /*  Do not render views fullscreened on other outputs */
  if (phoc_view_is_fullscreen (view) && phoc_view_get_fullscreen_output (view) != output)
    return;

  ctx->alpha = phoc_view_get_alpha (view);

  if (!phoc_view_is_fullscreen (view))
    render_blings (output, view, ctx);

  phoc_output_view_for_each_surface (output, view, render_surface_iterator, ctx);
}


static void
render_layer (enum zwlr_layer_shell_v1_layer layer, PhocRenderContext *ctx)
{
  GQueue *layer_surfaces = phoc_output_get_layer_surfaces_for_layer (ctx->output, layer);

  for (GList *l = layer_surfaces->head; l; l = l->next) {
    PhocLayerSurface *layer_surface = PHOC_LAYER_SURFACE (l->data);

    ctx->alpha = phoc_layer_surface_get_alpha (layer_surface);
    phoc_output_layer_surface_for_each_surface (ctx->output,
                                                layer_surface,
                                                render_surface_iterator,
                                                ctx);
  }
}


static void
render_drag_icons (PhocInput *input, PhocRenderContext *ctx)
{
  ctx->alpha = 1.0;

  phoc_output_drag_icons_for_each_surface (ctx->output, input, render_surface_iterator, ctx);
}


static void
render_touch_point_cb (gpointer key, gpointer value, gpointer user_data)
{
  PhocTouchPoint *touch_point = value;
  PhocRenderContext *ctx = user_data;

  phoc_touch_point_render (touch_point, ctx);
}


static void
render_touch_points (PhocRenderContext *ctx)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());

  for (GSList *l = phoc_input_get_seats (input); l; l = l->next) {
    PhocSeat *seat = PHOC_SEAT (l->data);
    PhocCursor *cursor = phoc_seat_get_cursor (seat);

    g_hash_table_foreach (phoc_cursor_get_touch_points (cursor), render_touch_point_cb, ctx);
  }
}

static void
view_render_to_buffer_iterator (struct wlr_surface *surface, int sx, int sy, void *_data)
{
  if (!wlr_surface_has_buffer (surface)) {
    return;
  }

  PhocServer *server = phoc_server_get_default ();
  PhocRenderer *self = phoc_server_get_renderer (server);
  struct wlr_texture *texture = wlr_surface_get_texture (surface);

  struct view_render_data *data = _data;
  PhocView *view = data->view;

  struct wlr_box geo;
  phoc_view_get_geometry (view, &geo);

  float scale = fmin (data->width / (float)geo.width,
                      data->height / (float)geo.height);

  float proj[9];
  wlr_matrix_identity (proj);
  wlr_matrix_scale (proj, scale, scale);
  wlr_matrix_translate (proj, -geo.x, -geo.y);

  struct wlr_fbox src_box;
  wlr_surface_get_buffer_source_box (surface, &src_box);

  struct wlr_box dst_box = {
    .x = sx,
    .y = sy,
    .width = surface->current.width,
    .height = surface->current.height,
  };

  float mat[9];
  wlr_matrix_project_box (mat, &dst_box, wlr_output_transform_invert (surface->current.transform), 0, proj);
  wlr_render_subtexture_with_matrix (self->wlr_renderer, texture, &src_box, mat, 1.0);
}


/* FIXME: Rework when switching to wlroots 0.18.x git again */
gboolean
phoc_renderer_render_view_to_buffer (PhocRenderer      *self,
                                     PhocView          *view,
                                     struct wlr_buffer *shm_buffer)
{
  struct wlr_surface *surface = view->wlr_surface;
  struct wlr_buffer *buffer;
  void *data;
  uint32_t format;
  size_t stride;

  g_return_val_if_fail (surface, false);
  g_return_val_if_fail (self->wlr_allocator, false);
  g_return_val_if_fail (shm_buffer, false);

  int32_t width = shm_buffer->width;
  int32_t height = shm_buffer->height;

  struct wlr_drm_format_set fmt_set = {};
  wlr_drm_format_set_add (&fmt_set, DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_INVALID);

  const struct wlr_drm_format *fmt = wlr_drm_format_set_get (&fmt_set, DRM_FORMAT_ARGB8888);

  buffer = wlr_allocator_create_buffer (self->wlr_allocator, width, height, fmt);
  if (!buffer) {
    wlr_drm_format_set_finish (&fmt_set);
    g_warning ("Failed to allocate buffer");
    return false;
  }

  struct view_render_data render_data = {
    .view = view,
    .width = width,
    .height = height
  };

  wlr_renderer_begin_with_buffer (self->wlr_renderer, buffer);
  wlr_renderer_clear (self->wlr_renderer, (float[])COLOR_TRANSPARENT);
  wlr_surface_for_each_surface (surface, view_render_to_buffer_iterator, &render_data);

  if (!wlr_buffer_begin_data_ptr_access (shm_buffer,
                                         WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
                                         &data, &format, &stride)) {
    return false;
  }

  wlr_renderer_read_pixels (self->wlr_renderer,
                            DRM_FORMAT_ARGB8888, stride, width, height, 0, 0, 0, 0, data);
  wlr_renderer_end (self->wlr_renderer);

  wlr_buffer_drop (buffer);
  wlr_drm_format_set_finish (&fmt_set);

  wlr_buffer_end_data_ptr_access (shm_buffer);

  return true;
}

#define DEBUG_DAMAGE_TIMEOUT_US (250.0 * 1000.0)
#define DEBUG_DAMAGE_MAX_OPACITY 0.8

static void
render_damage (PhocRenderer *self, PhocRenderContext *ctx)
{
  gint64 now = g_get_monotonic_time ();

  for (GSList *l = phoc_output_get_debug_damage (ctx->output); l; l = l->next) {
    PhocDebugDamageRegion *damage = l->data;
    float elapsed = fmax (1.0 - (now - damage->when) / DEBUG_DAMAGE_TIMEOUT_US, 0.0);
    float alpha = DEBUG_DAMAGE_MAX_OPACITY * elapsed;
    struct pixman_region32 clip;

    pixman_region32_init (&clip);
    pixman_region32_copy (&clip, &damage->region);
    phoc_output_transform_damage (ctx->output, &clip);

    /* Using an empty box makes us clip the damage from the whole output buffer */
    wlr_render_pass_add_rect (ctx->render_pass, &(struct wlr_render_rect_options){
        .color = COLOR_MAGENTA_ALPHA (alpha),
        .clip = &clip,
      });

    if (G_APPROX_VALUE (elapsed, 0.0, FLT_EPSILON))
      damage->done = 1;

    pixman_region32_fini (&clip);
  }
}

/**
 * phoc_renderer_render_output:
 * @self: The renderer
 * @output: The output to render
 * @context: The render context provided by the output
 *
 * Render a given output.
 */
void
phoc_renderer_render_output (PhocRenderer *self, PhocOutput *output, PhocRenderContext *ctx)
{
  PhocServer *server = phoc_server_get_default ();
  struct wlr_output *wlr_output = output->wlr_output;
  PhocDesktop *desktop = PHOC_DESKTOP (output->desktop);
  pixman_region32_t *damage = ctx->damage;
  pixman_region32_t transformed_damage;

  g_assert (PHOC_IS_RENDERER (self));

  pixman_region32_init (&transformed_damage);

  if (!pixman_region32_not_empty (damage)) {
    /* Output isn't damaged but needs buffer swap */
    goto renderer_end;
  }

  pixman_region32_copy (&transformed_damage, damage);
  phoc_output_transform_damage (output, &transformed_damage);

  wlr_render_pass_add_rect (ctx->render_pass,
                            &(struct wlr_render_rect_options){
                              .box = { .width = wlr_output->width, .height = wlr_output->height },
                              .color = COLOR_BLACK,
                              .clip = &transformed_damage,
                            });

  /* If a view is fullscreen on this output, render it */
  if (output->fullscreen_view != NULL) {
    PhocView *view = output->fullscreen_view;

    render_view (output, view, ctx);

    /* During normal rendering the xwayland window tree isn't traversed
     * because all windows are rendered. Here we only want to render
     * the fullscreen window's children so we have to traverse the tree. */
#ifdef PHOC_XWAYLAND
    if (PHOC_IS_XWAYLAND_SURFACE (view)) {
      struct wlr_xwayland_surface *xsurface =
        phoc_xwayland_surface_get_wlr_surface (PHOC_XWAYLAND_SURFACE (view));
      phoc_output_xwayland_children_for_each_surface (output,
                                                      xsurface,
                                                      render_surface_iterator,
                                                      ctx);
    }
#endif

    if (phoc_output_has_shell_revealed (output)) {
      /* Render top layer above fullscreen view when requested */
      render_layer (ZWLR_LAYER_SHELL_V1_LAYER_TOP, ctx);
    }
  } else {
    /* Render background and bottom layers under views */
    render_layer (ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, ctx);
    render_layer (ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ctx);

    /* Render all views */
    for (GList *l = phoc_desktop_get_views (desktop)->tail; l; l = l->prev) {
      PhocView *view = PHOC_VIEW (l->data);

      if (phoc_desktop_view_check_visibility (desktop, view))
        render_view (output, view, ctx);
    }
    /* Render top layer above views */
    render_layer (ZWLR_LAYER_SHELL_V1_LAYER_TOP, ctx);
  }
  render_drag_icons (phoc_server_get_input (server), ctx);

  render_layer (ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ctx);

 renderer_end:
  pixman_region32_fini (&transformed_damage);
  wlr_output_add_software_cursors_to_render_pass (wlr_output, ctx->render_pass, damage);

  if (G_UNLIKELY (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS)))
    render_touch_points (ctx);

  g_signal_emit (self, signals[RENDER_END], 0, ctx);
  if (G_UNLIKELY (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_DAMAGE_TRACKING)))
    render_damage (self, ctx);
}


static gboolean
phoc_renderer_initable_init (GInitable    *initable,
                             GCancellable *cancellable,
                             GError      **error)
{
  PhocRenderer *self = PHOC_RENDERER (initable);

  self->wlr_renderer = wlr_renderer_autocreate (self->wlr_backend);
  if (self->wlr_renderer == NULL) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "Could not create renderer");
    return FALSE;
  }

  self->wlr_allocator = wlr_allocator_autocreate (self->wlr_backend,
                                                  self->wlr_renderer);
  if (self->wlr_allocator == NULL) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "Could not create allocator");
    return FALSE;
  }

  return TRUE;
}


static void
phoc_renderer_finalize (GObject *object)
{
  PhocRenderer *self = PHOC_RENDERER (object);

  g_clear_pointer (&self->wlr_allocator, wlr_allocator_destroy);
  g_clear_pointer (&self->wlr_renderer, wlr_renderer_destroy);

  G_OBJECT_CLASS (phoc_renderer_parent_class)->finalize (object);
}


static void
phoc_renderer_initable_iface_init (GInitableIface *iface)
{
  iface->init = phoc_renderer_initable_init;
}


static void
phoc_renderer_class_init (PhocRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_renderer_get_property;
  object_class->set_property = phoc_renderer_set_property;
  object_class->finalize = phoc_renderer_finalize;

  /**
   * PhocRenderer:wlr-backend
   *
   * The wlr-backend to use for initializing the renderer
   */
  props[PROP_WLR_BACKEND] =
    g_param_spec_pointer ("wlr-backend",
                          "",
                          "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhocRenderer::render-end
   * @self: The renderer emitting the signal
   * @output: The output being rendered on
   *
   * This signal is emitted at the end of a render pass
   */
  signals[RENDER_END] = g_signal_new ("render-end",
                                      G_TYPE_FROM_CLASS (klass),
                                      G_SIGNAL_RUN_LAST,
                                      0, NULL, NULL, NULL,
                                      G_TYPE_NONE, 1,
                                      /* PhocRenderContext: */
                                      G_TYPE_POINTER);
}


static void
phoc_renderer_init (PhocRenderer *self)
{
}


PhocRenderer *
phoc_renderer_new (struct wlr_backend *wlr_backend, GError **error)
{
  return PHOC_RENDERER (g_initable_new (PHOC_TYPE_RENDERER, NULL, error,
                                        "wlr-backend", wlr_backend,
                                        NULL));
}


struct wlr_renderer *
phoc_renderer_get_wlr_renderer (PhocRenderer *self)
{
  g_assert (PHOC_IS_RENDERER (self));

  return self->wlr_renderer;
}


struct wlr_allocator *
phoc_renderer_get_wlr_allocator (PhocRenderer *self)
{
  g_assert (PHOC_IS_RENDERER (self));

  return self->wlr_allocator;
}
