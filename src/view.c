#define G_LOG_DOMAIN "phoc-view"

#include "config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output_layout.h>
#include "desktop.h"
#include "input.h"
#include "seat.h"
#include "server.h"
#include "view.h"

void view_init(struct roots_view *view, const struct roots_view_interface *impl,
		enum roots_view_type type, PhocDesktop *desktop) {
	assert(impl->destroy);
	view->impl = impl;
	view->type = type;
	view->desktop = desktop;
	view->alpha = 1.0f;
	view->scale = 1.0f;
	view->title = NULL;
	view->app_id = NULL;
	view->scaling_enabled = FALSE;
	view->send_activate = FALSE;
	wl_signal_init(&view->events.unmap);
	wl_signal_init(&view->events.destroy);
	wl_list_init(&view->child_surfaces);
	wl_list_init(&view->stack);
}

void view_destroy(struct roots_view *view) {
	if (view == NULL) {
		return;
	}

	if (view->parent) {
		wl_list_remove(&view->parent_link);
		wl_list_init(&view->parent_link);
	}
	struct roots_view *child, *tmp;
	wl_list_for_each_safe(child, tmp, &view->stack, parent_link) {
		wl_list_remove(&child->parent_link);
		wl_list_init(&child->parent_link);
		child->parent = view->parent;
		if (child->parent) {
			wl_list_insert(&child->parent->stack, &child->parent_link);
		}
	}

	wl_signal_emit(&view->events.destroy, view);

	if (view->wlr_surface != NULL) {
		view_unmap(view);
	}

	// Can happen if fullscreened while unmapped, and hasn't been mapped
	if (view->fullscreen_output != NULL) {
		view->fullscreen_output->fullscreen_view = NULL;
	}

	view->impl->destroy(view);
}

gboolean view_is_maximized(const struct roots_view *view)
{
	return view->state == PHOC_VIEW_STATE_MAXIMIZED;
}

gboolean view_is_tiled(const struct roots_view *view)
{
	return view->state == PHOC_VIEW_STATE_TILED;
}

void view_get_box(const struct roots_view *view, struct wlr_box *box) {
	box->x = view->box.x;
	box->y = view->box.y;
	box->width = view->box.width * view->scale;
	box->height = view->box.height * view->scale;
}


void
view_get_geometry (struct roots_view *view, struct wlr_box *geom)
{
  if (view->impl->get_geometry) {
    view->impl->get_geometry (view, geom);
  } else {
    geom->x = 0;
    geom->y = 0;
    geom->width = view->box.width * view->scale;
    geom->height = view->box.height * view->scale;
  }
}


void view_get_deco_box(const struct roots_view *view, struct wlr_box *box) {
	view_get_box(view, box);
	if (!view->decorated) {
		return;
	}

	box->x -= view->border_width;
	box->y -= (view->border_width + view->titlebar_height);
	box->width += view->border_width * 2;
	box->height += (view->border_width * 2 + view->titlebar_height);
}

enum roots_deco_part view_get_deco_part(struct roots_view *view, double sx,
		double sy) {
	if (!view->decorated) {
		return ROOTS_DECO_PART_NONE;
	}

	int sw = view->wlr_surface->current.width;
	int sh = view->wlr_surface->current.height;
	int bw = view->border_width;
	int titlebar_h = view->titlebar_height;

	if (sx > 0 && sx < sw && sy < 0 && sy > -view->titlebar_height) {
		return ROOTS_DECO_PART_TITLEBAR;
	}

	enum roots_deco_part parts = 0;
	if (sy >= -(titlebar_h + bw) &&
			sy <= sh + bw) {
		if (sx < 0 && sx > -bw) {
			parts |= ROOTS_DECO_PART_LEFT_BORDER;
		} else if (sx > sw && sx < sw + bw) {
			parts |= ROOTS_DECO_PART_RIGHT_BORDER;
		}
	}

	if (sx >= -bw && sx <= sw + bw) {
		if (sy > sh && sy <= sh + bw) {
			parts |= ROOTS_DECO_PART_BOTTOM_BORDER;
		} else if (sy >= -(titlebar_h + bw) && sy < 0) {
			parts |= ROOTS_DECO_PART_TOP_BORDER;
		}
	}

	// TODO corners

	return parts;
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_leave(surface, wlr_output);
}

static void view_update_output(struct roots_view *view,
		const struct wlr_box *before) {
	PhocDesktop *desktop = view->desktop;

	if (view->wlr_surface == NULL) {
		return;
	}

	struct wlr_box box;
	view_get_box(view, &box);

	PhocOutput *output;
	wl_list_for_each(output, &desktop->outputs, link) {
		bool intersected = before != NULL && wlr_output_layout_intersects(
			desktop->layout, output->wlr_output, before);
		bool intersects = wlr_output_layout_intersects(desktop->layout,
			output->wlr_output, &box);
		if (intersected && !intersects) {
			view_for_each_surface(view, surface_send_leave_iterator, output->wlr_output);
			if (view->toplevel_handle) {
				wlr_foreign_toplevel_handle_v1_output_leave(
					view->toplevel_handle, output->wlr_output);
			}
		}
		if (!intersected && intersects) {
			view_for_each_surface(view, surface_send_enter_iterator, output->wlr_output);
			if (view->toplevel_handle) {
				wlr_foreign_toplevel_handle_v1_output_enter(
					view->toplevel_handle, output->wlr_output);
			}
		}
	}
}

static void
view_save(struct roots_view *view)
{
  if (view_is_maximized (view) || view_is_tiled (view))
    return;

  /* backup window state */
  struct wlr_box geom;
  view_get_geometry(view, &geom);
  view->saved.x = view->box.x + geom.x * view->scale;
  view->saved.y = view->box.y + geom.y * view->scale;
  view->saved.width = view->box.width;
  view->saved.height = view->box.height;
}

void view_move(struct roots_view *view, double x, double y) {
	if (view->box.x == x && view->box.y == y) {
		return;
	}

	view->pending_move_resize.update_x = false;
	view->pending_move_resize.update_y = false;

	struct wlr_box before;
	view_get_box(view, &before);
	if (view->impl->move) {
		view->impl->move(view, x, y);
	} else {
		view_update_position(view, x, y);
	}
	view_update_output(view, &before);
}

void
view_appear_activated (struct roots_view *view, bool activated)
{
  if (view->impl->activate)
    view->impl->activate (view, activated);
}

void view_activate(struct roots_view *view, bool activate) {
	if (view->send_activate || !view->desktop->maximize) {
		view_appear_activated(view, activate);
	}

	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_activated(view->toplevel_handle,
			activate);
	}

	if (activate && view->fullscreen_output && view->fullscreen_output->force_shell_reveal) {
		view->fullscreen_output->force_shell_reveal = false;
		phoc_output_damage_whole(view->fullscreen_output);
	}
}

void view_resize(struct roots_view *view, uint32_t width, uint32_t height) {
	struct wlr_box before;
	view_get_box(view, &before);

	if (view->impl->resize) {
		view->impl->resize(view, width, height);
	}
	view_update_output(view, &before);
}

void view_move_resize(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height) {
	bool update_x = x != view->box.x;
	bool update_y = y != view->box.y;
	if (!update_x && !update_y) {
		view_resize(view, width, height);
		return;
	}

	if (view->impl->move_resize) {
		view->impl->move_resize(view, x, y, width, height);
		return;
	}

	view->pending_move_resize.update_x = update_x;
	view->pending_move_resize.update_y = update_y;
	view->pending_move_resize.x = x;
	view->pending_move_resize.y = y;
	view->pending_move_resize.width = width;
	view->pending_move_resize.height = height;

	view_resize(view, width, height);
}

static struct wlr_output *view_get_output(struct roots_view *view) {
	struct wlr_box view_box;
	view_get_box(view, &view_box);

	double output_x, output_y;
	wlr_output_layout_closest_point(view->desktop->layout, NULL,
		view->box.x + (double)view_box.width/2,
		view->box.y + (double)view_box.height/2,
		&output_x, &output_y);
	return wlr_output_layout_output_at(view->desktop->layout, output_x,
		output_y);
}

void view_arrange_maximized(struct roots_view *view, struct wlr_output *output) {
	if (view->fullscreen_output)
		return;

	if (!output)
		output = view_get_output(view);

	if (!output)
		return;

	PhocOutput *phoc_output = output->data;
	struct wlr_box *output_box =
		wlr_output_layout_get_box(view->desktop->layout, output);
	struct wlr_box usable_area = phoc_output->usable_area;
	usable_area.x += output_box->x;
	usable_area.y += output_box->y;

	view_move_resize(view, usable_area.x / view->scale, usable_area.y / view->scale,
			usable_area.width / view->scale, usable_area.height / view->scale);
}

void
view_arrange_tiled (struct roots_view *view, struct wlr_output *output)
{
  if (view->fullscreen_output)
    return;

  if (!output)
    output = view_get_output(view);

  if (!output)
    return;

  PhocOutput *phoc_output = output->data;
  struct wlr_box *output_box = wlr_output_layout_get_box (view->desktop->layout, output);
  struct wlr_box usable_area = phoc_output->usable_area;
  int x;

  usable_area.x += output_box->x;
  usable_area.y += output_box->y;

  switch (view->tile_direction) {
  case PHOC_VIEW_TILE_LEFT:
    x = usable_area.x;
    break;
  case PHOC_VIEW_TILE_RIGHT:
    x = usable_area.x + (0.5 * usable_area.width);
    break;
  default:
    g_error ("Invalid tiling direction %d", view->tile_direction);
  }

  /*
   * No need to take geometry into account since maximized surfaces
   * usually don't have drop shadows. It wouldn't be up to date here
   * yet anyway since a client's configure is not yet processed.
   */
  view_move_resize (view, x, usable_area.y,
                    usable_area.width / 2, usable_area.height);
}

/*
 * Check if a view needs to be maximized
 */
static bool
want_auto_maximize(struct roots_view *view) {
  if (!view->desktop->maximize)
    return false;

  if (view->impl->want_auto_maximize)
    return view->impl->want_auto_maximize(view);

  return false;
}

void view_maximize(struct roots_view *view, struct wlr_output *output) {
	if (view_is_maximized (view) && view_get_output(view) == output) {
		return;
	}

	if (view->fullscreen_output != NULL) {
		return;
	}

	if (view->impl->maximize) {
		view->impl->maximize(view, true);
	}

	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_maximized(view->toplevel_handle, true);
	}

	view_save (view);

	view->state = PHOC_VIEW_STATE_MAXIMIZED;
	view_arrange_maximized(view, output);
}

/*
 * Maximize view if in auto-maximize mode otherwise do nothing.
 */
void
view_auto_maximize(struct roots_view *view)
{
  if (want_auto_maximize (view))
    view_maximize (view, NULL);
}

void
view_restore(struct roots_view *view)
{
  if (!view_is_maximized (view) && !view_is_tiled (view))
    return;

  if (want_auto_maximize (view))
    return;

  struct wlr_box geom;
  view_get_geometry(view, &geom);

  view->state = PHOC_VIEW_STATE_NORMAL;
  view_move_resize (view, view->saved.x - geom.x * view->scale, view->saved.y - geom.y * view->scale,
                    view->saved.width, view->saved.height);

  if (view->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_maximized (view->toplevel_handle, false);

  if (view->impl->maximize)
    view->impl->maximize (view, false);
}

void view_set_fullscreen(struct roots_view *view, bool fullscreen,
		struct wlr_output *output) {
	bool was_fullscreen = view->fullscreen_output != NULL;

	// TODO: check if client is focused?

	if (!was_fullscreen) {
		if (view->impl->set_fullscreen) {
			view->impl->set_fullscreen(view, fullscreen);
		}

		if (view->toplevel_handle) {
			wlr_foreign_toplevel_handle_v1_set_fullscreen(view->toplevel_handle,
					fullscreen);
		}
	}

	struct wlr_box view_geom;
	view_get_geometry(view, &view_geom);

	if (fullscreen) {
		if (output == NULL) {
			output = view_get_output(view);
		}
		PhocOutput *phoc_output = output->data;
		if (phoc_output == NULL) {
			return;
		}

		if (was_fullscreen) {
			view->fullscreen_output->fullscreen_view = NULL;
		}

		struct wlr_box view_box;
		view_get_box(view, &view_box);

		view_save (view);

		struct wlr_box *output_box =
			wlr_output_layout_get_box(view->desktop->layout, output);
		view_move_resize(view, output_box->x, output_box->y, output_box->width,
			output_box->height);

		phoc_output->fullscreen_view = view;
		phoc_output->force_shell_reveal = false;
		view->fullscreen_output = phoc_output;
		phoc_output_damage_whole(phoc_output);
	}

	if (was_fullscreen && !fullscreen) {
		PhocOutput *phoc_output = view->fullscreen_output;
		view->fullscreen_output->fullscreen_view = NULL;
		view->fullscreen_output = NULL;

		phoc_output_damage_whole(phoc_output);

		if (view->state == PHOC_VIEW_STATE_MAXIMIZED) {
			view_arrange_maximized (view, phoc_output->wlr_output);
		} else if (view->state == PHOC_VIEW_STATE_TILED) {
			view_arrange_tiled (view, phoc_output->wlr_output);
		} else {
			view_move_resize(view, view->saved.x - view_geom.x * view->scale, view->saved.y - view_geom.y * view->scale,
			                 view->saved.width, view->saved.height);
		}

		view_auto_maximize(view);
	}
}

void view_close(struct roots_view *view) {
	if (view->impl->close) {
		view->impl->close(view);
	}
}

bool
view_move_to_next_output (struct roots_view *view, enum wlr_direction direction)
{
  PhocDesktop *desktop = view->desktop;
  struct wlr_output_layout *layout = view->desktop->layout;
  const struct wlr_output_layout_output *l_output;
  PhocOutput *phoc_output;
  struct wlr_output *output, *new_output;
  struct wlr_box usable_area;
  double x, y;

  if (view->fullscreen_output)
    return false;

  output = view_get_output(view);
  if (!output)
    return false;

  /* use current view's x,y as ref_lx, ref_ly */
  new_output = wlr_output_layout_adjacent_output (layout, direction, output,
						  view->box.x, view->box.y);
  if (!new_output)
    return false;

  phoc_output = new_output->data;
  usable_area = phoc_output->usable_area;
  l_output = wlr_output_layout_get(desktop->layout, new_output);

  /* use a proper position on the new output */
  x = usable_area.x + l_output->x;
  y = usable_area.y + l_output->y;
  g_debug("moving view to %f %f", x, y);

  view->saved.x = x;
  view->saved.y = y;

  if (view_is_maximized (view)) {
    view_arrange_maximized (view, new_output);
  } else if (view_is_tiled (view)) {
    view_arrange_tiled (view, new_output);
  } else {
    view_move(view, x, y);
  }

  return true;
}

void
view_tile(struct roots_view *view, PhocViewTileDirection direction, struct wlr_output *output)
{
  if (view->fullscreen_output)
    return;

  /* Set the maximized flag on the toplevel so it remove it's drop shadows */
  if (view->impl->maximize)
    view->impl->maximize(view, true);

  view_save (view);

  view->state = PHOC_VIEW_STATE_TILED;
  view->tile_direction = direction;
  view_arrange_tiled (view, output);
}

bool view_center(struct roots_view *view) {
        PhocServer *server = phoc_server_get_default ();
	struct wlr_box box, geom;
	view_get_box(view, &box);
	view_get_geometry (view, &geom);

	PhocDesktop *desktop = view->desktop;
	PhocInput *input = server->input;
	struct roots_seat *seat = input_last_active_seat(input);
	struct roots_cursor *cursor;

	if (!seat) {
		return false;
	}
	cursor = roots_seat_get_cursor (seat);

	struct wlr_output *output = wlr_output_layout_output_at(desktop->layout,
		cursor->cursor->x, cursor->cursor->y);
	if (!output) {
		// empty layout
		return false;
	}

	const struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(desktop->layout, output);

	PhocOutput *phoc_output = output->data;
	struct wlr_box usable_area = phoc_output->usable_area;

	double view_x = (double)(usable_area.width - box.width) / 2 +
	  usable_area.x + l_output->x - geom.x * view->scale;
	double view_y = (double)(usable_area.height - box.height) / 2 +
	  usable_area.y + l_output->y - geom.y * view->scale;

	g_debug ("moving view to %f %f", view_x, view_y);
	view_move(view, view_x / view->scale, view_y / view->scale);

	if (!desktop->maximize) {
		// TODO: fitting floating oversized windows requires more work (!228)
		return true;
	}

	if (view->box.width > phoc_output->usable_area.width || view->box.height > phoc_output->usable_area.height) {
		view_resize (view,
		             (view->box.width > phoc_output->usable_area.width) ? phoc_output->usable_area.width : view->box.width,
		             (view->box.height > phoc_output->usable_area.height) ? phoc_output->usable_area.height : view->box.height);
	}

	return true;
}

void view_child_destroy(struct roots_view_child *child) {
	if (child == NULL) {
		return;
	}
	view_damage_whole(child->view);
	wl_list_remove(&child->link);
	wl_list_remove(&child->commit.link);
	wl_list_remove(&child->new_subsurface.link);
	child->impl->destroy(child);
}

static void view_child_handle_commit(struct wl_listener *listener,
		void *data) {
	struct roots_view_child *child = wl_container_of(listener, child, commit);
	view_apply_damage(child->view);
}

static void view_child_handle_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct roots_view_child *child =
		wl_container_of(listener, child, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	subsurface_create(child->view, wlr_subsurface);
}

void view_child_init(struct roots_view_child *child,
		const struct roots_view_child_interface *impl, struct roots_view *view,
		struct wlr_surface *wlr_surface) {
	assert(impl->destroy);
	child->impl = impl;
	child->view = view;
	child->wlr_surface = wlr_surface;
	child->commit.notify = view_child_handle_commit;
	wl_signal_add(&wlr_surface->events.commit, &child->commit);
	child->new_subsurface.notify = view_child_handle_new_subsurface;
	wl_signal_add(&wlr_surface->events.new_subsurface, &child->new_subsurface);
	wl_list_insert(&view->child_surfaces, &child->link);
}

static const struct roots_view_child_interface subsurface_impl;

static void subsurface_destroy(struct roots_view_child *child) {
	assert(child->impl == &subsurface_impl);
	struct roots_subsurface *subsurface = (struct roots_subsurface *)child;
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	free(subsurface);
}

static const struct roots_view_child_interface subsurface_impl = {
	.destroy = subsurface_destroy,
};

static void subsurface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_subsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);
	view_child_destroy(&subsurface->view_child);
}

static void subsurface_handle_map(struct wl_listener *listener,
		void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_subsurface *subsurface =
		wl_container_of(listener, subsurface, map);
	struct roots_view *view = subsurface->view_child.view;
	view_damage_whole(view);
	phoc_input_update_cursor_focus(server->input);

	struct wlr_box box;
	view_get_box(view, &box);
	PhocOutput *output;
	wl_list_for_each(output, &view->desktop->outputs, link) {
		bool intersects = wlr_output_layout_intersects(view->desktop->layout,
			output->wlr_output, &box);
		if (intersects) {
			wlr_surface_send_enter (subsurface->wlr_subsurface->surface, output->wlr_output);
		}
	}
}

static void subsurface_handle_unmap(struct wl_listener *listener,
		void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_subsurface *subsurface =
		wl_container_of(listener, subsurface, unmap);
	struct roots_view *view = subsurface->view_child.view;
	view_damage_whole(view);
	phoc_input_update_cursor_focus(server->input);
}

struct roots_subsurface *subsurface_create(struct roots_view *view,
		struct wlr_subsurface *wlr_subsurface) {
	struct roots_subsurface *subsurface =
		calloc(1, sizeof(struct roots_subsurface));
	if (subsurface == NULL) {
		return NULL;
	}
	subsurface->wlr_subsurface = wlr_subsurface;
	view_child_init(&subsurface->view_child, &subsurface_impl,
		view, wlr_subsurface->surface);
	subsurface->destroy.notify = subsurface_handle_destroy;
	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->map.notify = subsurface_handle_map;
	wl_signal_add(&wlr_subsurface->events.map, &subsurface->map);
	subsurface->unmap.notify = subsurface_handle_unmap;
	wl_signal_add(&wlr_subsurface->events.unmap, &subsurface->unmap);
	return subsurface;
}

static void view_handle_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct roots_view *view = wl_container_of(listener, view, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	subsurface_create(view, wlr_subsurface);
}

static gchar *
munge_app_id (const gchar *app_id)
{
  gchar *id = g_strdup (app_id);
  gint i;

  g_strcanon (id,
              "0123456789"
              "abcdefghijklmnopqrstuvwxyz"
              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              "-",
              '-');
  for (i = 0; id[i] != '\0'; i++)
    id[i] = g_ascii_tolower (id[i]);

  return id;
}

static void view_update_scale(struct roots_view *view) {
	PhocServer *server = phoc_server_get_default ();

	if (!view->impl->want_scaling(view)) {
		return;
	}

	if (!view->scaling_enabled && !phoc_desktop_get_scale_to_fit (server->desktop)) {
		return;
	}

	struct wlr_output *output = view_get_output(view);
	if (!output) {
		return;
	}

	PhocOutput *phoc_output = output->data;

	float scalex = 1.0f, scaley = 1.0f, oldscale = view->scale;
	scalex = phoc_output->usable_area.width / (float)view->box.width;
	scaley = phoc_output->usable_area.height / (float)view->box.height;
	if (scaley < scalex) {
		view->scale = scaley;
	} else {
		view->scale = scalex;
	}
	if (view->scale < 0.5f) {
		view->scale = 0.5f;
	}
	if (view->scale > 1.0f || view->fullscreen_output) {
		view->scale = 1.0f;
	}
	if (view->scale != oldscale) {
		if (view_is_maximized(view)) {
			view_arrange_maximized(view, NULL);
		} else if (view_is_tiled(view)) {
			view_arrange_tiled(view, NULL);
		} else {
			view_center(view);
		}
	}
}

void view_map(struct roots_view *view, struct wlr_surface *surface) {
	PhocServer *server = phoc_server_get_default ();
	assert(view->wlr_surface == NULL);

	view->wlr_surface = surface;

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &view->wlr_surface->subsurfaces,
			parent_link) {
		subsurface_create(view, subsurface);
	}

	view->new_subsurface.notify = view_handle_new_subsurface;
	wl_signal_add(&view->wlr_surface->events.new_subsurface,
		&view->new_subsurface);

	if (view->desktop->maximize) {
		view_appear_activated(view, true);

		if (!wl_list_empty(&view->desktop->views)) {
			// mapping a new stack may make the old stack disappear, so damage its area
			struct roots_view *top_view = wl_container_of(view->desktop->views.next, view, link);
			while (top_view) {
				view_damage_whole(top_view);
				top_view = top_view->parent;
			}
		}
	}

	wl_list_insert(&view->desktop->views, &view->link);
	view_damage_whole(view);
	phoc_input_update_cursor_focus(server->input);
}

void view_unmap(struct roots_view *view) {
	assert(view->wlr_surface != NULL);

	bool was_visible = phoc_desktop_view_is_visible(view->desktop, view);

	wl_signal_emit(&view->events.unmap, view);

	view_damage_whole(view);

	wl_list_remove(&view->new_subsurface.link);

	struct roots_view_child *child, *tmp;
	wl_list_for_each_safe(child, tmp, &view->child_surfaces, link) {
		view_child_destroy(child);
	}

	if (view->fullscreen_output != NULL) {
		phoc_output_damage_whole(view->fullscreen_output);
		view->fullscreen_output->fullscreen_view = NULL;
		view->fullscreen_output = NULL;
	}

	wl_list_remove(&view->link);

	if (was_visible && view->desktop->maximize && !wl_list_empty(&view->desktop->views)) {
		// damage the newly activated stack as well since it may have just become visible
		struct roots_view *top_view = wl_container_of(view->desktop->views.next, view, link);
		while (top_view) {
			view_damage_whole(top_view);
			top_view = top_view->parent;
		}
	}

	view->wlr_surface = NULL;
	view->box.width = view->box.height = 0;

	if (view->toplevel_handle) {
		view->toplevel_handle->data = NULL;
		wlr_foreign_toplevel_handle_v1_destroy(view->toplevel_handle);
		view->toplevel_handle = NULL;
	}
}

void view_initial_focus(struct roots_view *view) {
	PhocServer *server = phoc_server_get_default ();
	PhocInput *input = server->input;
	// TODO what seat gets focus? the one with the last input event?
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_set_focus(seat, view);
	}
}


void view_setup(struct roots_view *view) {
	view_create_foreign_toplevel_handle(view);
	view_initial_focus(view);

	if (view->app_id) {
		g_autofree gchar *munged_app_id = munge_app_id (view->app_id);
		g_autofree gchar *path = g_strconcat ("/sm/puri/phoc/application/", munged_app_id, "/", NULL);
		g_autoptr (GSettings) setting =  g_settings_new_with_path ("sm.puri.phoc.application", path);
		view->scaling_enabled = g_settings_get_boolean (setting, "scale-to-fit");
		view->send_activate = g_settings_get_boolean (setting, "send-activate");
	}

	if (view->fullscreen_output == NULL && !view_is_maximized(view)) {
		view_center(view);
	}
	view_update_scale(view);

	view_update_output(view, NULL);

	wlr_foreign_toplevel_handle_v1_set_fullscreen(view->toplevel_handle,
	                                              view->fullscreen_output != NULL);
	wlr_foreign_toplevel_handle_v1_set_maximized(view->toplevel_handle,
	                                             view_is_maximized(view));
	wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle,
	                                         view->title ?: "");
	wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle,
	                                          view->app_id ?: "");
}

void view_apply_damage(struct roots_view *view) {
	PhocOutput *output;
	wl_list_for_each(output, &view->desktop->outputs, link) {
		phoc_output_damage_from_view(output, view);
	}
}

void view_damage_whole(struct roots_view *view) {
	PhocOutput *output;
	wl_list_for_each(output, &view->desktop->outputs, link) {
		phoc_output_damage_whole_view(output, view);
	}
}

void view_for_each_surface(struct roots_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	if (view->impl->for_each_surface) {
		view->impl->for_each_surface(view, iterator, user_data);
	} else if (view->wlr_surface) {
		wlr_surface_for_each_surface(view->wlr_surface, iterator, user_data);
	}
}

void view_update_position(struct roots_view *view, int x, int y) {
	if (view->box.x == x && view->box.y == y) {
		return;
	}

	view_damage_whole(view);
	view->box.x = x;
	view->box.y = y;
	view_damage_whole(view);
}

void view_update_size(struct roots_view *view, int width, int height) {

	if (view->box.width == width && view->box.height == height) {
		return;
	}

	view_damage_whole(view);
	view->box.width = width;
	view->box.height = height;
	view_update_scale(view);
	view_damage_whole(view);
}

void view_update_decorated(struct roots_view *view, bool decorated) {
	if (view->decorated == decorated) {
		return;
	}

	view_damage_whole(view);
	view->decorated = decorated;
	if (decorated) {
		view->border_width = 4;
		view->titlebar_height = 12;
	} else {
		view->border_width = 0;
		view->titlebar_height = 0;
	}
	view_damage_whole(view);
}

void view_set_title(struct roots_view *view, const char *title) {
	free(view->title);
	view->title = title ? strdup(title) : NULL;

	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle, title ?: "");
	}
}

void view_set_parent(struct roots_view *view, struct roots_view *parent) {
	// setting a new parent may cause a cycle
	struct roots_view *node = parent;
	while (node) {
		g_return_if_fail(node != view);
		node = node->parent;
	}

	if (view->parent) {
		wl_list_remove(&view->parent_link);
		wl_list_init(&view->parent_link);
	}

	view->parent = parent;
	if (parent) {
		wl_list_insert(&parent->stack, &view->parent_link);
	}
}

void view_set_app_id(struct roots_view *view, const char *app_id) {
	free(view->app_id);
	view->app_id = app_id ? strdup(app_id) : NULL;

	view_update_scale(view);

	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle, app_id ?: "");
	}
}

static void handle_toplevel_handle_request_maximize(struct wl_listener *listener,
		void *data) {
	struct roots_view *view = wl_container_of(listener, view,
			toplevel_handle_request_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
	if (event->maximized) {
		view_maximize(view, NULL);
	} else {
		view_restore(view);
	}
}

static void handle_toplevel_handle_request_activate(struct wl_listener *listener,
		void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_view *view =
		wl_container_of(listener, view, toplevel_handle_request_activate);
	struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;

	struct roots_seat *seat;
	wl_list_for_each(seat, &server->input->seats, link) {
		if (event->seat == seat->seat) {
			roots_seat_set_focus(seat, view);
		}
	}
}

static void handle_toplevel_handle_request_fullscreen(struct wl_listener *listener,
		void *data)  {
	struct roots_view *view =
		wl_container_of(listener, view, toplevel_handle_request_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	view_set_fullscreen(view, event->fullscreen, event->output);
}

static void handle_toplevel_handle_request_close(struct wl_listener *listener,
		void *data) {
	struct roots_view *view =
		wl_container_of(listener, view, toplevel_handle_request_close);
	view_close(view);
}

void view_create_foreign_toplevel_handle(struct roots_view *view) {
	view->toplevel_handle =
		wlr_foreign_toplevel_handle_v1_create(
			view->desktop->foreign_toplevel_manager_v1);

	view->toplevel_handle_request_maximize.notify =
		handle_toplevel_handle_request_maximize;
	wl_signal_add(&view->toplevel_handle->events.request_maximize,
			&view->toplevel_handle_request_maximize);
	view->toplevel_handle_request_activate.notify =
		handle_toplevel_handle_request_activate;
	wl_signal_add(&view->toplevel_handle->events.request_activate,
			&view->toplevel_handle_request_activate);
	view->toplevel_handle_request_fullscreen.notify =
		handle_toplevel_handle_request_fullscreen;
	wl_signal_add(&view->toplevel_handle->events.request_fullscreen,
			&view->toplevel_handle_request_fullscreen);
	view->toplevel_handle_request_close.notify =
		handle_toplevel_handle_request_close;
	wl_signal_add(&view->toplevel_handle->events.request_close,
			&view->toplevel_handle_request_close);

	view->toplevel_handle->data = view;
}

/*
 * roots_view_from_wlr_surface::
 *
 * Given a #wlr_surface return the corresponding view
 */
struct roots_view *
roots_view_from_wlr_surface (struct wlr_surface *wlr_surface)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  struct roots_view *view;

  wl_list_for_each(view, &desktop->views, link) {
    if (view->wlr_surface == wlr_surface) {
      return view;
    }
  }

  return NULL;
}
