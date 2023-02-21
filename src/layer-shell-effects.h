/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "keybindings.h"

#include <phoc-layer-shell-effects-unstable-v1-protocol.h>
#include "layers.h"
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PhocDraggableSurface:
 *
 * A dragable layer surface.
 */
typedef struct _PhocDraggableLayerSurface PhocDraggableLayerSurface;

typedef struct _PhocAlphaLayerSurface PhocAlphaLayerSurface;

/**
 * PhocDraggableSurfaceState:
 *
 * Drag states of a draggable surface (e.g. a [type@PhocDraggableLayerSurface].
 * TODO: Reuse when xdg surface become draggable too.
 */
typedef enum {
  PHOC_DRAGGABLE_SURFACE_STATE_NONE,
  PHOC_DRAGGABLE_SURFACE_STATE_PENDING,
  PHOC_DRAGGABLE_SURFACE_STATE_DRAGGING,
  PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING,
  PHOC_DRAGGABLE_SURFACE_STATE_REJECTED,
} PhocDraggableSurfaceState;

#define PHOC_TYPE_LAYER_SHELL_EFFECTS (phoc_layer_shell_effects_get_type ())

G_DECLARE_FINAL_TYPE (PhocLayerShellEffects, phoc_layer_shell_effects, PHOC, LAYER_SHELL_EFFECTS, GObject)

typedef enum _AnimDir {
  ANIM_DIR_IN = 0,
  ANIM_DIR_OUT,
} PhocAnimDir;

PhocLayerShellEffects *phoc_layer_shell_effects_new (void);
void                   phoc_layer_shell_effects_send_drag_start (PhocLayerShellEffects *self,
                                                                 PhocLayerSurface      *surface);
void                   phoc_layer_shell_effects_send_drag_end   (PhocLayerShellEffects *self,
                                                                 PhocLayerSurface      *surface,
                                                                 int                    state);


PhocDraggableLayerSurface *phoc_layer_shell_effects_get_draggable_layer_surface_from_layer_surface (
  PhocLayerShellEffects *self, PhocLayerSurface *layer_surface);

PhocLayerSurface        *phoc_draggable_layer_surface_get_layer_surface (PhocDraggableLayerSurface *drag_surface);
PhocDraggableSurfaceState phoc_draggable_layer_surface_drag_start  (PhocDraggableLayerSurface *drag_surface,
                                                                    double                     lx,
                                                                    double                     ly);
PhocDraggableSurfaceState phoc_draggable_layer_surface_drag_update (PhocDraggableLayerSurface *drag_surface,
                                                                    double                     lx,
                                                                    double                     ly);
void                     phoc_draggable_layer_surface_drag_end    (PhocDraggableLayerSurface  *drag_surface,
                                                                   double                      lx,
                                                                   double                      ly);
void                     phoc_draggable_layer_surface_slide       (PhocDraggableLayerSurface  *drag_surface,
                                                                   PhocAnimDir             anim_dir);

PhocDraggableSurfaceState phoc_draggable_layer_surface_get_state (PhocDraggableLayerSurface *drag_surface);
gboolean                  phoc_draggable_layer_surface_is_unfolded (PhocDraggableLayerSurface *drag_surface);


PhocLayerSurface         *phoc_alpha_layer_surface_get_layer_surface (PhocAlphaLayerSurface *alpha_surface);

PhocAlphaLayerSurface    *phoc_layer_shell_effects_get_alpha_layer_surface_from_layer_surface (PhocLayerShellEffects *self,
                                                                                               PhocLayerSurface *layer_surface);

G_END_DECLS
