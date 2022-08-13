/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_XWAYLAND_SURFACE (phoc_xwayland_surface_get_type ())

#ifdef PHOC_XWAYLAND
G_DECLARE_FINAL_TYPE (PhocXWaylandSurface, phoc_xwayland_surface, PHOC, XWAYLAND_SURFACE, PhocView)

/**
 * PhocXWaylandSurface
 *
 * An XWayland Surface.
 *
 * For how to setup such an object see handle_xwayland_surface.
 */
typedef struct _PhocXWaylandSurface {
	PhocView view;

	struct wlr_xwayland_surface *xwayland_surface;

	struct wl_listener destroy;
	struct wl_listener request_configure;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener set_title;
	struct wl_listener set_class;
	struct wl_listener set_startup_id;

	struct wl_listener surface_commit;
} PhocXWaylandSurface;

PhocXWaylandSurface *phoc_xwayland_surface_new (struct wlr_xwayland_surface *surface);
PhocXWaylandSurface *phoc_xwayland_surface_from_view(PhocView *view);

#endif

G_END_DECLS
