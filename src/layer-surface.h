/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "output.h"

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
/* TODO: we keep the struct public for now due to the list links and
   notifiers but we should avoid other member access */
struct _PhocLayerSurface {
  GObject parent;

  struct wlr_layer_surface_v1 *layer_surface;
  struct wl_list link; // PhocOutput::layer_surfaces

  struct wl_listener destroy;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener surface_commit;
  struct wl_listener output_destroy;
  struct wl_listener new_popup;
  struct wl_listener new_subsurface;
  struct wl_list subsurfaces; // phoc_layer_subsurface::link

  struct wlr_box geo;
  enum zwlr_layer_shell_v1_layer layer;
  float alpha;
  bool mapped;
};

PhocLayerSurface *phoc_layer_surface_new (struct wlr_layer_surface_v1 *layer_surface);
void              phoc_layer_surface_damage (PhocLayerSurface *self);
const char       *phoc_layer_surface_get_namespace (PhocLayerSurface *self);
PhocOutput       *phoc_layer_surface_get_output (PhocLayerSurface *self);
void              phoc_layer_surface_set_alpha (PhocLayerSurface *self, float alpha);
float             phoc_layer_surface_get_alpha (PhocLayerSurface *self);
enum zwlr_layer_shell_v1_layer
                  phoc_layer_surface_get_layer (PhocLayerSurface *self);

G_END_DECLS
