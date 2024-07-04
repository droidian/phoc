/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "keybindings.h"

#include <phoc-layer-shell-effects-unstable-v1-protocol.h>
#include "layers.h"
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PhocDraggableLayerSurface:
 *
 * A draggable layer surface.
 */
typedef struct _PhocDraggableLayerSurface PhocDraggableLayerSurface;

/**
 * PhocAlphaLayerSurface:
 *
 * A layer surface with translusency.
 */
typedef struct _PhocAlphaLayerSurface PhocAlphaLayerSurface;

/**
 * PhocStackedLayerSurface:
 *
 * Layer surface stacking
 */
typedef struct _PhocStackedLayerSurface PhocStackedLayerSurface;

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

typedef enum {
  PHOC_STACKED_SURFACE_STACK_BELOW,
  PHOC_STACKED_SURFACE_STACK_ABOVE,
} PhocStackedSurfacePos;

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
struct wl_global      *phoc_layer_shell_effects_get_global      (PhocLayerShellEffects *self);

/* Drag */
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
                                                                   PhocAnimDir                 anim_dir);
gboolean                 phoc_draggable_layer_surface_fling       (PhocDraggableLayerSurface  *drag_surface,
                                                                   double                      lx,
                                                                   double                      ly,
                                                                   double                      vx,
                                                                   double                      vy);

PhocDraggableSurfaceState phoc_draggable_layer_surface_get_state (PhocDraggableLayerSurface *drag_surface);
gboolean                  phoc_draggable_layer_surface_is_unfolded (PhocDraggableLayerSurface *drag_surface);

/* Alpha */
PhocLayerSurface         *phoc_alpha_layer_surface_get_layer_surface (PhocAlphaLayerSurface *alpha_surface);

/* Stacking */
GSList                   *phoc_layer_shell_effects_get_layer_surface_stacks (PhocLayerShellEffects *self);
enum zwlr_layer_shell_v1_layer
                          phoc_stacked_layer_surface_get_layer (PhocStackedLayerSurface *stacked_surface);
PhocLayerSurface         *phoc_stacked_layer_surface_get_layer_surface (PhocStackedLayerSurface *stacked_surface);
PhocLayerSurface         *phoc_stacked_layer_surface_get_target_layer_surface (PhocStackedLayerSurface *stacked_surface);
PhocStackedSurfacePos     phoc_stacked_layer_surface_get_position (PhocStackedLayerSurface *stacked_surface);

G_END_DECLS
