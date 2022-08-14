/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-layer-surface"

#include "config.h"

#include "layer-surface.h"
#include "layers.h"
#include "output.h"

G_DEFINE_TYPE (PhocLayerSurface, phoc_layer_surface, G_TYPE_OBJECT)


static void
phoc_layer_surface_finalize (GObject *object)
{
  PhocLayerSurface *self = PHOC_LAYER_SURFACE (object);

  if (self->layer_surface->mapped)
    phoc_layer_surface_unmap (self);

  wl_list_remove (&self->link);
  wl_list_remove (&self->destroy.link);
  wl_list_remove (&self->map.link);
  wl_list_remove (&self->unmap.link);
  wl_list_remove (&self->surface_commit.link);
  if (self->layer_surface->output) {
    PhocOutput *output = phoc_layer_surface_get_output (self);

    g_assert (PHOC_IS_OUTPUT (output));
    wl_list_remove (&self->output_destroy.link);
    phoc_layer_shell_arrange (output);
    phoc_layer_shell_update_focus ();
  }

  G_OBJECT_CLASS (phoc_layer_surface_parent_class)->finalize (object);
}


static void
phoc_layer_surface_class_init (PhocLayerSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phoc_layer_surface_finalize;
}


static void
phoc_layer_surface_init (PhocLayerSurface *self)
{
}


PhocLayerSurface *
phoc_layer_surface_new (void)
{
  return PHOC_LAYER_SURFACE (g_object_new (PHOC_TYPE_LAYER_SURFACE, NULL));
}


/**
 * phoc_layer_surface_unmap:
 * @self: The layer surface to unmap
 *
 * Unmaps a layer surface
 */
void
phoc_layer_surface_unmap (PhocLayerSurface *self)
{
  struct wlr_layer_surface_v1 *layer_surface;
  struct wlr_output *wlr_output;

  g_assert (PHOC_IS_LAYER_SURFACE (self));
  layer_surface = self->layer_surface;

  wlr_output = layer_surface->output;
  if (wlr_output != NULL) {
    phoc_output_damage_whole_local_surface(wlr_output->data, layer_surface->surface,
                                           self->geo.x, self->geo.y);
  }
}


/**
 * phoc_layer_surface_get_namespace:
 * @self: The layer surface
 *
 * Returns: (nullable): The layer surface's namespace
 */
const char *
phoc_layer_surface_get_namespace (PhocLayerSurface *self)
{
  g_assert (PHOC_IS_LAYER_SURFACE (self));

  return self->layer_surface->namespace;
}

/**
 * phoc_layer_surface_get_output:
 * @self: The layer surface
 *
 * Returns: (transfer none) (nullable): The layer surface's output or
 *  %NULL if the output was destroyed.
 */
PhocOutput *
phoc_layer_surface_get_output (PhocLayerSurface *self)
{
  g_assert (PHOC_IS_LAYER_SURFACE (self));

  if (self->layer_surface->output == NULL)
    return NULL;

  return self->layer_surface->output->data;
}
