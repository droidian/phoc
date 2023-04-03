/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

#include <wlr/types/wlr_xdg_shell.h>

G_BEGIN_DECLS

typedef struct phoc_xdg_popup PhocXdgPopup;
typedef struct phoc_xdg_toplevel_decoration PhocXdgToplevelDecoration;

PhocXdgPopup      *phoc_xdg_popup_create           (PhocView             *view,
                                                    struct wlr_xdg_popup *wlr_popup);
void               phoc_xdg_surface_set_decoration (PhocXdgSurface            *self,
                                                    PhocXdgToplevelDecoration *decoration);
PhocXdgToplevelDecoration *
                   phoc_xdg_surface_get_decoration (PhocXdgSurface        *self);
struct wlr_xdg_surface *
                   phoc_xdg_surface_get_wlr_xdg_surface (PhocXdgSurface   *self);

G_END_DECLS
