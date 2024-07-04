/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view.h"

#include <wlr/types/wlr_xdg_shell.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_XDG_SURFACE (phoc_xdg_surface_get_type ())

G_DECLARE_FINAL_TYPE (PhocXdgSurface, phoc_xdg_surface, PHOC, XDG_SURFACE, PhocView)

PhocXdgSurface     *phoc_xdg_surface_new (struct wlr_xdg_surface *xdg_surface);
void                phoc_xdg_surface_get_geometry (PhocXdgSurface *self, struct wlr_box *geom);
struct wlr_surface *phoc_xdg_surface_get_wlr_surface_at (PhocXdgSurface *self,
                                                         double           sx,
                                                         double           sy,
                                                         double          *sub_x,
                                                         double          *sub_y);

void                phoc_handle_xdg_shell_surface (struct wl_listener *listener, void *data);

G_END_DECLS
