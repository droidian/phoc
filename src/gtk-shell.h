/*
 * Copyright (C) 2019 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "desktop.h"
#include <wlr/types/wlr_surface.h>
#include <wayland-server-core.h>

#pragma once

G_BEGIN_DECLS

typedef struct _PhocGtkShell PhocGtkShell;
typedef struct _PhocGtkSurface PhocGtkSurface;

typedef struct _PhocDesktop PhocDesktop;
PhocGtkShell   *phoc_gtk_shell_create                           (PhocDesktop        *desktop,
                                                                 struct wl_display  *display);
void            phoc_gtk_shell_destroy                          (PhocGtkShell       *gtk_shell);
PhocGtkSurface *phoc_gtk_shell_get_gtk_surface_from_wlr_surface (PhocGtkShell       *self,
                                                                 struct wlr_surface *wlr_surface);
PhocGtkShell   *phoc_gtk_shell_from_resource                    (struct wl_resource *resource);

PhocGtkSurface *phoc_gtk_surface_from_resource                  (struct wl_resource *resource);
const char     *phoc_gtk_surface_get_app_id                     (PhocGtkSurface     *gtk_surface);

G_END_DECLS
