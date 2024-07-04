/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "xdg-surface.h"
#include "xdg-toplevel-decoration.h"

#include <glib-object.h>

G_BEGIN_DECLS

void               phoc_xdg_surface_set_decoration (PhocXdgSurface            *self,
                                                    PhocXdgToplevelDecoration *decoration);
PhocXdgToplevelDecoration *
                   phoc_xdg_surface_get_decoration (PhocXdgSurface        *self);
struct wlr_xdg_surface *
                   phoc_xdg_surface_get_wlr_xdg_surface (PhocXdgSurface   *self);

G_END_DECLS
