/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <wlr/types/wlr_layer_shell_v1.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_LAYER_SURFACE (phoc_layer_surface_get_type ())

G_DECLARE_FINAL_TYPE (PhocLayerSurface, phoc_layer_surface, PHOC, LAYER_SURFACE, GObject)

/**
 * PhocLayerSurface:
 *
 * A Layer surface backed by the wlr-layer-surface wayland protocol.
 *
 * For details on how to setup a layer surface see `handle_layer_shell_surface`.
 */
struct _PhocLayerSurface {
    GObject parent;

    struct wlr_layer_surface_v1 *layer_surface;
    struct wl_list link;

    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
    struct wl_listener output_destroy;
    struct wl_listener new_popup;
    struct wl_listener new_subsurface;
    struct wl_list subsurfaces; // roots_layer_subsurface::link

    struct wlr_box geo;
    enum zwlr_layer_shell_v1_layer layer;
};

PhocLayerSurface *phoc_layer_surface_new (void);
void              phoc_layer_surface_unmap (PhocLayerSurface *self);

G_END_DECLS
