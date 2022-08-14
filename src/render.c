/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: The wlroots authors
 *          Sebastian Krzyszkowiak
 *          Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-render"

#include "config.h"
#include "layers.h"
#include "seat.h"
#include "server.h"
#include "render.h"
#include "xwayland-surface.h"

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/config.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/gles2.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/region.h>
#include <wlr/version.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR > 12
/* Private wlr_allocator headers */
struct wlr_gbm_allocator *wlr_gbm_allocator_create(int drm_fd);
struct wlr_buffer *wlr_allocator_create_buffer (struct wlr_allocator *alloc, int width, int height, const struct wlr_drm_format *format);
void wlr_allocator_destroy (struct wlr_allocator *alloc);
struct wlr_drm_format *wlr_drm_format_create (uint32_t format);
/* ----------------------------- */
#endif

#define TOUCH_POINT_SIZE 20
#define TOUCH_POINT_BORDER 0.1

#define COLOR_BLACK                {0.0f, 0.0f, 0.0f, 1.0f}
#define COLOR_TRANSPARENT          {0.0f, 0.0f, 0.0f, 0.0f}
#define COLOR_TRANSPARENT_WHITE    {0.5f, 0.5f, 0.5f, 0.5f}
#define COLOR_TRANSPARENT_YELLOW   {0.5f, 0.5f, 0.0f, 0.5f}
#define COLOR_TRANSPARENT_MAGENTA  {0.5f, 0.0f, 0.5f, 0.5f}


/**
 * PhocRenderer:
 *
 * The renderer
 */

enum {
  RENDER_START,
  RENDER_END,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

enum {
  PROP_0,
  PROP_WLR_RENDERER,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocRenderer {
  GObject               parent;

  struct wlr_renderer  *wlr_renderer;
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR > 12
  struct wlr_allocator *allocator;
#endif
};
G_DEFINE_TYPE (PhocRenderer, phoc_renderer, G_TYPE_OBJECT)


struct render_data {
  pixman_region32_t *damage;
  float alpha;
};

struct view_render_data {
  PhocView *view;
  int width;
  int height;
};

struct touch_point_data {
  int id;
  double x;
  double y;
};


static void
phoc_renderer_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PhocRenderer *self = PHOC_RENDERER (object);

  switch (property_id) {
  case PROP_WLR_RENDERER:
    self->wlr_renderer = g_value_get_pointer (value);
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
  case PROP_WLR_RENDERER:
    g_value_set_pointer (value, self->wlr_renderer);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void scissor_output(struct wlr_output *wlr_output,
		pixman_box32_t *rect) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	struct wlr_box box = {
		.x = rect->x1,
		.y = rect->y1,
		.width = rect->x2 - rect->x1,
		.height = rect->y2 - rect->y1,
	};

	int ow, oh;
	wlr_output_transformed_resolution(wlr_output, &ow, &oh);

	enum wl_output_transform transform =
		wlr_output_transform_invert(wlr_output->transform);
	wlr_box_transform(&box, &box, transform, ow, oh);

	wlr_renderer_scissor(renderer, &box);
}

static void render_texture(struct wlr_output *wlr_output,
		pixman_region32_t *output_damage, struct wlr_texture *texture,
		const struct wlr_fbox *src_box, const struct wlr_box *dst_box,
		const float matrix[static 9],
		float rotation, float alpha) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	struct wlr_box rotated;
	wlr_box_rotated_bounds(&rotated, dst_box, rotation);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, rotated.x, rotated.y,
		rotated.width, rotated.height);
	pixman_region32_intersect(&damage, &damage, output_damage);
	bool damaged = pixman_region32_not_empty(&damage);
	if (!damaged) {
		goto buffer_damage_finish;
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(wlr_output, &rects[i]);

		if (src_box != NULL) {
			wlr_render_subtexture_with_matrix(renderer, texture, src_box, matrix, alpha);
		} else {
			wlr_render_texture_with_matrix(renderer, texture, matrix, alpha);
		}
	}

buffer_damage_finish:
	pixman_region32_fini(&damage);
}

static void
collect_touch_points (PhocOutput *output, struct wlr_surface *surface, struct wlr_box box, float scale)
{
  PhocServer *server = phoc_server_get_default ();
  if (G_LIKELY (!(server->debug_flags & PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS))) {
    return;
  }

  for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    struct wlr_touch_point *point;
    wl_list_for_each(point, &seat->seat->touch_state.touch_points, link) {
      if (point->surface != surface) { continue; }
      struct touch_point_data *touch_point = g_malloc(sizeof(struct touch_point_data));
      touch_point->id = point->touch_id;
      touch_point->x = box.x + point->sx * output->wlr_output->scale * scale;
      touch_point->y = box.y + point->sy * output->wlr_output->scale * scale;
      output->debug_touch_points = g_list_append(output->debug_touch_points, touch_point);
    }
  }
}

static void render_surface_iterator(PhocOutput *output,
		struct wlr_surface *surface, struct wlr_box *_box, float rotation,
		float scale, void *_data) {
	struct render_data *data = _data;
	struct wlr_output *wlr_output = output->wlr_output;
	pixman_region32_t *output_damage = data->damage;
	float alpha = data->alpha;

	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (!texture) {
		return;
	}

	struct wlr_fbox src_box;
	wlr_surface_get_buffer_source_box(surface, &src_box);

	struct wlr_box dst_box = *_box;
	phoc_output_scale_box(wlr_output->data, &dst_box, scale);
	phoc_output_scale_box(wlr_output->data, &dst_box, wlr_output->scale);

	float matrix[9];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &dst_box, transform, rotation,
		wlr_output->transform_matrix);

	render_texture(wlr_output, output_damage,
		texture, &src_box, &dst_box, matrix, rotation, alpha);

	wlr_presentation_surface_sampled_on_output(output->desktop->presentation,
		surface, wlr_output);

	collect_touch_points(output, surface, dst_box, scale);
}

static void render_decorations(PhocOutput *output,
		PhocView *view, struct render_data *data) {
	if (!view->decorated || !phoc_view_is_mapped (view)) {
		return;
	}

	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(output->wlr_output->backend);
	assert(renderer);

	struct wlr_box box;
	phoc_output_get_decoration_box(output, view, &box);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, box.x, box.y,
		box.width, box.height);
	pixman_region32_intersect(&damage, &damage, data->damage);
	bool damaged = pixman_region32_not_empty(&damage);
	if (!damaged) {
		goto buffer_damage_finish;
	}

	float matrix[9];
	wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL,
		0, output->wlr_output->transform_matrix);
	float color[] = { 0.2, 0.2, 0.2, view->alpha };

	int nrects;
	pixman_box32_t *rects =
		pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output->wlr_output, &rects[i]);
		wlr_render_quad_with_matrix(renderer, color, matrix);
	}

buffer_damage_finish:
	pixman_region32_fini(&damage);
}

static void render_view(PhocOutput *output, PhocView *view,
		struct render_data *data) {
	// Do not render views fullscreened on other outputs
	if (view_is_fullscreen (view) && view->fullscreen_output != output) {
		return;
	}

	data->alpha = view->alpha;
	if (!view_is_fullscreen (view)) {
		render_decorations(output, view, data);
	}
	phoc_output_view_for_each_surface(output, view, render_surface_iterator, data);
}

static void render_layer(PhocOutput *output,
		pixman_region32_t *damage, struct wl_list *layer_surfaces) {
	struct render_data data = {
		.damage = damage,
		.alpha = 1.0f,
	};
	phoc_output_layer_for_each_surface(output, layer_surfaces,
		render_surface_iterator, &data);
}

static void count_surface_iterator(PhocOutput *output,
		struct wlr_surface *surface, struct wlr_box *_box, float rotation,
		float scale, void *data) {
	size_t *n = data;
	n++;
}

static bool scan_out_fullscreen_view(PhocOutput *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	PhocServer *server = phoc_server_get_default ();

	for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
		PhocSeat *seat = PHOC_SEAT (elem->data);

		g_assert (PHOC_IS_SEAT (seat));
		PhocDragIcon *drag_icon = seat->drag_icon;
		if (drag_icon && drag_icon->wlr_drag_icon->mapped) {
			return false;
		}
	}

	if (!wl_list_empty(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY])) {
		return false;
	}

	struct wlr_output_cursor *cursor;
	wl_list_for_each(cursor, &wlr_output->cursors, link) {
		if (cursor->enabled && cursor->visible &&
				wlr_output->hardware_cursor != cursor) {
			return false;
		}
	}

	PhocView *view = output->fullscreen_view;
	assert(view != NULL);
	if (!phoc_view_is_mapped (view)) {
		return false;
	}
	size_t n_surfaces = 0;
	phoc_output_view_for_each_surface(output, view,
		count_surface_iterator, &n_surfaces);
	if (n_surfaces > 1) {
		return false;
	}

#if WLR_HAS_XWAYLAND
	if (view->type == PHOC_XWAYLAND_VIEW) {
		PhocXWaylandSurface *xwayland_surface =
			phoc_xwayland_surface_from_view(view);
		if (!wl_list_empty(&xwayland_surface->xwayland_surface->children)) {
			return false;
		}
	}
#endif

	struct wlr_surface *surface = view->wlr_surface;

	if (surface->buffer == NULL) {
		return false;
	}

	if ((float)surface->current.scale != wlr_output->scale ||
			surface->current.transform != wlr_output->transform) {
		return false;
	}

	wlr_output_attach_buffer(wlr_output, &surface->buffer->base);
	if (!wlr_output_test(wlr_output)) {
		return false;
	}

	wlr_presentation_surface_sampled_on_output(output->desktop->presentation, surface, output->wlr_output);

	return wlr_output_commit(wlr_output);
}

static void render_drag_icons(PhocOutput *output,
		pixman_region32_t *damage, PhocInput *input) {
	struct render_data data = {
		.damage = damage,
		.alpha = 1.0f,
	};
	phoc_output_drag_icons_for_each_surface(output, input,
		render_surface_iterator, &data);
}

static void
color_hsv_to_rgb (float* color)
{
  float h = color[0], s = color[1], v = color[2];

  h = fmodf (h, 360);
  if (h < 0) {
    h += 360;
  }
  int d = h / 60;
  float e = h / 60 - d;
  float a = v * (1 - s);
  float b = v * (1 - e * s);
  float c = v * (1 - (1 - e) * s);
  switch (d) {
    default:
    case 0: color[0] = v, color[1] = c, color[2] = a; return;
    case 1: color[0] = b, color[1] = v, color[2] = a; return;
    case 2: color[0] = a, color[1] = v, color[2] = c; return;
    case 3: color[0] = a, color[1] = b, color[2] = v; return;
    case 4: color[0] = c, color[1] = a, color[2] = v; return;
    case 5: color[0] = v, color[1] = a, color[2] = b; return;
  }
}

static struct wlr_box
wlr_box_from_touch_point (struct touch_point_data *touch_point, int width, int height)
{
  return (struct wlr_box) {
    .x = touch_point->x - width / 2.0,
    .y = touch_point->y - height / 2.0,
    .width = width,
    .height = height
  };
}

static void
render_touch_point_cb (gpointer data, gpointer user_data)
{
  struct touch_point_data *touch_point = data;

  PhocOutput *output = user_data;
  struct wlr_output *wlr_output = output->wlr_output;

  int size = TOUCH_POINT_SIZE * wlr_output->scale;
  struct wlr_box point_box = wlr_box_from_touch_point (touch_point, size, size);

  struct wlr_renderer *renderer = wlr_backend_get_renderer (wlr_output->backend);

  float color[4] = {touch_point->id * 100 + 240, 1.0, 1.0, 0.75};
  color_hsv_to_rgb (color);
  wlr_render_rect (renderer, &point_box, color, wlr_output->transform_matrix);

  size = TOUCH_POINT_SIZE * (1.0 - TOUCH_POINT_BORDER) * wlr_output->scale;
  point_box = wlr_box_from_touch_point (touch_point, size, size);
  wlr_render_rect (renderer, &point_box, (float[])COLOR_TRANSPARENT_WHITE, wlr_output->transform_matrix);

  point_box = wlr_box_from_touch_point (touch_point, 8 * wlr_output->scale, 2 * wlr_output->scale);
  wlr_render_rect (renderer, &point_box, color, wlr_output->transform_matrix);
  point_box = wlr_box_from_touch_point (touch_point, 2 * wlr_output->scale, 8 * wlr_output->scale);
  wlr_render_rect (renderer, &point_box, color, wlr_output->transform_matrix);
}

static void
render_touch_points (PhocOutput *output)
{
  PhocServer *server = phoc_server_get_default ();
  if (G_LIKELY (!(server->debug_flags & PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS))) {
    return;
  }
  g_list_foreach (output->debug_touch_points, render_touch_point_cb, output);
}

static void
damage_touch_point_cb (gpointer data, gpointer user_data)
{
  struct touch_point_data *touch_point = data;

  PhocOutput *output = user_data;
  struct wlr_output *wlr_output = output->wlr_output;

  int size = TOUCH_POINT_SIZE * wlr_output->scale;
  struct wlr_box box = wlr_box_from_touch_point (touch_point, size, size);
  pixman_region32_t region;
  pixman_region32_init_rect(&region, box.x, box.y, box.width, box.height);
  wlr_output_damage_add(output->damage, &region);
  pixman_region32_fini(&region);
}

static void
damage_touch_points (PhocOutput *output)
{
  if (output->debug_touch_points == NULL)
    return;

  g_list_foreach (output->debug_touch_points, damage_touch_point_cb, output);
  wlr_output_schedule_frame(output->wlr_output);
}

static void
view_render_iterator (struct wlr_surface *surface, int sx, int sy, void *_data)
{
  if (!wlr_surface_has_buffer (surface)) {
    return;
  }

  PhocServer *server = phoc_server_get_default ();
  PhocRenderer *self = phoc_server_get_renderer (server);
  struct wlr_texture *view_texture = wlr_surface_get_texture (surface);

  struct view_render_data *data = _data;
  PhocView *view = data->view;
  struct wlr_surface *root = view->wlr_surface;

  struct wlr_box box;
  view_get_box (view, &box);

  struct wlr_box geo;
  view_get_geometry (view, &geo);

  float mat[16];
  wlr_matrix_identity (mat);

#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR > 12
  wlr_matrix_scale (mat, data->width / (float)geo.width, data->height / (float)geo.height);
  wlr_matrix_scale (mat, 1 / (float)root->current.scale, 1 / (float)root->current.scale);
  wlr_matrix_scale (mat, view->scale, view->scale);
  wlr_matrix_scale (mat, root->current.scale / surface->current.scale, root->current.scale / surface->current.scale);
  wlr_matrix_translate (mat, -geo.x, -geo.y);
#else
  // NDC
  wlr_matrix_translate (mat, -1, -1);
  wlr_matrix_scale (mat, 2, 2);

  wlr_matrix_scale (mat, 1 / (float)box.width, 1 / (float)box.height);
  wlr_matrix_translate (mat, -geo.x, -geo.y);
  wlr_matrix_scale (mat, 1 / (float)root->current.scale, 1 / (float)root->current.scale);
  wlr_matrix_scale (mat, view->scale, view->scale);
  wlr_matrix_scale (mat, root->current.scale / surface->current.scale, root->current.scale / surface->current.scale);
#endif

  wlr_render_texture (self->wlr_renderer,
                      view_texture, mat, sx * surface->current.scale,
                      sy * surface->current.scale,
                      1.0);
}

gboolean
phoc_renderer_render_view_to_buffer (PhocRenderer *self,
                                     PhocView     *view,
                                     enum wl_shm_format shm_fmt,
                                     int           width,
                                     int           height,
                                     int           stride,
                                     uint32_t     *flags,
                                     void         *data)
{
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR > 12
  struct wlr_surface *surface = view->wlr_surface;

  g_return_val_if_fail (surface, false);
  g_return_val_if_fail (self->allocator, false);

  struct wlr_drm_format *fmt = wlr_drm_format_create (DRM_FORMAT_ARGB8888);
  struct wlr_buffer *buffer = wlr_allocator_create_buffer (self->allocator, width, height, fmt);

  if (!buffer) {
    free (fmt);
    g_return_val_if_reached (false);
  }

  struct view_render_data render_data ={
    .view = view,
    .width = width,
    .height = height
  };

  wlr_renderer_begin_with_buffer (self->wlr_renderer, buffer);
  wlr_renderer_clear (self->wlr_renderer, (float[])COLOR_TRANSPARENT);
  wlr_surface_for_each_surface (surface, view_render_iterator, &render_data);
  wlr_renderer_read_pixels (self->wlr_renderer, DRM_FORMAT_ARGB8888, NULL, stride, width, height, 0, 0, 0, 0, data);
  wlr_renderer_end (self->wlr_renderer);

  wlr_buffer_drop (buffer);
  free(fmt);

  return true;
}
#else
  struct wlr_surface *surface = view->wlr_surface;
  struct wlr_egl *egl = wlr_gles2_renderer_get_egl (self->wlr_renderer);
  GLuint tex, fbo;
  GLint gl_format;

  if (!surface || !wlr_egl_make_current (egl, EGL_NO_SURFACE, NULL)) {
    return false;
  }

  switch (shm_fmt) {
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_XRGB8888:
    gl_format = GL_BGRA_EXT;
    break;
  default:
    gl_format = GL_RGBA;
    break;
  }

  struct view_render_data render_data ={
    .view = view,
    .width = width,
    .height = height
  };

  glGenTextures (1, &tex);
  glBindTexture (GL_TEXTURE_2D, tex);
  glTexImage2D (GL_TEXTURE_2D, 0, gl_format, width, height, 0, gl_format, GL_UNSIGNED_BYTE, NULL);
  glBindTexture (GL_TEXTURE_2D, 0);

  glGenFramebuffers (1, &fbo);
  glBindFramebuffer (GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

  wlr_renderer_begin (self->wlr_renderer, width, height);
  wlr_renderer_clear (self->wlr_renderer, (float[])COLOR_TRANSPARENT);
  wlr_surface_for_each_surface (surface, view_render_iterator, &render_data);
  wlr_renderer_end (self->wlr_renderer);

  wlr_renderer_read_pixels (self->wlr_renderer, shm_fmt, flags, stride, width, height, 0, 0, 0, 0, data);

  glDeleteFramebuffers (1, &fbo);
  glDeleteTextures (1, &tex);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);

  wlr_egl_unset_current (egl);

  return true;
}
#endif

static void surface_send_frame_done_iterator(PhocOutput *output,
		struct wlr_surface *surface, struct wlr_box *box, float rotation,
		float scale, void *data) {
	struct timespec *when = data;
	wlr_surface_send_frame_done(surface, when);
}


static void
render_damage (PhocRenderer *self, PhocOutput *output)
{
  int nrects;
  pixman_box32_t *rects;
  struct wlr_box box;
  pixman_region32_t previous_damage;

  pixman_region32_init(&previous_damage);
  pixman_region32_subtract(&previous_damage,
                           &output->damage->previous[output->damage->previous_idx],
                           &output->damage->current);

  rects = pixman_region32_rectangles(&previous_damage, &nrects);
  for (int i = 0; i < nrects; ++i) {
    wlr_box_from_pixman_box32(&box, rects[i]);
    wlr_render_rect(self->wlr_renderer, &box, (float[])COLOR_TRANSPARENT_MAGENTA,
                    output->wlr_output->transform_matrix);
  }

  rects = pixman_region32_rectangles(&output->damage->current, &nrects);
  for (int i = 0; i < nrects; ++i) {
    wlr_box_from_pixman_box32(&box, rects[i]);
    wlr_render_rect(self->wlr_renderer, &box, (float[])COLOR_TRANSPARENT_YELLOW,
                    output->wlr_output->transform_matrix);
  }
  wlr_output_schedule_frame(output->wlr_output);
  pixman_region32_fini(&previous_damage);
}


/**
 * phoc_renderer_render_output:
 * @self: The renderer
 * @output: The output to render
 *
 * Render a given output.
 */
void phoc_renderer_render_output (PhocRenderer *self, PhocOutput *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	PhocDesktop *desktop = PHOC_DESKTOP (output->desktop);
	PhocServer *server = phoc_server_get_default ();
	struct wlr_renderer *wlr_renderer;

        g_assert (PHOC_IS_RENDERER (self));
        wlr_renderer = self->wlr_renderer;

	if (!wlr_output->enabled) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	float clear_color[] = COLOR_BLACK;

	g_signal_emit (self, signals[RENDER_START], 0, output);

	// Check if we can delegate the fullscreen surface to the output
	if (phoc_output_has_fullscreen_view (output)) {
		static bool last_scanned_out = false;
		bool scanned_out = scan_out_fullscreen_view(output);

		if (scanned_out && !last_scanned_out) {
			g_debug ("Starting fullscreen view scan out");
		} else if (!scanned_out && last_scanned_out) {
			g_debug ("Stopping fullscreen view scan out");
		}
		last_scanned_out = scanned_out;

		if (scanned_out) {
			goto send_frame_done;
		}
	}

	bool needs_frame;
	pixman_region32_t buffer_damage;
	pixman_region32_init(&buffer_damage);
	if (!wlr_output_damage_attach_render(output->damage, &needs_frame,
			&buffer_damage)) {
		return;
	}

	struct render_data data = {
		.damage = &buffer_damage,
		.alpha = 1.0,
	};

	enum wl_output_transform transform =
		wlr_output_transform_invert(wlr_output->transform);

	if (G_UNLIKELY (server->debug_flags & PHOC_SERVER_DEBUG_FLAG_DAMAGE_TRACKING)) {
		pixman_region32_union_rect(&buffer_damage, &buffer_damage,
			0, 0, wlr_output->width, wlr_output->height);
		wlr_region_transform(&buffer_damage, &buffer_damage,
			transform, wlr_output->width, wlr_output->height);
		needs_frame |= pixman_region32_not_empty(&output->damage->current);
		needs_frame |= pixman_region32_not_empty(&output->damage->previous[output->damage->previous_idx]);
	}

	if (!needs_frame) {
		// Output doesn't need swap and isn't damaged, skip rendering completely
		wlr_output_rollback(wlr_output);
		goto buffer_damage_finish;
	}

	wlr_renderer_begin(wlr_renderer, wlr_output->width, wlr_output->height);

	if (!pixman_region32_not_empty(&buffer_damage)) {
		// Output isn't damaged but needs buffer swap
		goto renderer_end;
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&buffer_damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output->wlr_output, &rects[i]);
		wlr_renderer_clear(wlr_renderer, clear_color);
	}

	// If a view is fullscreen on this output, render it
	if (output->fullscreen_view != NULL) {
		PhocView *view = output->fullscreen_view;

		render_view(output, view, &data);

		// During normal rendering the xwayland window tree isn't traversed
		// because all windows are rendered. Here we only want to render
		// the fullscreen window's children so we have to traverse the tree.
#ifdef PHOC_XWAYLAND
		if (view->type == PHOC_XWAYLAND_VIEW) {
			PhocXWaylandSurface *xwayland_surface =
				phoc_xwayland_surface_from_view(view);
			phoc_output_xwayland_children_for_each_surface(output,
				xwayland_surface->xwayland_surface,
				render_surface_iterator, &data);
		}
#endif

		if (output->force_shell_reveal) {
			// Render top layer above fullscreen view when requested
			render_layer(output, &buffer_damage,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);
		}
	} else {
		// Render background and bottom layers under views
		render_layer(output, &buffer_damage,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
		render_layer(output, &buffer_damage,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);

		PhocView *view;
			// Render all views
		wl_list_for_each_reverse(view, &desktop->views, link) {
			if (phoc_desktop_view_is_visible(desktop, view)) {
				render_view(output, view, &data);
			}
		}

		// Render top layer above views
		render_layer(output, &buffer_damage,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);
	}

	render_drag_icons(output, &buffer_damage, server->input);

	render_layer(output, &buffer_damage,
		&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);

renderer_end:
	wlr_output_render_software_cursors(wlr_output, &buffer_damage);
	wlr_renderer_scissor(wlr_renderer, NULL);

	render_touch_points (output);
	g_signal_emit (self, signals[RENDER_END], 0, output);
	if (G_UNLIKELY (server->debug_flags & PHOC_SERVER_DEBUG_FLAG_DAMAGE_TRACKING))
		render_damage (self, output);

	wlr_renderer_end(wlr_renderer);

	int width, height;
	wlr_output_transformed_resolution(wlr_output, &width, &height);

	pixman_region32_t frame_damage;
	pixman_region32_init(&frame_damage);

	wlr_region_transform(&frame_damage, &output->damage->current,
		transform, width, height);

	wlr_output_set_damage(wlr_output, &frame_damage);
	pixman_region32_fini(&frame_damage);

	if (!wlr_output_commit(wlr_output)) {
		goto buffer_damage_finish;
	}

buffer_damage_finish:
	pixman_region32_fini(&buffer_damage);

send_frame_done:
	// Send frame done events to all visible surfaces
	phoc_output_for_each_surface(output, surface_send_frame_done_iterator, &now, true);

	damage_touch_points(output);
	g_clear_list (&output->debug_touch_points, g_free);
}


static void
phoc_renderer_constructed (GObject *object)
{
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR > 12
  PhocRenderer *self = PHOC_RENDERER (object);
  int drm_fd = fcntl (wlr_renderer_get_drm_fd (self->wlr_renderer), F_DUPFD_CLOEXEC, 0);
  // TODO: use wlr_allocator_autocreate or retrieve a reference from wlr_renderer once possible
  if (wlr_renderer_is_gles2 (self->wlr_renderer))
    self->allocator = (struct wlr_allocator*) wlr_gbm_allocator_create (drm_fd);
#endif
}


static void
phoc_renderer_finalize (GObject *object)
{
  PhocRenderer *self = PHOC_RENDERER (object);
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR > 12
  if (self->allocator)
    wlr_allocator_destroy (self->allocator);
  /* TODO: destroy wlr_renderer */
#endif
}


static void
phoc_renderer_class_init (PhocRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_renderer_get_property;
  object_class->set_property = phoc_renderer_set_property;
  object_class->constructed = phoc_renderer_constructed;
  object_class->finalize = phoc_renderer_finalize;

  /**
   * PhocRenderer:wlr-renderer
   *
   * The wlr-renderer from wlroots to use
   */
  props[PROP_WLR_RENDERER] =
    g_param_spec_pointer ("wlr-renderer",
                          "",
                          "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhocRenderer::render-start
   * @self: The renderer emitting the signal
   * @output: The output being rendered on
   *
   * This signal is emitted at the start of a render pass
   */
  signals[RENDER_START] = g_signal_new ("render-start",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 1, PHOC_TYPE_OUTPUT);
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
                                      G_TYPE_NONE, 1, PHOC_TYPE_OUTPUT);
}


static void
phoc_renderer_init (PhocRenderer *self)
{
}


PhocRenderer *
phoc_renderer_new (struct wlr_renderer *wlr_renderer)
{
  return PHOC_RENDERER (g_object_new (PHOC_TYPE_RENDERER,
                                      "wlr-renderer", wlr_renderer,
                                      NULL));
}
