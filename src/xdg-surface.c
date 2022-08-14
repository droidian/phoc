/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-xdg-surface"

#include "config.h"

#include "xdg-surface.h"

#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

enum {
  PROP_0,
  PROP_WLR_XDG_SURFACE,
  PROP_LAST_PROP
};

static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE (PhocXdgSurface, phoc_xdg_surface, PHOC_TYPE_VIEW)

static void
phoc_xdg_surface_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  PhocXdgSurface *self = PHOC_XDG_SURFACE (object);

  switch (property_id) {
  case PROP_WLR_XDG_SURFACE:
    self->xdg_surface = g_value_get_pointer (value);
    self->xdg_surface->data = self;
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void set_active(PhocView *view, bool active) {
	struct wlr_xdg_surface *xdg_surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;
	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_set_activated(xdg_surface, active);
	}
}

static void apply_size_constraints(struct wlr_xdg_surface *xdg_surface,
		uint32_t width, uint32_t height, uint32_t *dest_width,
		uint32_t *dest_height) {
	*dest_width = width;
	*dest_height = height;

	struct wlr_xdg_toplevel_state *state = &xdg_surface->toplevel->current;
	if (width < state->min_width) {
		*dest_width = state->min_width;
	} else if (state->max_width > 0 &&
			width > state->max_width) {
		*dest_width = state->max_width;
	}
	if (height < state->min_height) {
		*dest_height = state->min_height;
	} else if (state->max_height > 0 &&
			height > state->max_height) {
		*dest_height = state->max_height;
	}
}

static void resize(PhocView *view, uint32_t width, uint32_t height) {
	struct wlr_xdg_surface *xdg_surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(xdg_surface, width, height, &constrained_width,
		&constrained_height);

	wlr_xdg_toplevel_set_size(xdg_surface, constrained_width,
		constrained_height);

	view_send_frame_done_if_not_visible (view);
}

static void move_resize(PhocView *view, double x, double y,
		uint32_t width, uint32_t height) {
	PhocXdgSurface *xdg_surface =
		phoc_xdg_surface_from_view (view);
	struct wlr_xdg_surface *wlr_xdg_surface = xdg_surface->xdg_surface;
	if (wlr_xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	bool update_x = x != view->box.x;
	bool update_y = y != view->box.y;

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(wlr_xdg_surface, width, height, &constrained_width,
		&constrained_height);

	if (update_x) {
		x = x + width - constrained_width;
	}
	if (update_y) {
		y = y + height - constrained_height;
	}

	view->pending_move_resize.update_x = update_x;
	view->pending_move_resize.update_y = update_y;
	view->pending_move_resize.x = x;
	view->pending_move_resize.y = y;
	view->pending_move_resize.width = constrained_width;
	view->pending_move_resize.height = constrained_height;

	uint32_t serial = wlr_xdg_toplevel_set_size(wlr_xdg_surface,
		constrained_width, constrained_height);
	if (serial > 0) {
		xdg_surface->pending_move_resize_configure_serial = serial;
	} else if (xdg_surface->pending_move_resize_configure_serial == 0) {
		view_update_position(view, x, y);
	}

	view_send_frame_done_if_not_visible (view);
}

static bool want_scaling(PhocView *view) {
	return true;
}

static bool want_auto_maximize(PhocView *view) {
	struct wlr_xdg_surface *surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;

	return surface->toplevel && !surface->toplevel->parent;
}

static void set_maximized(PhocView *view, bool maximized) {
	struct wlr_xdg_surface *xdg_surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	wlr_xdg_toplevel_set_maximized(xdg_surface, maximized);
}

static void
set_tiled (PhocView *view, bool tiled)
{
  struct wlr_xdg_surface *xdg_surface = phoc_xdg_surface_from_view (view)->xdg_surface;

  if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    return;

  if (!tiled) {
    wlr_xdg_toplevel_set_tiled (xdg_surface, WLR_EDGE_NONE);
    return;
  }

  switch (view->tile_direction) {
    case PHOC_VIEW_TILE_LEFT:
      wlr_xdg_toplevel_set_tiled (xdg_surface, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);
      break;
    case PHOC_VIEW_TILE_RIGHT:
      wlr_xdg_toplevel_set_tiled (xdg_surface, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
      break;
    default:
      g_warn_if_reached ();
  }
}

static void set_fullscreen(PhocView *view, bool fullscreen) {
	struct wlr_xdg_surface *xdg_surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	wlr_xdg_toplevel_set_fullscreen(xdg_surface, fullscreen);
}

static void _close(PhocView *view) {
	struct wlr_xdg_surface *xdg_surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;
	struct wlr_xdg_popup *popup = NULL;
	wl_list_for_each(popup, &xdg_surface->popups, link) {
		wlr_xdg_popup_destroy(popup->base);
	}
	wlr_xdg_toplevel_send_close(xdg_surface);

	view_send_frame_done_if_not_visible (view);
}

static void for_each_surface(PhocView *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xdg_surface *xdg_surface =
		phoc_xdg_surface_from_view (view)->xdg_surface;
	wlr_xdg_surface_for_each_surface(xdg_surface, iterator, user_data);
}

static void get_geometry(PhocView *view, struct wlr_box *geom) {
        phoc_xdg_surface_get_geometry (phoc_xdg_surface_from_view (view), geom);
}


static void
phoc_xdg_surface_finalize (GObject *object)
{
  PhocXdgSurface *self = PHOC_XDG_SURFACE(object);

  wl_list_remove(&self->surface_commit.link);
  wl_list_remove(&self->destroy.link);
  wl_list_remove(&self->new_popup.link);
  wl_list_remove(&self->map.link);
  wl_list_remove(&self->unmap.link);
  wl_list_remove(&self->request_move.link);
  wl_list_remove(&self->request_resize.link);
  wl_list_remove(&self->request_maximize.link);
  wl_list_remove(&self->request_fullscreen.link);
  wl_list_remove(&self->set_title.link);
  wl_list_remove(&self->set_app_id.link);
  wl_list_remove(&self->set_parent.link);
  self->xdg_surface->data = NULL;

  G_OBJECT_CLASS (phoc_xdg_surface_parent_class)->finalize (object);
}


static void
phoc_xdg_surface_class_init (PhocXdgSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocViewClass *view_class = PHOC_VIEW_CLASS (klass);

  object_class->finalize = phoc_xdg_surface_finalize;
  object_class->set_property = phoc_xdg_surface_set_property;

  view_class->resize = resize;
  view_class->move_resize = move_resize;
  view_class->want_auto_maximize = want_auto_maximize;
  view_class->want_scaling = want_scaling;
  view_class->set_active = set_active;
  view_class->set_fullscreen = set_fullscreen;
  view_class->set_maximized = set_maximized;
  view_class->set_tiled = set_tiled;
  view_class->close = _close;
  view_class->for_each_surface = for_each_surface;
  view_class->get_geometry = get_geometry;

  /**
   * PhocXdgSurface:wlr-xdg-surface:
   *
   * The underlying wlroots xdg-surface
   */
  props[PROP_WLR_XDG_SURFACE] =
    g_param_spec_pointer ("wlr-xdg-surface", "", "",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_xdg_surface_init (PhocXdgSurface *self)
{
  PHOC_VIEW (self)->type = PHOC_XDG_SHELL_VIEW;
}


PhocXdgSurface *
phoc_xdg_surface_new (struct wlr_xdg_surface *wlr_xdg_surface)
{
  return PHOC_XDG_SURFACE (g_object_new (PHOC_TYPE_XDG_SURFACE,
                                         "wlr-xdg-surface", wlr_xdg_surface,
                                         NULL));
}

void
phoc_xdg_surface_get_geometry (PhocXdgSurface *self, struct wlr_box *geom)
{
  wlr_xdg_surface_get_geometry (self->xdg_surface, geom);
}

PhocXdgSurface *
phoc_xdg_surface_from_view (PhocView *view) {
	g_assert (PHOC_IS_XDG_SURFACE (view));
	return PHOC_XDG_SURFACE (view);
}
