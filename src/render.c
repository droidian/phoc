#define G_LOG_DOMAIN "phoc-render"

#include "config.h"

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/config.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/version.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "layers.h"
#include "render.h"
#include "server.h"
#include "render.h"

#define TOUCH_POINT_RADIUS 30
#define TOUCH_POINT_BORDER 0.1

#define COLOR_BLACK                {0.0f, 0.0f, 0.0f, 1.0f}
#define COLOR_TRANSPARENT          {0.0f, 0.0f, 0.0f, 0.0f}
#define COLOR_TRANSPARENT_WHITE    {0.5f, 0.5f, 0.5f, 0.5f}
#define COLOR_TRANSPARENT_YELLOW   {0.5f, 0.5f, 0.0f, 0.5f}
#define COLOR_TRANSPARENT_MAGENTA  {0.5f, 0.0f, 0.5f, 0.5f}

struct render_data {
	pixman_region32_t *damage;
	float alpha;
};

struct touch_point_data {
  int id;
  double x;
  double y;
};

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
		const struct wlr_box *box, const float matrix[static 9],
		float rotation, float alpha) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	struct wlr_box rotated;
	wlr_box_rotated_bounds(&rotated, box, rotation);

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
		wlr_render_texture_with_matrix(renderer, texture, matrix, alpha);
	}

buffer_damage_finish:
	pixman_region32_fini(&damage);
}

static void
collect_touch_points (struct roots_output *output, struct wlr_surface *surface, struct wlr_box box, float scale)
{
  PhocServer *server = phoc_server_get_default ();
  if (G_LIKELY (!(server->debug_flags & PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS))) {
    return;
  }

  struct roots_seat *seat;
  wl_list_for_each(seat, &server->input->seats, link) {
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

static void render_surface_iterator(struct roots_output *output,
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

	struct wlr_box box = *_box;
	scale_box(&box, scale);
	scale_box(&box, wlr_output->scale);

	float matrix[9];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &box, transform, rotation,
		wlr_output->transform_matrix);

	render_texture(wlr_output, output_damage,
		texture, &box, matrix, rotation, alpha);

	wlr_presentation_surface_sampled_on_output(output->desktop->presentation,
		surface, wlr_output);

	collect_touch_points(output, surface, box, scale);
}

static void render_decorations(struct roots_output *output,
		struct roots_view *view, struct render_data *data) {
	if (!view->decorated || view->wlr_surface == NULL) {
		return;
	}

	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(output->wlr_output->backend);
	assert(renderer);

	struct wlr_box box;
	get_decoration_box(view, output, &box);

	struct wlr_box rotated;
	wlr_box_rotated_bounds(&rotated, &box, view->rotation);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, rotated.x, rotated.y,
		rotated.width, rotated.height);
	pixman_region32_intersect(&damage, &damage, data->damage);
	bool damaged = pixman_region32_not_empty(&damage);
	if (!damaged) {
		goto buffer_damage_finish;
	}

	float matrix[9];
	wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL,
		view->rotation, output->wlr_output->transform_matrix);
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

static void render_view(struct roots_output *output, struct roots_view *view,
		struct render_data *data) {
	// Do not render views fullscreened on other outputs
	if (view->fullscreen_output != NULL && view->fullscreen_output != output) {
		return;
	}

	data->alpha = view->alpha;
	if (view->fullscreen_output == NULL) {
		render_decorations(output, view, data);
	}
	output_view_for_each_surface(output, view, render_surface_iterator, data);
}

static void render_layer(struct roots_output *output,
		pixman_region32_t *damage, struct wl_list *layer_surfaces) {
	struct render_data data = {
		.damage = damage,
		.alpha = 1.0f,
	};
	output_layer_for_each_surface(output, layer_surfaces,
		render_surface_iterator, &data);
}

static void count_surface_iterator(struct roots_output *output,
		struct wlr_surface *surface, struct wlr_box *_box, float rotation,
		float scale, void *data) {
	size_t *n = data;
	n++;
}

static bool scan_out_fullscreen_view(struct roots_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	PhocServer *server = phoc_server_get_default ();

	struct roots_seat *seat;
	wl_list_for_each(seat, &server->input->seats, link) {
		struct roots_drag_icon *drag_icon = seat->drag_icon;
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

	struct roots_view *view = output->fullscreen_view;
	assert(view != NULL);
	if (view->wlr_surface == NULL) {
		return false;
	}
	size_t n_surfaces = 0;
	output_view_for_each_surface(output, view,
		count_surface_iterator, &n_surfaces);
	if (n_surfaces > 1) {
		return false;
	}

#if WLR_HAS_XWAYLAND
	if (view->type == ROOTS_XWAYLAND_VIEW) {
		struct roots_xwayland_surface *xwayland_surface =
			roots_xwayland_surface_from_view(view);
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

	wlr_presentation_surface_sampled(output->desktop->presentation, surface);

#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR < 11
	if (!wlr_output_attach_buffer(wlr_output, surface->buffer)) {
		return false;
	}
#else
	wlr_output_attach_buffer(wlr_output, &surface->buffer->base);
#endif
	return wlr_output_commit(wlr_output);
}

static void render_drag_icons(struct roots_output *output,
		pixman_region32_t *damage, PhocInput *input) {
	struct render_data data = {
		.damage = damage,
		.alpha = 1.0f,
	};
	output_drag_icons_for_each_surface(output, input,
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
wlr_box_from_touch_point (struct touch_point_data *touch_point, int radius)
{
  return (struct wlr_box) {
    .x = touch_point->x - radius / 2.0,
    .y = touch_point->y - radius / 2.0,
    .width = radius,
    .height = radius
  };
}

static void
render_touch_point_cb (gpointer data, gpointer user_data)
{
  struct touch_point_data *touch_point = data;

  struct roots_output *output = user_data;
  struct wlr_output *wlr_output = output->wlr_output;

  struct wlr_box point_box = wlr_box_from_touch_point (touch_point, TOUCH_POINT_RADIUS * wlr_output->scale);

  struct wlr_renderer *renderer = wlr_backend_get_renderer (wlr_output->backend);

  float color[4] = {touch_point->id * 100 + 240, 1.0, 1.0, 0.75};
  color_hsv_to_rgb (color);
  wlr_render_ellipse (renderer, &point_box, color, wlr_output->transform_matrix);

  point_box = wlr_box_from_touch_point (touch_point, TOUCH_POINT_RADIUS * (1.0 - TOUCH_POINT_BORDER) * wlr_output->scale);
  wlr_render_ellipse(renderer, &point_box, (float[])COLOR_TRANSPARENT_WHITE, wlr_output->transform_matrix);
}

static void
render_touch_points (struct roots_output *output)
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

  struct roots_output *output = user_data;
  struct wlr_output *wlr_output = output->wlr_output;

  struct wlr_box box = wlr_box_from_touch_point (touch_point, TOUCH_POINT_RADIUS * wlr_output->scale);
  pixman_region32_t region;
  pixman_region32_init_rect(&region, box.x, box.y, box.width, box.height);
  wlr_output_damage_add(output->damage, &region);
  pixman_region32_fini(&region);
}

static void
damage_touch_points (struct roots_output *output)
{
  if (!g_list_length (output->debug_touch_points)) {
    return;
  }
  g_list_foreach (output->debug_touch_points, damage_touch_point_cb, output);
  wlr_output_schedule_frame(output->wlr_output);
}

static void
view_render_iterator (struct wlr_surface *surface, int sx, int sy, void *data)
{
  if (!wlr_surface_has_buffer (surface)) {
    return;
  }

  PhocServer *server = phoc_server_get_default ();
  struct wlr_texture *view_texture = wlr_surface_get_texture (surface);

  struct roots_view *view = data;
  struct wlr_surface *root = view->wlr_surface;

  struct wlr_box box;
  view_get_box (view, &box);

  struct wlr_box geo;
  view_get_geometry (view, &geo);

  float mat[16];
  wlr_matrix_identity (mat);

  // NDC
  wlr_matrix_translate (mat, -1, -1);
  wlr_matrix_scale (mat, 2, 2);

  wlr_matrix_scale (mat, 1 / (float)box.width, 1 / (float)box.height);
  wlr_matrix_translate (mat, -geo.x, -geo.y);

  wlr_matrix_scale (mat, 1 / (float)root->current.scale, 1 / (float)root->current.scale);
  wlr_matrix_scale (mat, view->scale, view->scale);
  wlr_matrix_scale (mat, root->current.scale / surface->current.scale, root->current.scale / surface->current.scale);

  wlr_render_texture (server->renderer, view_texture, mat, sx * surface->current.scale, sy * surface->current.scale, 1.0);
}

gboolean
view_render_to_buffer (struct roots_view *view, enum wl_shm_format fmt, int width, int height, int stride, uint32_t *flags, void* data)
{
  PhocServer *server = phoc_server_get_default ();
  struct wlr_surface *surface = view->wlr_surface;
  struct wlr_egl *egl = wlr_gles2_renderer_get_egl (server->renderer);
  GLuint tex, fbo;
  GLint gl_format;

  if (!surface || !wlr_egl_make_current (egl, EGL_NO_SURFACE, NULL)) {
    return FALSE;
  }

  switch (fmt) {
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_XRGB8888:
    gl_format = GL_BGRA_EXT;
    break;
  default:
    gl_format = GL_RGBA;
    break;
  }

  glGenTextures (1, &tex);
  glBindTexture (GL_TEXTURE_2D, tex);
  glTexImage2D (GL_TEXTURE_2D, 0, gl_format, width, height, 0, gl_format, GL_UNSIGNED_BYTE, NULL);
  glBindTexture (GL_TEXTURE_2D, 0);

  glGenFramebuffers (1, &fbo);
  glBindFramebuffer (GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

  wlr_renderer_begin (server->renderer, width, height);
  wlr_renderer_clear (server->renderer, (float[])COLOR_TRANSPARENT);
  wlr_surface_for_each_surface (surface, view_render_iterator, view);
  wlr_renderer_end (server->renderer);

  wlr_renderer_read_pixels (server->renderer, fmt, flags, stride, width, height, 0, 0, 0, 0, data);

  glDeleteFramebuffers (1, &fbo);
  glDeleteTextures (1, &tex);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);

#if WLR_VERSION_MAJOR > 0 || WLR_VERSION_MINOR >= 11
  wlr_egl_unset_current (egl);
#endif

  return TRUE;
}

static void surface_send_frame_done_iterator(struct roots_output *output,
		struct wlr_surface *surface, struct wlr_box *box, float rotation,
		float scale, void *data) {
	struct timespec *when = data;
	wlr_surface_send_frame_done(surface, when);
}

void output_render(struct roots_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	PhocDesktop *desktop = output->desktop;
	PhocServer *server = phoc_server_get_default ();
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	if (!wlr_output->enabled) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	float clear_color[] = COLOR_BLACK;

	const struct wlr_box *output_box =
		wlr_output_layout_get_box(desktop->layout, wlr_output);

	// Check if we can delegate the fullscreen surface to the output
	if (output->fullscreen_view != NULL &&
			output->fullscreen_view->wlr_surface != NULL) {
		struct roots_view *view = output->fullscreen_view;

		// Make sure the view is centered on screen
		struct wlr_box view_box;
		view_get_box(view, &view_box);
		double view_x = (double)(output_box->width - view_box.width) / 2 +
			output_box->x;
		double view_y = (double)(output_box->height - view_box.height) / 2 +
			output_box->y;
		view_move(view, view_x, view_y);

		// Fullscreen views are rendered on a black background
		clear_color[0] = clear_color[1] = clear_color[2] = 0;

		// Check if we can scan-out the fullscreen view
		static bool last_scanned_out = false;
		bool scanned_out = scan_out_fullscreen_view(output);

		if (scanned_out && !last_scanned_out) {
			wlr_log(WLR_DEBUG, "Scanning out fullscreen view");
		}
		if (last_scanned_out && !scanned_out) {
			wlr_log(WLR_DEBUG, "Stopping fullscreen view scan out");
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

	wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);

	if (!pixman_region32_not_empty(&buffer_damage)) {
		// Output isn't damaged but needs buffer swap
		goto renderer_end;
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&buffer_damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output->wlr_output, &rects[i]);
		wlr_renderer_clear(renderer, clear_color);
	}

	// If a view is fullscreen on this output, render it
	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		render_view(output, view, &data);

		// During normal rendering the xwayland window tree isn't traversed
		// because all windows are rendered. Here we only want to render
		// the fullscreen window's children so we have to traverse the tree.
#ifdef PHOC_XWAYLAND
		if (view->type == ROOTS_XWAYLAND_VIEW) {
			struct roots_xwayland_surface *xwayland_surface =
				roots_xwayland_surface_from_view(view);
			output_xwayland_children_for_each_surface(output,
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

		// Render all views
		struct roots_view *view;
		wl_list_for_each_reverse(view, &desktop->views, link) {
			render_view(output, view, &data);
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
	wlr_renderer_scissor(renderer, NULL);

	render_touch_points (output);

	int width, height;
	wlr_output_transformed_resolution(wlr_output, &width, &height);

	pixman_region32_t frame_damage;
	pixman_region32_init(&frame_damage);

	wlr_region_transform(&frame_damage, &output->damage->current,
		transform, width, height);

	if (G_UNLIKELY (server->debug_flags & PHOC_SERVER_DEBUG_FLAG_DAMAGE_TRACKING)) {
		pixman_region32_t previous_damage;
		pixman_region32_init(&previous_damage);
		pixman_region32_subtract(&previous_damage,
			&output->damage->previous[output->damage->previous_idx], &output->damage->current);

		struct wlr_box box;
		rects = pixman_region32_rectangles(&previous_damage, &nrects);
		for (int i = 0; i < nrects; ++i) {
			wlr_box_from_pixman_box32(&box, rects[i]);
			wlr_render_rect(renderer, &box, (float[])COLOR_TRANSPARENT_MAGENTA, wlr_output->transform_matrix);
		}

		rects = pixman_region32_rectangles(&output->damage->current, &nrects);
		for (int i = 0; i < nrects; ++i) {
			wlr_box_from_pixman_box32(&box, rects[i]);
			wlr_render_rect(renderer, &box, (float[])COLOR_TRANSPARENT_YELLOW, wlr_output->transform_matrix);
		}
		wlr_output_schedule_frame(output->wlr_output);
		pixman_region32_fini(&previous_damage);
	}

	wlr_renderer_end(renderer);

	wlr_output_set_damage(wlr_output, &frame_damage);
	pixman_region32_fini(&frame_damage);

	if (!wlr_output_commit(wlr_output)) {
		goto buffer_damage_finish;
	}
	output->last_frame = desktop->last_frame = now;

buffer_damage_finish:
	pixman_region32_fini(&buffer_damage);

send_frame_done:
	// Send frame done events to all surfaces
	output_for_each_surface(output, surface_send_frame_done_iterator, &now);

	damage_touch_points(output);
	g_list_free_full(output->debug_touch_points, g_free);
	output->debug_touch_points = NULL;
}
