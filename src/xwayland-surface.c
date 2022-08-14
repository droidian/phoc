/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-xwayland-surface"

#include "config.h"
#include "server.h"
#include "xwayland-surface.h"

#include <wlr/xwayland.h>

enum {
  PROP_0,
  PROP_WLR_XWAYLAND_SURFACE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE (PhocXWaylandSurface, phoc_xwayland_surface, PHOC_TYPE_VIEW)

static
bool is_moveable(PhocView *view)
{
	PhocServer *server = phoc_server_get_default ();
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view (view)->xwayland_surface;

	if (xwayland_surface->window_type == NULL)
		return true;

	for (guint i = 0; i < xwayland_surface->window_type_len; i++)
		if (xwayland_surface->window_type[i] != server->desktop->xwayland_atoms[NET_WM_WINDOW_TYPE_NORMAL] &&
		    xwayland_surface->window_type[i] != server->desktop->xwayland_atoms[NET_WM_WINDOW_TYPE_DIALOG])
			return false;

	return true;
}

static void set_active(PhocView *view, bool active) {
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;
	wlr_xwayland_surface_activate(xwayland_surface, active);
	wlr_xwayland_surface_restack(xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
}

static void move(PhocView *view, double x, double y) {
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;

	if (!is_moveable (view))
		return;

	view_update_position(view, x, y);
	wlr_xwayland_surface_configure(xwayland_surface, x, y,
		xwayland_surface->width, xwayland_surface->height);
}

static void apply_size_constraints(PhocView *view,
		struct wlr_xwayland_surface *xwayland_surface, uint32_t width,
		uint32_t height, uint32_t *dest_width, uint32_t *dest_height) {
	*dest_width = width;
	*dest_height = height;

	if (view_is_maximized(view))
		return;

	struct wlr_xwayland_surface_size_hints *size_hints =
		xwayland_surface->size_hints;
	if (size_hints != NULL) {
		if (width < (uint32_t)size_hints->min_width) {
			*dest_width = size_hints->min_width;
		} else if (size_hints->max_width > 0 &&
				width > (uint32_t)size_hints->max_width) {
			*dest_width = size_hints->max_width;
		}
		if (height < (uint32_t)size_hints->min_height) {
			*dest_height = size_hints->min_height;
		} else if (size_hints->max_height > 0 &&
				height > (uint32_t)size_hints->max_height) {
			*dest_height = size_hints->max_height;
		}
	}
}

static void resize(PhocView *view, uint32_t width, uint32_t height) {
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(view, xwayland_surface, width, height, &constrained_width,
		&constrained_height);

	wlr_xwayland_surface_configure(xwayland_surface, xwayland_surface->x,
			xwayland_surface->y, constrained_width, constrained_height);
}

static void move_resize(PhocView *view, double x, double y,
		uint32_t width, uint32_t height) {
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;

	if (!is_moveable (view)) {
		x = view->box.x;
		y = view->box.y;
	}

	bool update_x = x != view->box.x;
	bool update_y = y != view->box.y;

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(view, xwayland_surface, width, height, &constrained_width,
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

	wlr_xwayland_surface_configure(xwayland_surface, x, y, constrained_width,
		constrained_height);
}

static void _close(PhocView *view) {
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;
	wlr_xwayland_surface_close(xwayland_surface);
}


static bool want_scaling(PhocView *view) {
	return false;
}

static bool want_auto_maximize(PhocView *view) {
	struct wlr_xwayland_surface *surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;

	if (surface->size_hints &&
	    (surface->size_hints->min_width > 0 && surface->size_hints->min_width == surface->size_hints->max_width) &&
	    (surface->size_hints->min_height > 0 && surface->size_hints->min_height == surface->size_hints->max_height))
		return false;

	return is_moveable(view);
}

static void set_maximized(PhocView *view, bool maximized) {
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;
	wlr_xwayland_surface_set_maximized(xwayland_surface, maximized);
}

static void set_fullscreen(PhocView *view, bool fullscreen) {
	struct wlr_xwayland_surface *xwayland_surface =
		phoc_xwayland_surface_from_view(view)->xwayland_surface;
	wlr_xwayland_surface_set_fullscreen(xwayland_surface, fullscreen);
}

static void
phoc_xwayland_surface_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  PhocXWaylandSurface *self = PHOC_XWAYLAND_SURFACE (object);

  switch (property_id) {
  case PROP_WLR_XWAYLAND_SURFACE:
    self->xwayland_surface = g_value_get_pointer (value);
    self->xwayland_surface->data = self;
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_xwayland_surface_finalize (GObject *object)
{
  PhocXWaylandSurface *self = PHOC_XWAYLAND_SURFACE(object);

  wl_list_remove(&self->destroy.link);
  wl_list_remove(&self->request_configure.link);
  wl_list_remove(&self->request_move.link);
  wl_list_remove(&self->request_resize.link);
  wl_list_remove(&self->request_maximize.link);
  wl_list_remove(&self->set_title.link);
  wl_list_remove(&self->set_class.link);
  wl_list_remove(&self->set_startup_id.link);
  wl_list_remove(&self->map.link);
  wl_list_remove(&self->unmap.link);

  self->xwayland_surface->data = NULL;

  G_OBJECT_CLASS (phoc_xwayland_surface_parent_class)->finalize (object);
}


static void
phoc_xwayland_surface_class_init (PhocXWaylandSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocViewClass *view_class = PHOC_VIEW_CLASS (klass);

  object_class->set_property = phoc_xwayland_surface_set_property;
  object_class->finalize = phoc_xwayland_surface_finalize;

  view_class->resize = resize;
  view_class->move = move;
  view_class->move_resize = move_resize;
  view_class->want_scaling = want_scaling;
  view_class->want_auto_maximize = want_auto_maximize;
  view_class->set_active = set_active;
  view_class->set_fullscreen = set_fullscreen;
  view_class->set_maximized = set_maximized;
  view_class->close = _close;

  /**
   * PhocXWaylandSurface:wlr-xwayland-surface:
   *
   * The underlying wlroots xwayland-surface
   */
  props[PROP_WLR_XWAYLAND_SURFACE] =
    g_param_spec_pointer ("wlr-xwayland-surface", "", "",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_xwayland_surface_init (PhocXWaylandSurface *self)
{
  PHOC_VIEW (self)->type = PHOC_XWAYLAND_VIEW;
}


PhocXWaylandSurface *
phoc_xwayland_surface_new (struct wlr_xwayland_surface *surface)
{
  return PHOC_XWAYLAND_SURFACE (g_object_new (PHOC_TYPE_XWAYLAND_SURFACE,
                                              "wlr-xwayland-surface", surface,
                                              NULL));
}

/**
 * phoc_xwayland_surface_from_view:
 * @view: A view
 *
 * Returns the [class@XWaylandSurface] associated with this
 * [type@Phoc.View]. It is a programming error if the [class@View]
 * isn't a [type@XWaylandSurface].
 *
 * Returns: (transfer none): Returns the [type@XWaylandSurface]
 */
PhocXWaylandSurface *
phoc_xwayland_surface_from_view (PhocView *view)
{
  g_assert (PHOC_IS_XWAYLAND_SURFACE (view));
  return PHOC_XWAYLAND_SURFACE (view);
}
