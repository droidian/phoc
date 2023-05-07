/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef PHOC_XWAYLAND

#include "view.h"

#include <wlr/xwayland.h>
#include <xcb/xproto.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_XWAYLAND_SURFACE (phoc_xwayland_surface_get_type ())

enum xwayland_atom_name {
  NET_WM_WINDOW_TYPE_NORMAL,
  NET_WM_WINDOW_TYPE_DIALOG,
  XWAYLAND_ATOM_LAST
};

G_DECLARE_FINAL_TYPE (PhocXWaylandSurface, phoc_xwayland_surface, PHOC, XWAYLAND_SURFACE, PhocView)

PhocXWaylandSurface *phoc_xwayland_surface_new (struct wlr_xwayland_surface *surface);
struct wlr_xwayland_surface *phoc_xwayland_surface_get_wlr_surface (PhocXWaylandSurface *self);

G_END_DECLS

#endif
