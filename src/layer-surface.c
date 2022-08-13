/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-layer-surface"

#include "config.h"

#include "anim/animatable.h"
#include "layer-surface.h"
#include "layers.h"
#include "output.h"

enum {
  PROP_0,
  PROP_WLR_LAYER_SURFACE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


static void phoc_animatable_interface_init (PhocAnimatableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocLayerSurface, phoc_layer_surface, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (PHOC_TYPE_ANIMATABLE,
                                                phoc_animatable_interface_init))

static guint
phoc_layer_surface_add_frame_callback (PhocAnimatable    *iface,
                                       PhocFrameCallback  callback,
                                       gpointer           user_data,
                                       GDestroyNotify     notify)
{
  PhocLayerSurface *self = PHOC_LAYER_SURFACE (iface);
  PhocOutput *output = phoc_layer_surface_get_output (self);

  return phoc_output_add_frame_callback (output, iface, callback, user_data, notify);
}


static void
phoc_layer_surface_remove_frame_callback (PhocAnimatable *iface, guint id)
{
  PhocLayerSurface *self = PHOC_LAYER_SURFACE (iface);
  PhocOutput *output = phoc_layer_surface_get_output (self);

  /* Only remove frame callback if output is not inert */
  if (self->layer_surface->output)
    phoc_output_remove_frame_callback (output, id);
}


static void
handle_output_destroy (struct wl_listener *listener, void *data)
{
  PhocLayerSurface *self = wl_container_of (listener, self, output_destroy);

  self->layer_surface->output = NULL;
  wl_list_remove (&self->output_destroy.link);
  wlr_layer_surface_v1_destroy (self->layer_surface);
}


static void
phoc_layer_surface_set_property (GObject     *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhocLayerSurface *self = PHOC_LAYER_SURFACE (object);

  switch (property_id) {
  case PROP_WLR_LAYER_SURFACE:
    self->layer_surface = g_value_get_pointer (value);
    self->layer_surface->data = self;
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_layer_surface_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhocLayerSurface *self = PHOC_LAYER_SURFACE (object);

  switch (property_id) {
  case PROP_WLR_LAYER_SURFACE:
    g_value_set_pointer (value, self->layer_surface);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_layer_surface_constructed (GObject *object)
{
  PhocLayerSurface *self = PHOC_LAYER_SURFACE (object);

  G_OBJECT_CLASS (phoc_layer_surface_parent_class)->constructed (object);

  /* wlr signals */
  self->output_destroy.notify = handle_output_destroy;
  wl_signal_add (&self->layer_surface->output->events.destroy, &self->output_destroy);
}


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
phoc_animatable_interface_init (PhocAnimatableInterface *iface)
{
  iface->add_frame_callback = phoc_layer_surface_add_frame_callback;
  iface->remove_frame_callback = phoc_layer_surface_remove_frame_callback;
}


static void
phoc_layer_surface_class_init (PhocLayerSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = phoc_layer_surface_set_property;
  object_class->get_property = phoc_layer_surface_get_property;

  object_class->constructed = phoc_layer_surface_constructed;
  object_class->finalize = phoc_layer_surface_finalize;

  props[PROP_WLR_LAYER_SURFACE] =
    g_param_spec_pointer ("wlr-layer-surface", "", "",
                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_layer_surface_init (PhocLayerSurface *self)
{
  wl_list_init(&self->subsurfaces);
}


PhocLayerSurface *
phoc_layer_surface_new (struct wlr_layer_surface_v1 *layer_surface)
{
  return PHOC_LAYER_SURFACE (g_object_new (PHOC_TYPE_LAYER_SURFACE,
                                           "wlr-layer-surface", layer_surface,
                                           NULL));
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
