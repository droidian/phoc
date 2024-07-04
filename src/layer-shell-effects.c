/*
 * Copyright (C) 2022 Purism SPC
 *               2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-layer-shell-effects"

#include "phoc-config.h"
#include "layers.h"
#include "layer-shell-effects.h"
#include "phoc-animation.h"
#include "phoc-enums.h"
#include "server.h"
#include "utils.h"

#include <glib-object.h>

#define LAYER_SHELL_EFFECTS_VERSION 3
#define DRAG_ACCEPT_THRESHOLD_DISTANCE 16
#define DRAG_REJECT_THRESHOLD_DISTANCE 24
#define SLIDE_ANIM_DURATION_MS 300 /* ms */
#define FLING_V_MIN 1500

typedef enum {
  PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP = (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                           ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                           ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP),
  PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM = (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                              ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM),
  PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT = (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT),
  PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT = (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)
} PhocLayerShellEffectDrags;


typedef struct _PhocDraggableLayerSurfaceParams {
  /* Margin when folded / unfolded */
  int32_t  folded, unfolded;
  /* Height of exclusive area */
  uint32_t exclusive;
  /* When is the surface pulled out [0.0, 1.0] */
  double   threshold;

  enum zphoc_draggable_layer_surface_v1_drag_mode drag_mode;
  uint32_t drag_handle;
} PhocDraggableLayerSurfaceParams;

struct _PhocDraggableLayerSurface {
  struct wl_resource *resource;
  PhocLayerSurface *layer_surface;
  PhocLayerShellEffects *layer_shell_effects;

  /* Double buffered params set by the client */
  PhocDraggableLayerSurfaceParams current;
  PhocDraggableLayerSurfaceParams pending;

  PhocDraggableSurfaceState state;
  struct {
    /* Margin at gesture start */
    int      start_margin;
    /* Threshold until drag is accepted */
    int      pending_accept;
    /* Threshold until drag is rejected */
    int      pending_reject;
    /* Slide in/out animation */
    guint    anim_id;
    float    anim_t;
    int32_t  anim_duration;
    int32_t  anim_start;
    int32_t  anim_end;
    PhocAnimDir anim_dir;
    enum zphoc_draggable_layer_surface_v1_drag_end_state last_state;
  } drag;
  struct wlr_box geo;

  struct wl_listener surface_handle_commit;
  struct wl_listener layer_surface_handle_destroy;
};

struct _PhocAlphaLayerSurface {
  struct wl_resource *resource;
  PhocLayerSurface *layer_surface;
  PhocLayerShellEffects *layer_shell_effects;

  /* Double buffered alpha set by the client */
  float current;
  float pending;

  struct wl_listener surface_handle_commit;
  struct wl_listener layer_surface_handle_destroy;
};


struct _PhocStackedLayerSurface {
  struct wl_resource *resource;
  PhocLayerSurface *layer_surface;
  PhocLayerShellEffects *layer_shell_effects;

  /* Double buffered stack target */
  struct {
    PhocLayerSurface      *surface;
    PhocStackedSurfacePos  position;
  } pending, current;

  struct wl_listener surface_handle_commit;
  struct wl_listener layer_surface_handle_destroy;
  struct wl_listener target_layer_surface_handle_destroy;
};


/**
 * PhocLayerShellEffects:
 *
 * Additional effects for layer surfaces.
 */
struct _PhocLayerShellEffects {
  GObject             parent;

  struct wl_global   *global;
  GSList             *resources;

  GSList             *drag_surfaces;
  GHashTable         *drag_surfaces_by_layer_surface;

  GSList             *alpha_surfaces;

  GSList             *stacked_surfaces;
};

G_DEFINE_TYPE (PhocLayerShellEffects, phoc_layer_shell_effects, G_TYPE_OBJECT)

static PhocLayerShellEffects     *phoc_layer_shell_effects_from_resource     (struct wl_resource *resource);
static PhocDraggableLayerSurface *phoc_draggable_layer_surface_from_resource (struct wl_resource *resource);
static PhocAlphaLayerSurface     *phoc_alpha_layer_surface_from_resource     (struct wl_resource *resource);
static PhocStackedLayerSurface   *phoc_stacked_layer_surface_from_resource   (struct wl_resource *resource);

static void
resource_handle_destroy(struct wl_client *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}


static void
handle_draggable_layer_surface_set_margins (struct wl_client   *client,
                                            struct wl_resource *resource,
                                            int32_t             margin_folded,
                                            int32_t             margin_unfolded)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);

  g_assert (drag_surface);

  g_debug ("Draggable Layer surface margins for %p: %d,%d", drag_surface,
           margin_folded, margin_unfolded);

  if (drag_surface->layer_surface == NULL)
    return;

  if (margin_unfolded <= margin_folded) {
    wl_resource_post_error (resource, ZPHOC_LAYER_SHELL_EFFECTS_V1_ERROR_BAD_MARGIN,
                            "unfolded margin (%d) <= folded marign (%d)",
                            margin_unfolded, margin_folded);
  }

  switch (drag_surface->layer_surface->layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    G_GNUC_FALLTHROUGH;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    G_GNUC_FALLTHROUGH;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    G_GNUC_FALLTHROUGH;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    break;
  default:
    wl_resource_post_error (resource, ZPHOC_LAYER_SHELL_EFFECTS_V1_ERROR_BAD_ANCHORS,
                            "Surface not anchored to three edges");
    return;
  }

  drag_surface->pending.folded = margin_folded;
  drag_surface->pending.unfolded = margin_unfolded;
}


static void
handle_draggable_layer_surface_set_exclusive (struct wl_client   *client,
                                              struct wl_resource *resource,
                                              uint32_t            exclusive)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);

  g_assert (drag_surface);
  g_debug ("Draggable Layer surface exclusive for %p: %u", drag_surface, exclusive);
  if (drag_surface->layer_surface == NULL)
    return;

  drag_surface->pending.exclusive = exclusive;
}


static void
handle_draggable_layer_surface_set_threshold (struct wl_client   *client,
                                              struct wl_resource *resource,
                                              wl_fixed_t          threshold_f)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);
  double threshold;

  g_assert (drag_surface);
  threshold = wl_fixed_to_double (threshold_f);
  g_debug ("Draggable Layer surface threshold for %p: %f", drag_surface, threshold);
  if (drag_surface->layer_surface == NULL)
    return;

  threshold = MIN (threshold, 1.0);
  threshold = MAX (threshold, 0.0);

  drag_surface->pending.threshold = threshold;
}


static void
handle_draggable_layer_surface_set_drag_mode (struct wl_client   *client,
                                              struct wl_resource *resource,
                                              uint32_t            drag_mode)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);

  g_assert (drag_surface);
  g_debug ("Draggable Layer surface drag-mode for %p: %d", drag_surface, drag_mode);
  if (drag_surface->layer_surface == NULL)
    return;

  drag_surface->pending.drag_mode = drag_mode;
}


static void
handle_draggable_layer_surface_set_drag_handle (struct wl_client   *client,
                                                struct wl_resource *resource,
                                                uint32_t            drag_handle)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);

  g_assert (drag_surface);
  g_debug ("Draggable Layer surface drag-handle for %p: %d", drag_surface, drag_handle);
  if (drag_surface->layer_surface == NULL)
    return;

  drag_surface->pending.drag_handle = drag_handle;
}


static void
handle_draggable_layer_surface_set_state (struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t state)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);
  PhocAnimDir dir;

  g_assert (drag_surface);
  if (drag_surface->layer_surface == NULL)
    return;

  switch (state) {
  case ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED:
    dir = ANIM_DIR_IN;
    break;
  case ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_UNFOLDED:
    dir = ANIM_DIR_OUT;
    break;
  default:
    g_warning ("Drag surface %p: Ignoring invalid drag state: %d", drag_surface, state);
    return;
  }

  g_debug ("Sliding %p: %d", drag_surface, dir);
  phoc_draggable_layer_surface_slide (drag_surface, dir);
}


static const struct zphoc_draggable_layer_surface_v1_interface draggable_layer_surface_v1_impl = {
  .set_margins = handle_draggable_layer_surface_set_margins,
  .set_exclusive = handle_draggable_layer_surface_set_exclusive,
  .set_threshold = handle_draggable_layer_surface_set_threshold,
  .set_drag_mode = handle_draggable_layer_surface_set_drag_mode,
  .set_drag_handle = handle_draggable_layer_surface_set_drag_handle,
  .set_state = handle_draggable_layer_surface_set_state,
  .destroy = resource_handle_destroy,
};



static void
handle_alpha_layer_surface_set_alpha (struct wl_client   *client,
                                      struct wl_resource *resource,
                                      wl_fixed_t          alpha_f)
{
  PhocAlphaLayerSurface *alpha_surface = wl_resource_get_user_data (resource);
  double alpha;

  g_assert (alpha_surface);
  alpha = wl_fixed_to_double (alpha_f);

  g_debug ("Alpha Layer surface alpha for %p: alpha: %f", alpha_surface, alpha);

  if (alpha_surface->layer_surface == NULL)
    return;

  alpha_surface->pending = alpha;
}



static const struct zphoc_alpha_layer_surface_v1_interface alpha_layer_surface_v1_impl = {
  .set_alpha = handle_alpha_layer_surface_set_alpha,
  .destroy = resource_handle_destroy,
};


static void
handle_stacked_layer_surface_stack_above (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          struct wl_resource *surface_resource)
{
  PhocStackedLayerSurface *stacked_surface = wl_resource_get_user_data (resource);
  struct wlr_layer_surface_v1 *wlr_layer_surface;

  g_assert (stacked_surface);

  if (!stacked_surface->layer_surface)
    return;

  wlr_layer_surface = wlr_layer_surface_v1_from_resource (surface_resource);
  if (!wlr_layer_surface) {
    wl_resource_post_error (resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_STACK_ERROR_INVALID_SURFACE,
                            "Layer surface not yet committed");
  }

  if (!wlr_layer_surface->data) {
    wl_resource_post_error (resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_STACK_ERROR_INVALID_SURFACE,
                            "Layer surface not yet committed");
  }

  stacked_surface->pending.surface = PHOC_LAYER_SURFACE (wlr_layer_surface->data);
  stacked_surface->pending.position = PHOC_STACKED_SURFACE_STACK_ABOVE;
}


static void
handle_stacked_layer_surface_stack_below (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          struct wl_resource *surface_resource)
{
  PhocStackedLayerSurface *stacked_surface = wl_resource_get_user_data (resource);
  struct wlr_layer_surface_v1 *wlr_layer_surface;

  g_assert (stacked_surface);

  if (!stacked_surface->layer_surface)
    return;

  wlr_layer_surface = wlr_layer_surface_v1_from_resource (surface_resource);
  if (!wlr_layer_surface) {
    wl_resource_post_error (resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_STACK_ERROR_INVALID_SURFACE,
                            "Layer surface not yet committed");
  }

  if (!wlr_layer_surface->data) {
    wl_resource_post_error (resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_STACK_ERROR_INVALID_SURFACE,
                            "Layer surface not yet committed");
  }

  stacked_surface->pending.surface = PHOC_LAYER_SURFACE (wlr_layer_surface->data);
  stacked_surface->pending.position = PHOC_STACKED_SURFACE_STACK_BELOW;
}


static const struct zphoc_stacked_layer_surface_v1_interface stacked_layer_surface_v1_impl = {
  .stack_above = handle_stacked_layer_surface_stack_above,
  .stack_below = handle_stacked_layer_surface_stack_below,
  .destroy = resource_handle_destroy,
};


static PhocDraggableLayerSurface *
phoc_draggable_layer_surface_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_draggable_layer_surface_v1_interface,
                                     &draggable_layer_surface_v1_impl));
  return wl_resource_get_user_data (resource);
}


static PhocAlphaLayerSurface *
phoc_alpha_layer_surface_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_alpha_layer_surface_v1_interface,
                                     &alpha_layer_surface_v1_impl));
  return wl_resource_get_user_data (resource);
}


static PhocStackedLayerSurface *
phoc_stacked_layer_surface_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_stacked_layer_surface_v1_interface,
                                     &stacked_layer_surface_v1_impl));
  return wl_resource_get_user_data (resource);
}



static void
phoc_draggable_layer_surface_destroy (PhocDraggableLayerSurface *drag_surface)
{
  PhocLayerShellEffects *layer_shell_effects;

  if (drag_surface == NULL)
    return;

  g_debug ("Destroying draggable_layer_surface %p (res %p)", drag_surface, drag_surface->resource);
  layer_shell_effects = PHOC_LAYER_SHELL_EFFECTS (drag_surface->layer_shell_effects);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (layer_shell_effects));

  /* End any ongoing animations */
  if (drag_surface->drag.anim_id && drag_surface->layer_surface) {
    phoc_animatable_remove_frame_callback (PHOC_ANIMATABLE (drag_surface->layer_surface),
                                           drag_surface->drag.anim_id);
  }

  if (drag_surface->layer_surface) {
    g_hash_table_remove (layer_shell_effects->drag_surfaces_by_layer_surface,
                         drag_surface->layer_surface);
    /* wlr signals */
    wl_list_remove (&drag_surface->surface_handle_commit.link);
    wl_list_remove (&drag_surface->layer_surface_handle_destroy.link);
  }
  layer_shell_effects->drag_surfaces = g_slist_remove (layer_shell_effects->drag_surfaces,
                                                       drag_surface);

  drag_surface->layer_surface = NULL;
  wl_resource_set_user_data (drag_surface->resource, NULL);
  g_free (drag_surface);
}


static void
phoc_alpha_layer_surface_destroy (PhocAlphaLayerSurface *alpha_surface)
{
  PhocLayerShellEffects *layer_shell_effects;

  if (alpha_surface == NULL)
    return;

  g_debug ("Destroying alpha_layer_surface %p (res %p)", alpha_surface, alpha_surface->resource);
  layer_shell_effects = PHOC_LAYER_SHELL_EFFECTS (alpha_surface->layer_shell_effects);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (layer_shell_effects));

  if (alpha_surface->layer_surface) {
    /* wlr signals */
    wl_list_remove (&alpha_surface->surface_handle_commit.link);
    wl_list_remove (&alpha_surface->layer_surface_handle_destroy.link);
  }
  layer_shell_effects->alpha_surfaces = g_slist_remove (layer_shell_effects->alpha_surfaces,
                                                        alpha_surface);

  alpha_surface->layer_surface = NULL;
  wl_resource_set_user_data (alpha_surface->resource, NULL);
  g_free (alpha_surface);
}


static void
phoc_stacked_layer_surface_destroy (PhocStackedLayerSurface *stacked_surface)
{
  PhocLayerShellEffects *layer_shell_effects;
  PhocLayerSurface *layer_surface;

  if (stacked_surface == NULL)
    return;

  g_debug ("Destroying stacked_layer_surface %p (res %p)", stacked_surface, stacked_surface->resource);
  layer_shell_effects = PHOC_LAYER_SHELL_EFFECTS (stacked_surface->layer_shell_effects);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (layer_shell_effects));

  layer_surface = stacked_surface->layer_surface;
  if (layer_surface) {
      PhocOutput *output;

    /* wlr signals */
    wl_list_remove (&stacked_surface->surface_handle_commit.link);
    wl_list_remove (&stacked_surface->layer_surface_handle_destroy.link);

    output = phoc_layer_surface_get_output (layer_surface);
    if (output) {
      enum zwlr_layer_shell_v1_layer layer = phoc_layer_surface_get_layer (layer_surface);

      phoc_output_set_layer_dirty (output, layer);
    }
  }

  if (stacked_surface->current.surface)
    wl_list_remove (&stacked_surface->target_layer_surface_handle_destroy.link);

  layer_shell_effects->stacked_surfaces = g_slist_remove (layer_shell_effects->stacked_surfaces,
                                                          stacked_surface);

  stacked_surface->layer_surface = NULL;
  stacked_surface->current.surface = NULL;

  wl_resource_set_user_data (stacked_surface->resource, NULL);
  g_free (stacked_surface);
}


static void
draggable_layer_surface_handle_resource_destroy (struct wl_resource *resource)
{
  PhocDraggableLayerSurface *drag_surface = phoc_draggable_layer_surface_from_resource (resource);

  phoc_draggable_layer_surface_destroy (drag_surface);
}


static void
alpha_layer_surface_handle_resource_destroy (struct wl_resource *resource)
{
  PhocAlphaLayerSurface *alpha_surface = phoc_alpha_layer_surface_from_resource (resource);

  phoc_alpha_layer_surface_destroy (alpha_surface);
}


static void
stacked_layer_surface_handle_resource_destroy (struct wl_resource *resource)
{
  PhocStackedLayerSurface *stacked_surface = phoc_stacked_layer_surface_from_resource (resource);

  phoc_stacked_layer_surface_destroy (stacked_surface);
}


static void
alpha_layer_surface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocAlphaLayerSurface *alpha_surface = wl_container_of(listener, alpha_surface, layer_surface_handle_destroy);

  /* The layer-surface is unusable for us now */
  alpha_surface->layer_surface = NULL;
}


static void
draggable_layer_surface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocLayerShellEffects *layer_shell_effects;
  PhocDraggableLayerSurface *drag_surface =
    wl_container_of(listener, drag_surface, layer_surface_handle_destroy);

  /* Drop the gone layer-surface from the layer-surface -> drag-surface mapping */
  layer_shell_effects = PHOC_LAYER_SHELL_EFFECTS (drag_surface->layer_shell_effects);
  g_hash_table_remove (layer_shell_effects->drag_surfaces_by_layer_surface,
                       drag_surface->layer_surface);

  /* The layer-surface is unusable for us now */
  drag_surface->layer_surface = NULL;
}


static void
stacked_layer_surface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocStackedLayerSurface *stacked_surface;

  stacked_surface = wl_container_of (listener, stacked_surface, layer_surface_handle_destroy);

  /* The layer-surface is unusable for us now */
  stacked_surface->layer_surface = NULL;

  /* No need to invalidate the output layer as the layer surface's destroy handler does so */
}


static void
stacked_target_layer_surface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocStackedLayerSurface *stacked_surface;

  stacked_surface = wl_container_of (listener, stacked_surface, target_layer_surface_handle_destroy);

  /* The layer-surface is unusable for us now */
  stacked_surface->current.surface = NULL;

  /* No need to invalidate the output layer cache, the layer surface's destroy handler does so */
}


static void
draggable_layer_surface_handle_commit (struct wl_listener *listener, void *data)
{
  PhocDraggableLayerSurface *drag_surface =
    wl_container_of(listener, drag_surface, surface_handle_commit);
  PhocLayerSurface *layer_surface = drag_surface->layer_surface;
  gboolean changed = FALSE;

  if (layer_surface == NULL)
    return;

  if (drag_surface->current.folded != drag_surface->pending.folded) {
    drag_surface->current.folded = drag_surface->pending.folded;
    changed = TRUE;
  }
  drag_surface->current.unfolded = drag_surface->pending.unfolded;
  drag_surface->current.exclusive = drag_surface->pending.exclusive;
  drag_surface->current.threshold = drag_surface->pending.threshold;
  drag_surface->current.drag_mode = drag_surface->pending.drag_mode;
  drag_surface->current.drag_handle = drag_surface->pending.drag_handle;

  /* Update animation end in case it's ongoing to compensate for size changes */
  if (changed && drag_surface->drag.anim_dir == ANIM_DIR_IN) {
    drag_surface->drag.anim_end = drag_surface->current.folded;
    phoc_draggable_layer_surface_slide (drag_surface, ANIM_DIR_IN);
  }

  /* TODO: cancel related gestures on drag mode changes */

  /* Keep in sync with layer surface geometry changes */
  if (memcmp (&drag_surface->geo, &layer_surface->geo, sizeof (drag_surface->geo)) == 0)
    return;

  g_debug ("Geometry changed %d,%d %dx%d",
           layer_surface->geo.x, layer_surface->geo.y,
           layer_surface->geo.width, layer_surface->geo.height);
  drag_surface->geo = layer_surface->geo;
}


static void
alpha_surface_handle_commit (struct wl_listener *listener, void *data)
{
  PhocAlphaLayerSurface *alpha_surface =
    wl_container_of(listener, alpha_surface, surface_handle_commit);
  PhocLayerSurface *layer_surface = alpha_surface->layer_surface;
  PhocOutput *output;

  if (layer_surface == NULL)
    return;

  if (alpha_surface->current == alpha_surface->pending)
    return;

  alpha_surface->current  = alpha_surface->pending;
  phoc_layer_surface_set_alpha (layer_surface, alpha_surface->current);

  output = phoc_layer_surface_get_output (layer_surface);
  phoc_output_damage_whole_surface (output,
                                    layer_surface->layer_surface->surface,
                                    layer_surface->geo.x,
                                    layer_surface->geo.y);
}


static void
stacked_surface_handle_commit (struct wl_listener *listener, void *data)
{
  PhocStackedLayerSurface *stacked_surface =
    wl_container_of (listener, stacked_surface, surface_handle_commit);
  PhocOutput *output;

  /* stacked surface already inert */
  if (stacked_surface->layer_surface == NULL)
    return;

  if (memcmp (&stacked_surface->current,
              &stacked_surface->pending,
              sizeof (stacked_surface->current)) == 0) {
    return;
  }

  if (phoc_layer_surface_get_layer (stacked_surface->pending.surface) !=
      phoc_layer_surface_get_layer (stacked_surface->layer_surface)) {
    wl_resource_post_error (stacked_surface->resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_STACK_ERROR_INVALID_LAYER,
                            "Layer surface not on same layer");
    return;
  }

  if (phoc_layer_surface_get_output (stacked_surface->pending.surface) !=
      phoc_layer_surface_get_output (stacked_surface->layer_surface)) {
    wl_resource_post_error (stacked_surface->resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_STACK_ERROR_INVALID_OUTPUT,
                            "Layer surface not on same output");
    return;
  }

  if (stacked_surface->current.surface) {
    /* Remove listeners for current target surface */
    wl_list_remove (&stacked_surface->target_layer_surface_handle_destroy.link);
  }

  stacked_surface->current = stacked_surface->pending;

  /* Connect destroy listener for current target surface */
  stacked_surface->target_layer_surface_handle_destroy.notify = stacked_target_layer_surface_handle_destroy;
  wl_signal_add (&stacked_surface->current.surface->layer_surface->events.destroy,
                 &stacked_surface->target_layer_surface_handle_destroy);

  output = phoc_layer_surface_get_output (stacked_surface->layer_surface);
  phoc_output_set_layer_dirty (output,
                               phoc_layer_surface_get_layer (stacked_surface->current.surface));
}


static void
handle_get_draggable_layer_surface (struct wl_client   *client,
                                    struct wl_resource *layer_shell_effects_resource,
                                    uint32_t            id,
                                    struct wl_resource *layer_surface_resource)
{
  PhocLayerShellEffects *self;
  g_autofree PhocDraggableLayerSurface *drag_surface = NULL;
  struct wlr_surface *wlr_surface;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  int version;

  self = phoc_layer_shell_effects_from_resource (layer_shell_effects_resource);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));
  wlr_layer_surface = wlr_layer_surface_v1_from_resource (layer_surface_resource);
  wlr_surface = wlr_layer_surface->surface;
  g_assert (wlr_surface);

  drag_surface = g_new0 (PhocDraggableLayerSurface, 1);

  version = wl_resource_get_version (layer_shell_effects_resource);
  drag_surface->layer_shell_effects = self;
  drag_surface->resource = wl_resource_create (client,
                                               &zphoc_draggable_layer_surface_v1_interface,
                                               version,
                                               id);
  if (drag_surface->resource == NULL) {
    g_free (drag_surface);
    wl_client_post_no_memory(client);
    return;
  }

  g_debug ("New draggable layer_surface %p (res %p)", drag_surface, drag_surface->resource);
  wl_resource_set_implementation (drag_surface->resource,
                                  &draggable_layer_surface_v1_impl,
                                  drag_surface,
                                  draggable_layer_surface_handle_resource_destroy);

  if (!wlr_layer_surface->data) {
    wl_resource_post_error (layer_shell_effects_resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_ERROR_BAD_SURFACE,
                            "Layer surface not yet committed");
    return;
  }

  g_assert (PHOC_IS_LAYER_SURFACE (wlr_layer_surface->data));
  drag_surface->layer_surface = PHOC_LAYER_SURFACE (wlr_layer_surface->data);

  drag_surface->surface_handle_commit.notify = draggable_layer_surface_handle_commit;
  wl_signal_add (&wlr_surface->events.commit, &drag_surface->surface_handle_commit);

  drag_surface->layer_surface_handle_destroy.notify = draggable_layer_surface_handle_destroy;
  wl_signal_add (&wlr_layer_surface->events.destroy, &drag_surface->layer_surface_handle_destroy);

  g_hash_table_insert (self->drag_surfaces_by_layer_surface,
                       drag_surface->layer_surface, drag_surface);
  self->drag_surfaces = g_slist_prepend (self->drag_surfaces, g_steal_pointer (&drag_surface));
}


static void
handle_get_alpha_layer_surface (struct wl_client   *client,
                                struct wl_resource *layer_shell_effects_resource,
                                uint32_t            id,
                                struct wl_resource *layer_surface_resource)
{
  PhocLayerShellEffects *self;
  g_autofree PhocAlphaLayerSurface *alpha_surface = NULL;
  struct wlr_surface *wlr_surface;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  int version;

  self = phoc_layer_shell_effects_from_resource (layer_shell_effects_resource);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));
  wlr_layer_surface = wlr_layer_surface_v1_from_resource (layer_surface_resource);
  wlr_surface = wlr_layer_surface->surface;
  g_assert (wlr_surface);

  alpha_surface = g_new0 (PhocAlphaLayerSurface, 1);

  version = wl_resource_get_version (layer_shell_effects_resource);
  alpha_surface->layer_shell_effects = self;
  alpha_surface->resource = wl_resource_create (client,
                                                &zphoc_alpha_layer_surface_v1_interface,
                                                version,
                                                id);
  if (alpha_surface->resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  g_debug ("New alpha layer_surface %p (res %p)", alpha_surface, alpha_surface->resource);
  wl_resource_set_implementation (alpha_surface->resource,
                                  &alpha_layer_surface_v1_impl,
                                  alpha_surface,
                                  alpha_layer_surface_handle_resource_destroy);

  if (!wlr_layer_surface->data) {
    wl_resource_post_error (layer_shell_effects_resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_ERROR_BAD_SURFACE,
                            "Layer surface not yet committed");
    return;
  }

  g_assert (PHOC_IS_LAYER_SURFACE (wlr_layer_surface->data));
  alpha_surface->layer_surface = PHOC_LAYER_SURFACE (wlr_layer_surface->data);

  alpha_surface->surface_handle_commit.notify = alpha_surface_handle_commit;
  wl_signal_add (&wlr_surface->events.commit, &alpha_surface->surface_handle_commit);

  alpha_surface->layer_surface_handle_destroy.notify = alpha_layer_surface_handle_destroy;
  wl_signal_add (&wlr_layer_surface->events.destroy, &alpha_surface->layer_surface_handle_destroy);

  self->alpha_surfaces = g_slist_prepend (self->alpha_surfaces, g_steal_pointer (&alpha_surface));
}


static void
handle_get_stacked_layer_surface (struct wl_client   *client,
                                  struct wl_resource *layer_shell_effects_resource,
                                  uint32_t            id,
                                  struct wl_resource *layer_surface_resource)
{
  PhocLayerShellEffects *self;
  g_autofree PhocStackedLayerSurface *stacked_surface = NULL;
  struct wlr_surface *wlr_surface;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  int version;

  self = phoc_layer_shell_effects_from_resource (layer_shell_effects_resource);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));
  wlr_layer_surface = wlr_layer_surface_v1_from_resource (layer_surface_resource);
  wlr_surface = wlr_layer_surface->surface;
  g_assert (wlr_surface);

  stacked_surface = g_new0 (PhocStackedLayerSurface, 1);

  version = wl_resource_get_version (layer_shell_effects_resource);
  stacked_surface->layer_shell_effects = self;
  stacked_surface->resource = wl_resource_create (client,
                                                 &zphoc_stacked_layer_surface_v1_interface,
                                                 version,
                                                 id);
  if (stacked_surface->resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  g_debug ("New stacked layer_surface %p (res %p)", stacked_surface, stacked_surface->resource);
  wl_resource_set_implementation (stacked_surface->resource,
                                  &stacked_layer_surface_v1_impl,
                                  stacked_surface,
                                  stacked_layer_surface_handle_resource_destroy);
  if (!wlr_layer_surface->data) {
    wl_resource_post_error (layer_shell_effects_resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_ERROR_BAD_SURFACE,
                            "Layer surface not yet committed");
    return;
  }

  g_assert (PHOC_IS_LAYER_SURFACE (wlr_layer_surface->data));

  stacked_surface->layer_surface = PHOC_LAYER_SURFACE (wlr_layer_surface->data);

  stacked_surface->surface_handle_commit.notify = stacked_surface_handle_commit;
  wl_signal_add (&wlr_surface->events.commit, &stacked_surface->surface_handle_commit);

  stacked_surface->layer_surface_handle_destroy.notify = stacked_layer_surface_handle_destroy;
  wl_signal_add (&wlr_layer_surface->events.destroy, &stacked_surface->layer_surface_handle_destroy);

  self->stacked_surfaces = g_slist_prepend (self->stacked_surfaces, g_steal_pointer (&stacked_surface));
}


static void
layer_shell_effects_handle_resource_destroy (struct wl_resource *resource)
{
  PhocLayerShellEffects *self = wl_resource_get_user_data (resource);

  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));

  g_debug ("Destroying layer_shell_effects %p (res %p)", self, resource);
  self->resources = g_slist_remove (self->resources, resource);
}


static const struct zphoc_layer_shell_effects_v1_interface layer_shell_effects_impl = {
  .destroy = resource_handle_destroy,
  .get_draggable_layer_surface = handle_get_draggable_layer_surface,
  .get_alpha_layer_surface = handle_get_alpha_layer_surface,
  .get_stacked_layer_surface = handle_get_stacked_layer_surface,
};


static void
layer_shell_effects_bind (struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
  PhocLayerShellEffects *self = PHOC_LAYER_SHELL_EFFECTS (data);
  struct wl_resource *resource  = wl_resource_create (client, &zphoc_layer_shell_effects_v1_interface,
                                                      version, id);

  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));

  wl_resource_set_implementation (resource,
                                  &layer_shell_effects_impl,
                                  self,
                                  layer_shell_effects_handle_resource_destroy);

  self->resources = g_slist_prepend (self->resources, resource);
  return;
}


static PhocLayerShellEffects *
phoc_layer_shell_effects_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_layer_shell_effects_v1_interface,
                                     &layer_shell_effects_impl));
  return wl_resource_get_user_data (resource);
}


static void
phoc_layer_shell_effects_finalize (GObject *object)
{
  PhocLayerShellEffects *self = PHOC_LAYER_SHELL_EFFECTS (object);

  g_clear_pointer (&self->drag_surfaces_by_layer_surface, g_hash_table_destroy);

  wl_global_destroy (self->global);

  G_OBJECT_CLASS (phoc_layer_shell_effects_parent_class)->finalize (object);
}


static void
phoc_layer_shell_effects_class_init (PhocLayerShellEffectsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phoc_layer_shell_effects_finalize;
}


static void
phoc_layer_shell_effects_init (PhocLayerShellEffects *self)
{
  struct wl_display *wl_display = phoc_server_get_wl_display (phoc_server_get_default ());

  self->drag_surfaces_by_layer_surface = g_hash_table_new (g_direct_hash, g_direct_equal);

  self->global = wl_global_create (wl_display, &zphoc_layer_shell_effects_v1_interface,
                                   LAYER_SHELL_EFFECTS_VERSION, self, layer_shell_effects_bind);

}


PhocLayerShellEffects *
phoc_layer_shell_effects_new (void)
{
  return g_object_new (PHOC_TYPE_LAYER_SHELL_EFFECTS, NULL);
}


struct wl_global *
phoc_layer_shell_effects_get_global (PhocLayerShellEffects *self)
{
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));

  return self->global;
}


static void
apply_margin (PhocDraggableLayerSurface *drag_surface, double margin)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;

  /* The client is not supposed to update margin or exclusive zone so
   * keep current and pending in sync */
  g_debug ("%s: margin: %f %f", __func__, drag_surface->drag.anim_t, margin);
  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    wlr_layer_surface->current.margin.top = (int32_t)margin;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    wlr_layer_surface->current.margin.bottom = (int32_t)margin;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    wlr_layer_surface->current.margin.left = (int32_t)margin;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    wlr_layer_surface->current.margin.right = (int32_t)margin;
    break;
  default:
    g_assert_not_reached ();
    break;
  }
  wlr_layer_surface->current.exclusive_zone = -margin + drag_surface->current.exclusive;

  wlr_layer_surface->pending.margin.top = wlr_layer_surface->current.margin.top;
  wlr_layer_surface->pending.margin.bottom = wlr_layer_surface->current.margin.bottom;
  wlr_layer_surface->pending.margin.left = wlr_layer_surface->current.margin.left;
  wlr_layer_surface->pending.margin.right = wlr_layer_surface->current.margin.right;
  wlr_layer_surface->pending.exclusive_zone = wlr_layer_surface->current.exclusive_zone;
}


static void
apply_state (PhocDraggableLayerSurface *drag_surface, PhocDraggableSurfaceState state)
{
  struct wlr_output *wlr_output;

  drag_surface->state = state;

  wlr_output = drag_surface->layer_surface->layer_surface->output;
  if (wlr_output) {
    PhocOutput *output = PHOC_OUTPUT (wlr_output->data);
    phoc_output_update_shell_reveal (output);
  }
}


static gboolean
on_output_frame_callback (PhocAnimatable *animatable, guint64 last_frame, gpointer user_data)

{
  PhocDraggableLayerSurface *drag_surface = user_data;
  PhocLayerSurface *layer_surface;
  PhocOutput *output;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  double margin, distance;
  bool done;

  g_assert (drag_surface);
  g_assert (PHOC_IS_LAYER_SURFACE (animatable));

  layer_surface = PHOC_LAYER_SURFACE (animatable);
  g_assert (layer_surface == drag_surface->layer_surface);

  g_assert (drag_surface->state == PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING);
  wlr_layer_surface = drag_surface->layer_surface->layer_surface;
  output = phoc_layer_surface_get_output (layer_surface);

  if (output == NULL)
    return G_SOURCE_REMOVE;

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = (int32_t)wlr_layer_surface->current.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = (int32_t)wlr_layer_surface->current.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = (int32_t)wlr_layer_surface->current.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = (int32_t)wlr_layer_surface->current.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  done = (drag_surface->drag.anim_dir == ANIM_DIR_IN && margin <= drag_surface->drag.anim_end) ||
    (drag_surface->drag.anim_dir == ANIM_DIR_OUT && margin >= drag_surface->drag.anim_end);

  if (done) {
    g_debug ("Ending animation for %p, margin: %f", drag_surface, margin);

    switch (drag_surface->drag.anim_dir) {
    case ANIM_DIR_IN:
      drag_surface->drag.last_state = ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED;
      break;
    case ANIM_DIR_OUT:
      drag_surface->drag.last_state = ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_UNFOLDED;
      break;
    default:
      g_assert_not_reached ();
    }
    margin = drag_surface->drag.anim_end;
    zphoc_draggable_layer_surface_v1_send_drag_end (drag_surface->resource, drag_surface->drag.last_state);
    apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_NONE);
    drag_surface->drag.anim_id = 0;
  } else {
    gint64 now = g_get_monotonic_time ();

    drag_surface->drag.anim_t += ((float)(now - last_frame)) / drag_surface->drag.anim_duration;
    if (drag_surface->drag.anim_t > 1.0)
      drag_surface->drag.anim_t = 1.0;

    distance = (drag_surface->drag.anim_end - drag_surface->drag.anim_start) *
      phoc_easing_ease (PHOC_EASING_EASE_OUT_CUBIC, drag_surface->drag.anim_t);
    switch (drag_surface->drag.anim_dir) {
    case ANIM_DIR_OUT:
    case ANIM_DIR_IN:
      margin = drag_surface->drag.anim_start + distance;
      break;
    default:
      g_assert_not_reached ();
    }
    zphoc_draggable_layer_surface_v1_send_dragged (drag_surface->resource, (int32_t)margin);
  }

  apply_margin (drag_surface, margin);
  phoc_layer_shell_arrange (output);
  /* FIXME: way too much damage */
  phoc_output_damage_whole (output);

  return done ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}


void
phoc_draggable_layer_surface_slide (PhocDraggableLayerSurface *drag_surface, PhocAnimDir anim_dir)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  double margin;
  struct wlr_output *wlr_output;

  if (drag_surface->layer_surface == NULL)
    return;

  wlr_layer_surface = drag_surface->layer_surface->layer_surface;
  wlr_output = wlr_layer_surface->output;

  if (wlr_output == NULL)
    return;

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = (double)(int32_t)wlr_layer_surface->current.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = (double)(int32_t)wlr_layer_surface->current.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = (double)(int32_t)wlr_layer_surface->current.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = (double)(int32_t)wlr_layer_surface->current.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  drag_surface->drag.anim_t = 0;
  drag_surface->drag.anim_start = margin;
  drag_surface->drag.anim_dir = anim_dir;
  drag_surface->drag.anim_end = (anim_dir == ANIM_DIR_OUT) ?
    drag_surface->current.unfolded : drag_surface->current.folded;
  drag_surface->drag.anim_duration = SLIDE_ANIM_DURATION_MS * 1000 *
    cbrt (ABS (drag_surface->drag.anim_end - drag_surface->drag.anim_start) /
          (float) ABS (drag_surface->current.unfolded - drag_surface->current.folded));

  apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING);

  g_debug ("%s: start: %d, end: %d dir: %d", __func__,
           drag_surface->drag.anim_start, drag_surface->drag.anim_end, drag_surface->drag.anim_dir);


  if (drag_surface->drag.anim_id) {
    phoc_animatable_remove_frame_callback (PHOC_ANIMATABLE (drag_surface->layer_surface),
                                           drag_surface->drag.anim_id);
  }
  drag_surface->drag.anim_id = phoc_animatable_add_frame_callback (
    PHOC_ANIMATABLE (drag_surface->layer_surface),
    on_output_frame_callback,
    drag_surface,
    NULL);
}


/**
 * phoc_draggable_layer_surface_get_layer_surface:
 * @drag_surface: The draggable layer surface
 *
 * Returns: (transfer none): The underlying layer surface
 */
PhocLayerSurface *
phoc_draggable_layer_surface_get_layer_surface (PhocDraggableLayerSurface *drag_surface)
{
  return drag_surface->layer_surface;
}

/**
 * phoc_alpha_layer_surface_get_layer_surface:
 * @alpha_surface: The layer surface with alpha effect
 *
 * Returns: (transfer none): The underlying layer surface
 */
PhocLayerSurface *
phoc_alpha_layer_surface_get_layer_surface (PhocAlphaLayerSurface *alpha_surface)
{
  return alpha_surface->layer_surface;
}

/**
 * phoc_stacked_layer_surface_get_layer_surface:
 * @stacked_surface: The layer surface binder
 *
 * Returns: (transfer none): The layer surface that has another layer
 *     surface bound to it
 */
PhocLayerSurface *
phoc_stacked_layer_surface_get_layer_surface (PhocStackedLayerSurface *stacked_surface)
{
  return stacked_surface->layer_surface;
}


static void
accept_drag (PhocDraggableLayerSurface *drag_surface,
             double                     off_x,
             double                     off_y)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;
  struct wlr_output *wlr_output = wlr_layer_surface->output;
  PhocOutput *output;
  int32_t *target;
  int32_t margin = 0;

  output = PHOC_OUTPUT (wlr_output->data);
  g_assert (PHOC_IS_OUTPUT (output));

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    target = &wlr_layer_surface->current.margin.top;
    margin = drag_surface->drag.start_margin + off_y;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    target = &wlr_layer_surface->current.margin.bottom;
    margin = drag_surface->drag.start_margin - off_y;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    target = &wlr_layer_surface->current.margin.left;
    margin = drag_surface->drag.start_margin + off_x;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    target = &wlr_layer_surface->current.margin.right;
    margin = drag_surface->drag.start_margin - off_x;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  g_debug ("%s: %f,%f, margin %d", __func__, off_x, off_y, margin);

  if (margin >= drag_surface->current.unfolded)
    margin = drag_surface->current.unfolded;

  if (margin <= drag_surface->current.folded)
    margin = drag_surface->current.folded;

  *target = margin;
  wlr_layer_surface->current.exclusive_zone = -margin + drag_surface->current.exclusive;

  wlr_layer_surface->pending.margin.top = wlr_layer_surface->current.margin.top;
  wlr_layer_surface->pending.margin.bottom = wlr_layer_surface->current.margin.bottom;
  wlr_layer_surface->pending.margin.left = wlr_layer_surface->current.margin.left;
  wlr_layer_surface->pending.margin.right = wlr_layer_surface->current.margin.right;
  wlr_layer_surface->pending.exclusive_zone = wlr_layer_surface->current.exclusive_zone;

  zphoc_draggable_layer_surface_v1_send_dragged (drag_surface->resource, margin);
  phoc_layer_shell_arrange (output);

  /* FIXME: way too much damage */
  phoc_output_damage_whole (output);

  apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_DRAGGING);
}


static gboolean
point_is_handle (PhocDraggableLayerSurface *drag_surface, double lx, double ly)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;
  struct wlr_box output_box;
  double sx, sy;

  wlr_output_layout_get_box (desktop->layout, wlr_layer_surface->output, &output_box);

  sx = lx - drag_surface->geo.x - output_box.x;
  sy = ly - drag_surface->geo.y - output_box.y;

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    return sy > drag_surface->current.drag_handle;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    return sy < drag_surface->current.drag_handle;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    return sx > drag_surface->current.drag_handle;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    return sx < drag_surface->current.drag_handle;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  return FALSE;
}


PhocDraggableSurfaceState
phoc_draggable_layer_surface_drag_start (PhocDraggableLayerSurface *drag_surface, double lx, double ly)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;
  bool is_handle = false;
  int32_t start_margin;

  if (drag_surface->current.drag_mode == ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_MODE_NONE)
    return PHOC_DRAGGABLE_SURFACE_STATE_REJECTED;

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    start_margin = (int32_t)wlr_layer_surface->current.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    start_margin = (int32_t)wlr_layer_surface->current.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    start_margin = (int32_t)wlr_layer_surface->current.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    start_margin = (int32_t)wlr_layer_surface->current.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }
  is_handle = point_is_handle (drag_surface, lx, ly);

  /* The user "caught" the surface during an animation */
  if (drag_surface->state == PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING) {
    if (drag_surface->drag.anim_id) {
      phoc_animatable_remove_frame_callback (PHOC_ANIMATABLE (drag_surface->layer_surface),
                                             drag_surface->drag.anim_id);
    }
    drag_surface->drag.start_margin = start_margin;
    drag_surface->drag.anim_id = 0;
    accept_drag (drag_surface, 0, 0);
    return drag_surface->state;
  }

  g_return_val_if_fail (drag_surface->state == PHOC_DRAGGABLE_SURFACE_STATE_NONE, drag_surface->state);

  drag_surface->drag.start_margin = start_margin;

  if (drag_surface->current.drag_mode == ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_MODE_HANDLE) {
    if (!is_handle)
      return PHOC_DRAGGABLE_SURFACE_STATE_REJECTED;
  }

  g_debug ("%s: %f,%f, margin: %d", __func__, lx, ly, drag_surface->drag.start_margin);

  drag_surface->drag.pending_accept = 0;
  drag_surface->drag.pending_reject = 0;

  apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_PENDING);
  return drag_surface->state;
}


static gboolean
phoc_draggable_surface_is_vertical (PhocDraggableLayerSurface *drag_surface)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    return TRUE;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    return FALSE;
  default:
    g_assert_not_reached ();
  }
}


PhocDraggableSurfaceState
phoc_draggable_layer_surface_drag_update (PhocDraggableLayerSurface *drag_surface,
                                          double                     off_x,
                                          double                     off_y)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;
  struct wlr_output *wlr_output = wlr_layer_surface->output;
  bool accept;

  if (drag_surface->state != PHOC_DRAGGABLE_SURFACE_STATE_PENDING &&
      drag_surface->state != PHOC_DRAGGABLE_SURFACE_STATE_DRAGGING) {
    apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_REJECTED);
    return drag_surface->state;
  }

  if (!wlr_output) {
    apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_REJECTED);
    return drag_surface->state;
  }

  if (phoc_draggable_surface_is_vertical (drag_surface)) {
    drag_surface->drag.pending_accept = off_y;
    drag_surface->drag.pending_reject = off_x;
  } else {
    drag_surface->drag.pending_accept = off_x;
    drag_surface->drag.pending_reject = off_y;
  }

  /* Too much motion in the wrong orientation, reject gesture */
  if (ABS (drag_surface->drag.pending_reject) > DRAG_REJECT_THRESHOLD_DISTANCE &&
      drag_surface->state == PHOC_DRAGGABLE_SURFACE_STATE_PENDING) {
    apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_REJECTED);
    return drag_surface->state;
  }

  /* Keep gesture pending until we reach the threshold */
  if (ABS (drag_surface->drag.pending_accept) < DRAG_ACCEPT_THRESHOLD_DISTANCE &&
      drag_surface->state == PHOC_DRAGGABLE_SURFACE_STATE_PENDING) {
    return drag_surface->state;
  }

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    accept = (wlr_layer_surface->current.margin.top == drag_surface->current.unfolded) ? off_y < 0 : true;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    accept = (wlr_layer_surface->current.margin.bottom == drag_surface->current.unfolded) ? off_y > 0 : true;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    accept = (wlr_layer_surface->current.margin.left == drag_surface->current.unfolded) ? off_x < 0 : true;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    accept = (wlr_layer_surface->current.margin.right == drag_surface->current.unfolded) ? off_x > 0 : true;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  /* Moved far enough but is it the right direction when unfolded? */
  if (drag_surface->state == PHOC_DRAGGABLE_SURFACE_STATE_PENDING && !accept) {
    apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_REJECTED);
    return drag_surface->state;
  }

  accept_drag (drag_surface, off_x, off_y);

  return drag_surface->state;
}


static gboolean
hit_threshold (PhocDraggableLayerSurface *drag_surface)
{
  int distance, start;
  int max_distance = ABS (drag_surface->current.folded - drag_surface->current.unfolded);
  float threshold = max_distance * drag_surface->current.threshold;
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;

  switch (drag_surface->drag.last_state) {
  case ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED:
    start = drag_surface->current.folded;
    break;
  case ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_UNFOLDED:
    start = drag_surface->current.unfolded;
    break;
  default:
    g_return_val_if_reached (FALSE);
  }

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    distance = (int32_t)wlr_layer_surface->current.margin.top - start;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    distance = (int32_t)wlr_layer_surface->current.margin.bottom - start;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    distance = (int32_t)wlr_layer_surface->current.margin.left - start;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    distance = (int32_t)wlr_layer_surface->current.margin.right - start;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  return ABS (distance) > threshold;
}


void
phoc_draggable_layer_surface_drag_end (PhocDraggableLayerSurface *drag_surface,
                                       double                     off_x,
                                       double                     off_y)
{
  PhocOutput *output;
  PhocAnimDir dir;
  struct wlr_layer_surface_v1 *wlr_layer_surface = drag_surface->layer_surface->layer_surface;
  struct wlr_output *wlr_output = wlr_layer_surface->output;

  if (!wlr_output)
    return;

  output = PHOC_OUTPUT (wlr_output->data);
  g_assert (PHOC_IS_OUTPUT (output));

  if (hit_threshold (drag_surface)) {
    dir = drag_surface->drag.last_state == ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED ?
      ANIM_DIR_OUT : ANIM_DIR_IN;
  } else {
    dir = drag_surface->drag.last_state == ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED ?
      ANIM_DIR_IN : ANIM_DIR_OUT;
  }

  phoc_layer_shell_arrange (output);
  drag_surface->drag.pending_accept = 0;
  drag_surface->drag.pending_reject = 0;

  phoc_draggable_layer_surface_slide (drag_surface, dir);

  apply_state (drag_surface, PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING);
}


PhocDraggableSurfaceState
phoc_draggable_layer_surface_get_state (PhocDraggableLayerSurface *drag_surface)
{
  return drag_surface->state;
}


gboolean
phoc_draggable_layer_surface_is_unfolded (PhocDraggableLayerSurface *drag_surface)
{
  return drag_surface->drag.last_state == ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_UNFOLDED;
}

/**
 * phoc_draggable_layer_surface_fling:
 * @drag_surface: The drag surface
 * @lx: The location where the fling happened
 * @ly: The location where the fling happened
 * @vx: The fling velocity in x direction
 * @vy: The fling velocity in y direction
 *
 * Determine whether a fling should move the drag surface, if so
 * animate the move to folded or unfolded position.
 *
 * Returns: `TRUE` if the fling was accepted, otherwise `FALSE`
 */
gboolean
phoc_draggable_layer_surface_fling (PhocDraggableLayerSurface *drag_surface,
                                    double                     lx,
                                    double                     ly,
                                    double                     vx,
                                    double                     vy)
{
  PhocAnimDir dir;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  double v;
  int32_t margin;

  g_assert (PHOC_IS_LAYER_SURFACE (drag_surface->layer_surface));
  wlr_layer_surface = drag_surface->layer_surface->layer_surface;

  /* Only accept fling in the drag area */
  if (!point_is_handle (drag_surface, lx, ly))
    return FALSE;

  /* Reject too low velocity */
  v = phoc_draggable_surface_is_vertical (drag_surface) ? vy : vx;
  if (fabs (v) < FLING_V_MIN)
    return FALSE;

  switch (wlr_layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = (int32_t)wlr_layer_surface->current.margin.top;
    /* Reject fling down when unfolded */
    if (margin == drag_surface->current.unfolded && v > 0)
      return FALSE;
    dir = v > 0 ? ANIM_DIR_OUT : ANIM_DIR_IN;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = (int32_t)wlr_layer_surface->current.margin.bottom;
    /* Reject fling up when unfolded */
    if (margin == drag_surface->current.unfolded && v < 0)
      return FALSE;
    dir = v > 0 ? ANIM_DIR_IN : ANIM_DIR_OUT;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = (int32_t)wlr_layer_surface->current.margin.right;
    /* Reject fling left when unfolded */
    if (margin == drag_surface->current.unfolded && v > 0)
      return FALSE;
    dir = v > 0 ? ANIM_DIR_IN : ANIM_DIR_OUT;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = (int32_t)wlr_layer_surface->current.margin.left;
    /* Reject fling right when unfolded */
    if (margin == drag_surface->current.unfolded && v < 0)
      return FALSE;
    dir = v > 0 ? ANIM_DIR_OUT : ANIM_DIR_IN;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  g_debug ("Fling surface with %f",vy);
  phoc_draggable_layer_surface_slide (drag_surface, dir);

  return TRUE;
}

/**
 * phoc_layer_shell_effects_get_draggable_layer_surface_from_layer_surface:
 * @self: The effects object that tracks the surfaces
 * @layer_surface: The layer-surface to look up
 *
 * Looks up the [type@PhocDraggableLayerSurface] attached to a [type@PhocLayerSurface].
 *
 * Returns:(transfer none) (nullable): The draggable layer surface.
 */
PhocDraggableLayerSurface *
phoc_layer_shell_effects_get_draggable_layer_surface_from_layer_surface (
  PhocLayerShellEffects *self,
  PhocLayerSurface *layer_surface)
{
  g_return_val_if_fail (PHOC_IS_LAYER_SHELL_EFFECTS (self), NULL);
  g_return_val_if_fail (PHOC_IS_LAYER_SURFACE (layer_surface), NULL);

  return g_hash_table_lookup (self->drag_surfaces_by_layer_surface, layer_surface);
}

/**
 * phoc_layer_shell_effects_get_layer_surface_stacks:
 * @self: The effects object that tracks the layer surface stacks
 *
 * Get the list of currently known stacks
 *
 * Returns:(transfer none)(element-type PhocStackedLayerSurface): The layer surface stacks
 */
GSList *
phoc_layer_shell_effects_get_layer_surface_stacks (PhocLayerShellEffects *self)
{
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));

  return self->stacked_surfaces;
}

enum zwlr_layer_shell_v1_layer
phoc_stacked_layer_surface_get_layer (PhocStackedLayerSurface *stacked_surface)
{
  g_assert (stacked_surface);

  if (!stacked_surface->layer_surface)
    return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;

  return phoc_layer_surface_get_layer (stacked_surface->layer_surface);
}

/**
 * phoc_stacked_layer_surface_get_target_layer_surface:
 * @stacked_surface: The stacked surface
 *
 * Get the [type@LayerSurface] this [type@StackedLayerSurface] is attached to
 *
 * Returns:(transfer none)(nullable): The target layer surface
 */
PhocLayerSurface *
phoc_stacked_layer_surface_get_target_layer_surface (PhocStackedLayerSurface *stacked_surface)
{
  g_assert (stacked_surface);

  return stacked_surface->current.surface;
}

PhocStackedSurfacePos
phoc_stacked_layer_surface_get_position (PhocStackedLayerSurface *stacked_surface)
{
  g_assert (stacked_surface);

  return stacked_surface->current.position;
}
