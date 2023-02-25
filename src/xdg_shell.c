#define G_LOG_DOMAIN "phoc-xdg-shell"

#include "phoc-config.h"
#include "xdg-surface.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include "cursor.h"
#include "desktop.h"
#include "input.h"
#include "server.h"
#include "view.h"

struct phoc_xdg_toplevel_decoration {
  struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
  PhocXdgSurface *surface;
  struct wl_listener destroy;
  struct wl_listener request_mode;
  struct wl_listener surface_commit;
};

typedef struct phoc_xdg_popup {
  PhocViewChild child;
  struct wlr_xdg_popup *wlr_popup;

  struct wl_listener destroy;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener new_popup;
} PhocXdgPopup;

static const struct phoc_view_child_interface popup_impl;

static void popup_destroy(PhocViewChild *child) {
	assert(child->impl == &popup_impl);
	struct phoc_xdg_popup *popup = (struct phoc_xdg_popup *)child;
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->new_popup.link);
	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->unmap.link);
	free(popup);
}

static const struct phoc_view_child_interface popup_impl = {
	.destroy = popup_destroy,
};

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct phoc_xdg_popup *popup =
		wl_container_of(listener, popup, destroy);
	phoc_view_child_destroy(&popup->child);
}

static void popup_handle_map(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct phoc_xdg_popup *popup = wl_container_of(listener, popup, map);
	phoc_view_child_damage_whole (&popup->child);
	phoc_input_update_cursor_focus(server->input);
	popup->child.mapped = true;
}

static void popup_handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct phoc_xdg_popup *popup = wl_container_of(listener, popup, unmap);
	phoc_view_child_damage_whole (&popup->child);
	phoc_input_update_cursor_focus(server->input);
	popup->child.mapped = false;
}

static struct phoc_xdg_popup *popup_create(PhocView *view,
	struct wlr_xdg_popup *wlr_popup);

static void popup_handle_new_popup(struct wl_listener *listener, void *data) {
	struct phoc_xdg_popup *popup =
		wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	popup_create(popup->child.view, wlr_popup);
}

static void popup_unconstrain (PhocXdgPopup* popup)
{
  // get the output of the popup's positioner anchor point and convert it to
  // the toplevel parent's coordinate system and then pass it to
  // wlr_xdg_popup_unconstrain_from_box
  PhocView *view = PHOC_VIEW (popup->child.view);
  struct wlr_output_layout *layout = view->desktop->layout;
  struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;

  int anchor_lx, anchor_ly;
  wlr_xdg_popup_get_anchor_point (wlr_popup, &anchor_lx, &anchor_ly);

  int popup_lx, popup_ly;
  wlr_xdg_popup_get_toplevel_coords (wlr_popup, wlr_popup->geometry.x,
                                     wlr_popup->geometry.y, &popup_lx, &popup_ly);
  popup_lx += view->box.x;
  popup_ly += view->box.y;

  anchor_lx += popup_lx;
  anchor_ly += popup_ly;

  struct wlr_output *output = wlr_output_layout_output_at (layout, view->box.x, view->box.y);
  if (output == NULL)
    return;

  struct wlr_box *output_box = wlr_output_layout_get_box (view->desktop->layout, output);
  PhocOutput *phoc_output = PHOC_OUTPUT (output->data);
  struct wlr_box usable_area = phoc_output->usable_area;
  usable_area.x += output_box->x;
  usable_area.y += output_box->y;

  // the output box expressed in the coordinate system of the toplevel parent
  // of the popup
  struct wlr_box output_toplevel_sx_box = {
    .x = usable_area.x - view->box.x,
    .y = usable_area.y - view->box.y,
    .width = usable_area.width,
    .height = usable_area.height,
  };

  wlr_xdg_popup_unconstrain_from_box (popup->wlr_popup, &output_toplevel_sx_box);
}

static struct phoc_xdg_popup *popup_create(PhocView *view,
		struct wlr_xdg_popup *wlr_popup) {
	struct phoc_xdg_popup *popup =
		calloc(1, sizeof(struct phoc_xdg_popup));
	if (popup == NULL) {
		return NULL;
	}
	popup->wlr_popup = wlr_popup;
	phoc_view_child_init(&popup->child, &popup_impl,
			     view, wlr_popup->base->surface);
	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);
	popup->map.notify = popup_handle_map;
	wl_signal_add(&wlr_popup->base->events.map, &popup->map);
	popup->unmap.notify = popup_handle_unmap;
	wl_signal_add(&wlr_popup->base->events.unmap, &popup->unmap);
	popup->new_popup.notify = popup_handle_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

	popup_unconstrain(popup);

	return popup;
}

static void get_size(PhocView *view, struct wlr_box *box) {
	struct wlr_xdg_surface *xdg_surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;

	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(xdg_surface, &geo_box);
	box->width = geo_box.width;
	box->height = geo_box.height;
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, request_move);
	PhocView *view = PHOC_VIEW (phoc_xdg_surface);
	PhocInput *input = server->input;
	struct wlr_xdg_toplevel_move_event *e = data;
	PhocSeat *seat = phoc_input_seat_from_wlr_seat(input, e->seat->seat);

	// TODO verify event serial
	if (!seat || phoc_seat_get_cursor(seat)->mode != PHOC_CURSOR_PASSTHROUGH) {
		return;
	}
	phoc_seat_begin_move(seat, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, request_resize);
	PhocView *view = PHOC_VIEW (phoc_xdg_surface);
	PhocInput *input = server->input;
	struct wlr_xdg_toplevel_resize_event *e = data;
	PhocSeat *seat = phoc_input_seat_from_wlr_seat(input, e->seat->seat);

	// TODO verify event serial
	assert(seat);
	if (!seat || phoc_seat_get_cursor(seat)->mode != PHOC_CURSOR_PASSTHROUGH) {
		return;
	}
	phoc_seat_begin_resize(seat, view, e->edges);
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, request_maximize);
	PhocView *view = PHOC_VIEW (phoc_xdg_surface);
	struct wlr_xdg_surface *surface = phoc_xdg_surface->xdg_surface;

	if (surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	if (surface->toplevel->requested.maximized) {
		view_maximize(view, NULL);
	} else {
		view_restore(view);
	}
}

static void handle_request_fullscreen(struct wl_listener *listener,
		void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, request_fullscreen);
	PhocView *view = PHOC_VIEW (phoc_xdg_surface);
	struct wlr_xdg_surface *surface = phoc_xdg_surface->xdg_surface;
	struct wlr_xdg_toplevel_set_fullscreen_event *e = data;

	if (surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	phoc_view_set_fullscreen(view, e->fullscreen, e->output);
}

static void handle_set_title(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, set_title);

	view_set_title(&phoc_xdg_surface->view,
			phoc_xdg_surface->xdg_surface->toplevel->title);
}

static void handle_set_app_id(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, set_app_id);

	phoc_view_set_app_id(&phoc_xdg_surface->view,
			     phoc_xdg_surface->xdg_surface->toplevel->app_id);
}

static void handle_set_parent(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, set_parent);

	if (phoc_xdg_surface->xdg_surface->toplevel->parent) {
		PhocXdgSurface *parent = phoc_xdg_surface->xdg_surface->toplevel->parent->data;
		view_set_parent(&phoc_xdg_surface->view, &parent->view);
	} else {
		view_set_parent(&phoc_xdg_surface->view, NULL);
	}
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_surface =
		wl_container_of(listener, phoc_surface, surface_commit);
	PhocView *view = PHOC_VIEW (phoc_surface);
	struct wlr_xdg_surface *surface = phoc_surface->xdg_surface;

	if (!surface->mapped) {
		return;
	}

	phoc_view_apply_damage(view);

	struct wlr_box size;
	get_size(view, &size);
	view_update_size(view, size.width, size.height);

	uint32_t pending_serial =
		phoc_surface->pending_move_resize_configure_serial;
	if (pending_serial > 0 && pending_serial >= surface->current.configure_serial) {
		double x = view->box.x;
		double y = view->box.y;
		if (view->pending_move_resize.update_x) {
			if (view_is_floating (view)) {
				x = view->pending_move_resize.x + view->pending_move_resize.width -
					size.width;
			} else {
				x = view->pending_move_resize.x;
			}
		}
		if (view->pending_move_resize.update_y) {
			if (view_is_floating (view)) {
				y = view->pending_move_resize.y + view->pending_move_resize.height -
					size.height;
			} else {
				y = view->pending_move_resize.y;
			}
		}
		view_update_position(view, x, y);

		if (pending_serial == surface->current.configure_serial) {
			phoc_surface->pending_move_resize_configure_serial = 0;
		}
	}

	struct wlr_box geometry;
        phoc_xdg_surface_get_geometry (phoc_surface, &geometry);
	if (phoc_surface->saved_geometry.x != geometry.x || phoc_surface->saved_geometry.y != geometry.y) {
		view_update_position(view,
		                     view->box.x + (phoc_surface->saved_geometry.x - geometry.x) * view->scale,
		                     view->box.y + (phoc_surface->saved_geometry.y - geometry.y) * view->scale);
	}
	phoc_surface->saved_geometry = geometry;
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	popup_create(PHOC_VIEW (phoc_xdg_surface), wlr_popup);
}

static void handle_map(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, map);
	PhocView *view = PHOC_VIEW (phoc_xdg_surface);

	struct wlr_box box;
	get_size(view, &box);
	view->box.width = box.width;
	view->box.height = box.height;
	phoc_xdg_surface_get_geometry (phoc_xdg_surface, &phoc_xdg_surface->saved_geometry);

	phoc_view_map(view, phoc_xdg_surface->xdg_surface->surface);
	view_setup(view);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, unmap);
	view_unmap (PHOC_VIEW (phoc_xdg_surface));
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	PhocXdgSurface *phoc_xdg_surface =
		wl_container_of(listener, phoc_xdg_surface, destroy);
	g_object_unref (phoc_xdg_surface);
}

void handle_xdg_shell_surface(struct wl_listener *listener, void *data) {
	struct wlr_xdg_surface *surface = data;
	assert(surface->role != WLR_XDG_SURFACE_ROLE_NONE);

	if (surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		g_debug ("new xdg popup");
		return;
	}

	PhocDesktop *desktop =
		wl_container_of(listener, desktop, xdg_shell_surface);

	g_debug ("new xdg toplevel: title=%s, app_id=%s",
		surface->toplevel->title, surface->toplevel->app_id);

	wlr_xdg_surface_ping(surface);

	PhocXdgSurface *phoc_surface = phoc_xdg_surface_new (surface);
	phoc_surface->view.desktop = desktop;

	// catch up with state accumulated before commiting
	if (surface->toplevel->parent) {
		PhocXdgSurface *parent = surface->toplevel->parent->data;
		view_set_parent(&phoc_surface->view, &parent->view);
	}

	if (surface->toplevel->requested.maximized) {
		view_maximize(&phoc_surface->view, NULL);
	}
	phoc_view_set_fullscreen(&phoc_surface->view, surface->toplevel->requested.fullscreen,
		surface->toplevel->requested.fullscreen_output);
	view_auto_maximize(&phoc_surface->view);
	view_set_title(&phoc_surface->view, surface->toplevel->title);

	// Check for app-id override coming from gtk-shell
	PhocGtkSurface *gtk_surface = phoc_gtk_shell_get_gtk_surface_from_wlr_surface (desktop->gtk_shell, surface->surface);
	if (gtk_surface && phoc_gtk_surface_get_app_id (gtk_surface)) {
          phoc_view_set_app_id (&phoc_surface->view, phoc_gtk_surface_get_app_id (gtk_surface));
	} else {
		phoc_view_set_app_id (&phoc_surface->view, surface->toplevel->app_id);
	}

	phoc_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->surface->events.commit,
		&phoc_surface->surface_commit);
	phoc_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &phoc_surface->destroy);
	phoc_surface->map.notify = handle_map;
	wl_signal_add(&surface->events.map, &phoc_surface->map);
	phoc_surface->unmap.notify = handle_unmap;
	wl_signal_add(&surface->events.unmap, &phoc_surface->unmap);
	phoc_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->toplevel->events.request_move,
		&phoc_surface->request_move);
	phoc_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->toplevel->events.request_resize,
		&phoc_surface->request_resize);
	phoc_surface->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&surface->toplevel->events.request_maximize,
		&phoc_surface->request_maximize);
	phoc_surface->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&surface->toplevel->events.request_fullscreen,
		&phoc_surface->request_fullscreen);
	phoc_surface->set_title.notify = handle_set_title;
	wl_signal_add(&surface->toplevel->events.set_title, &phoc_surface->set_title);
	phoc_surface->set_app_id.notify = handle_set_app_id;
	wl_signal_add(&surface->toplevel->events.set_app_id,
		&phoc_surface->set_app_id);
	phoc_surface->set_parent.notify = handle_set_parent;
	wl_signal_add(&surface->toplevel->events.set_parent, &phoc_surface->set_parent);
	phoc_surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&surface->events.new_popup, &phoc_surface->new_popup);
}


static void decoration_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct phoc_xdg_toplevel_decoration *decoration =
		wl_container_of(listener, decoration, destroy);

	decoration->surface->xdg_toplevel_decoration = NULL;
	view_update_decorated(&decoration->surface->view, false);
	wl_list_remove(&decoration->destroy.link);
	wl_list_remove(&decoration->request_mode.link);
	wl_list_remove(&decoration->surface_commit.link);
	free(decoration);
}

static void decoration_handle_request_mode(struct wl_listener *listener,
		void *data) {
	struct phoc_xdg_toplevel_decoration *decoration =
		wl_container_of(listener, decoration, request_mode);

	enum wlr_xdg_toplevel_decoration_v1_mode mode =
		decoration->wlr_decoration->requested_mode;
	if (mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) {
		mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	}
	wlr_xdg_toplevel_decoration_v1_set_mode(decoration->wlr_decoration, mode);
}

static void decoration_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct phoc_xdg_toplevel_decoration *decoration =
		wl_container_of(listener, decoration, surface_commit);

	bool decorated = decoration->wlr_decoration->current.mode ==
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	view_update_decorated(&decoration->surface->view, decorated);
}

void handle_xdg_toplevel_decoration(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;

	g_debug ("new xdg toplevel decoration");

	PhocXdgSurface *xdg_surface = wlr_decoration->surface->data;
	assert(xdg_surface != NULL);
	struct wlr_xdg_surface *wlr_xdg_surface = xdg_surface->xdg_surface;

	struct phoc_xdg_toplevel_decoration *decoration =
		calloc(1, sizeof(struct phoc_xdg_toplevel_decoration));
	if (decoration == NULL) {
		return;
	}
	decoration->wlr_decoration = wlr_decoration;
	decoration->surface = xdg_surface;
	xdg_surface->xdg_toplevel_decoration = decoration;

	decoration->destroy.notify = decoration_handle_destroy;
	wl_signal_add(&wlr_decoration->events.destroy, &decoration->destroy);
	decoration->request_mode.notify = decoration_handle_request_mode;
	wl_signal_add(&wlr_decoration->events.request_mode,
		&decoration->request_mode);
	decoration->surface_commit.notify = decoration_handle_surface_commit;
	wl_signal_add(&wlr_xdg_surface->surface->events.commit,
		&decoration->surface_commit);

	decoration_handle_request_mode(&decoration->request_mode, wlr_decoration);
}
