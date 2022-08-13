/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_XDG_SURFACE (phoc_xdg_surface_get_type ())

/**
 * PhocXdgSurface:
 *
 * An xdg surface.
 *
 * For how to setup such an object see handle_xdg_shell_surface.
 */
typedef struct _PhocXdgSurface {
	PhocView view;

	struct wlr_xdg_surface *xdg_surface;

	struct wlr_box saved_geometry;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener set_parent;

	struct wl_listener surface_commit;

	uint32_t pending_move_resize_configure_serial;

	struct phoc_xdg_toplevel_decoration *xdg_toplevel_decoration;
} PhocXdgSurface;

G_DECLARE_FINAL_TYPE (PhocXdgSurface, phoc_xdg_surface, PHOC, XDG_SURFACE, PhocView)

PhocXdgSurface *phoc_xdg_surface_new (struct wlr_xdg_surface *xdg_surface);
void            phoc_xdg_surface_get_geometry (PhocXdgSurface *self, struct wlr_box *geom);

PhocXdgSurface *phoc_xdg_surface_from_view(PhocView *view);


G_END_DECLS
