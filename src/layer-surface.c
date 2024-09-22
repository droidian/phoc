/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-layer-surface"

#include "phoc-config.h"

#include "anim/animatable.h"
#include "layer-shell-private.h"
#include "layer-surface.h"
#include "layer-shell.h"
#include "output.h"
#include "server.h"
#include "utils.h"


/**
 * PhocLayerSurface:
 *
 * A Layer surface backed by the wlr-layer-surface wayland protocol.
 *
 * For details on how to setup a layer surface see `phoc_handle_layer_shell_surface`.
 *
 * This handles the events concerning individual surfaces like mapping
 * and unmapping.  For the actual layout of surfaces on a
 * [class@Output] see [func@layer_shell_arrange].
 */

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


static void
phoc_layer_surface_damage (PhocLayerSurface *self)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  struct wlr_output *wlr_output;

  g_assert (PHOC_IS_LAYER_SURFACE (self));
  wlr_layer_surface = self->layer_surface;

  wlr_output = wlr_layer_surface->output;
  if (!wlr_output)
    return;

  phoc_output_damage_whole_surface (PHOC_OUTPUT (wlr_output->data),
                                    wlr_layer_surface->surface,
                                    self->geo.x,
                                    self->geo.y);
}


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
handle_surface_commit (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocInput *input = phoc_server_get_input (server);
  PhocLayerSurface *self = wl_container_of (listener, self, surface_commit);
  struct wlr_layer_surface_v1 *wlr_layer_surface = self->layer_surface;
  struct wlr_output *wlr_output = wlr_layer_surface->output;

  if (wlr_output != NULL) {
    PhocOutput *output = PHOC_OUTPUT (wlr_output->data);
    struct wlr_box old_geo = self->geo;

    bool layer_changed = false;
    if (wlr_layer_surface->current.committed != 0) {
      layer_changed = self->layer != wlr_layer_surface->current.layer;

      phoc_output_set_layer_dirty (output, self->layer);

      self->layer = wlr_layer_surface->current.layer;
      phoc_layer_shell_arrange (output);
      phoc_layer_shell_update_focus ();
    }

    // Cursor changes which happen as a consequence of resizing a layer
    // surface are applied in phoc_layer_shell_arrange. Because the resize happens
    // before the underlying surface changes, it will only receive a cursor
    // update if the new cursor position crosses the *old* sized surface in
    // the *new* layer surface.
    // Another cursor move event is needed when the surface actually
    // changes.
    struct wlr_surface *surface = wlr_layer_surface->surface;
    if (surface->previous.width != surface->current.width ||
        surface->previous.height != surface->current.height) {
      phoc_layer_shell_update_cursors (self, phoc_input_get_seats (input));
    }

    bool geo_changed = memcmp (&old_geo, &self->geo, sizeof (struct wlr_box)) != 0;
    if (geo_changed || layer_changed) {
      phoc_output_damage_whole_surface (output,
                                        wlr_layer_surface->surface,
                                        old_geo.x,
                                        old_geo.y);
      phoc_output_damage_whole_surface (output,
                                        wlr_layer_surface->surface,
                                        self->geo.x,
                                        self->geo.y);
    } else {
      phoc_output_damage_from_surface (output,
                                       wlr_layer_surface->surface,
                                       self->geo.x,
                                       self->geo.y);
    }

    phoc_output_set_layer_dirty (output, self->layer);
  }
}


static void
handle_new_subsurface (struct wl_listener *listener, void *data)
{
  PhocLayerSurface *self = wl_container_of (listener, self, new_subsurface);
  struct wlr_subsurface *wlr_subsurface = data;

  PhocLayerSubsurface *subsurface = phoc_layer_subsurface_create (wlr_subsurface);
  subsurface->parent_type = LAYER_PARENT_LAYER;
  subsurface->parent_layer = self;
  wl_list_insert (&self->subsurfaces, &subsurface->link);
}


static void
handle_new_popup (struct wl_listener *listener, void *data)
{
  PhocLayerSurface *self = wl_container_of (listener, self, new_popup);
  struct wlr_xdg_popup *wlr_popup = data;
  PhocLayerPopup *popup = phoc_layer_popup_create (wlr_popup);

  popup->parent_type = LAYER_PARENT_LAYER;
  popup->parent_layer = self;
  phoc_layer_popup_unconstrain (popup);
}


static void
handle_map (struct wl_listener *listener, void *data)
{
  PhocLayerSurface *self = wl_container_of (listener, self, map);
  struct wlr_layer_surface_v1 *wlr_layer_surface = self->layer_surface;
  PhocOutput *output = phoc_layer_surface_get_output (self);

  if (!output)
    return;

  self->mapped = true;

  struct wlr_subsurface *wlr_subsurface;
  wl_list_for_each (wlr_subsurface,
                    &wlr_layer_surface->surface->current.subsurfaces_below,
                    current.link) {
    PhocLayerSubsurface *subsurface = phoc_layer_subsurface_create (wlr_subsurface);
    subsurface->parent_type = LAYER_PARENT_LAYER;
    subsurface->parent_layer = self;
    wl_list_insert (&self->subsurfaces, &subsurface->link);
  }
  wl_list_for_each (wlr_subsurface,
                    &wlr_layer_surface->surface->current.subsurfaces_above,
                    current.link) {
    PhocLayerSubsurface *subsurface = phoc_layer_subsurface_create (wlr_subsurface);
    subsurface->parent_type = LAYER_PARENT_LAYER;
    subsurface->parent_layer = self;
    wl_list_insert (&self->subsurfaces, &subsurface->link);
  }

  self->new_subsurface.notify = handle_new_subsurface;
  wl_signal_add (&wlr_layer_surface->surface->events.new_subsurface, &self->new_subsurface);

  phoc_output_damage_whole_surface (output,
                                    wlr_layer_surface->surface,
                                    self->geo.x,
                                    self->geo.y);

  phoc_utils_wlr_surface_enter_output (wlr_layer_surface->surface, output->wlr_output);

  phoc_layer_shell_arrange (output);
  phoc_layer_shell_update_focus ();
}


static void
handle_unmap (struct wl_listener *listener, void *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocLayerSurface *self = wl_container_of (listener, self, unmap);
  PhocOutput *output = phoc_layer_surface_get_output (self);

  self->mapped = false;

  PhocLayerSubsurface *subsurface, *tmp;
  wl_list_for_each_safe (subsurface, tmp, &self->subsurfaces, link)
    phoc_layer_subsurface_destroy (subsurface);

  wl_list_remove (&self->new_subsurface.link);

  phoc_layer_surface_damage (self);
  phoc_input_update_cursor_focus (input);

  if (output)
    phoc_layer_shell_arrange (output);
  phoc_layer_shell_update_focus ();
}


static void
handle_destroy (struct wl_listener *listener, void *data)
{
  PhocLayerSurface *self = wl_container_of (listener, self, destroy);

  g_object_unref (self);
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
  PhocOutput *output;

  G_OBJECT_CLASS (phoc_layer_surface_parent_class)->constructed (object);

  /* wlr signals */
  self->output_destroy.notify = handle_output_destroy;
  wl_signal_add (&self->layer_surface->output->events.destroy, &self->output_destroy);

  self->destroy.notify = handle_destroy;
  wl_signal_add (&self->layer_surface->events.destroy, &self->destroy);

  self->map.notify = handle_map;
  wl_signal_add (&self->layer_surface->surface->events.map, &self->map);

  self->unmap.notify = handle_unmap;
  wl_signal_add (&self->layer_surface->surface->events.unmap, &self->unmap);

  self->new_popup.notify = handle_new_popup;
  wl_signal_add (&self->layer_surface->events.new_popup, &self->new_popup);

  self->surface_commit.notify = handle_surface_commit;
  wl_signal_add (&self->layer_surface->surface->events.commit, &self->surface_commit);

  /* Add to the list of layer surfaces on the output */
  output = PHOC_OUTPUT (self->layer_surface->output->data);
  wl_list_insert (&output->layer_surfaces, &self->link);
}


static void
phoc_layer_surface_finalize (GObject *object)
{
  PhocLayerSurface *self = PHOC_LAYER_SURFACE (object);
  PhocOutput *output = phoc_layer_surface_get_output (self);

  if (self->layer_surface->surface->mapped)
    phoc_layer_surface_damage (self);

  wl_list_remove (&self->link);
  if (output)
    phoc_output_set_layer_dirty (output, self->layer);

  wl_list_remove (&self->destroy.link);
  wl_list_remove (&self->map.link);
  wl_list_remove (&self->unmap.link);
  wl_list_remove (&self->surface_commit.link);
  wl_list_remove (&self->new_popup.link);

  if (output) {
    g_assert (PHOC_IS_OUTPUT (output));
    phoc_output_remove_frame_callbacks_by_animatable (output, PHOC_ANIMATABLE (self));
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
  self->alpha = 1.0;
  wl_list_init(&self->subsurfaces);
}


PhocLayerSurface *
phoc_layer_surface_new (struct wlr_layer_surface_v1 *layer_surface)
{
  return g_object_new (PHOC_TYPE_LAYER_SURFACE,
                       "wlr-layer-surface", layer_surface,
                       NULL);
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

/**
 * phoc_layer_surface_set_alpha:
 * @self: The layer surface
 * @alpha: The alpha value
 *
 * Sets the surfaces transparency.
 */
void
phoc_layer_surface_set_alpha (PhocLayerSurface *self, float alpha)
{
  g_assert (PHOC_IS_LAYER_SURFACE (self));
  g_return_if_fail (alpha >= 0.0 && alpha <= 1.0);

  self->alpha = alpha;
}

/**
 * phoc_layer_surface_get_alpha:
 * @self: The layer surface
 *
 * Returns: the surfaces transparency
 */
float
phoc_layer_surface_get_alpha (PhocLayerSurface *self)
{
  g_assert (PHOC_IS_LAYER_SURFACE (self));

  return self->alpha;
}

/**
 * phoc_layer_surface_get_layer:
 * @self: The layer surface
 *
 * Get the layer surface's current layer
 *
 * Returns: the current layer
 */
enum zwlr_layer_shell_v1_layer
phoc_layer_surface_get_layer (PhocLayerSurface *self)
{
  g_assert (PHOC_IS_LAYER_SURFACE (self));

  return self->layer;
}

/**
 * phoc_layer_surface_get_mapped:
 * @self: The layer surface
 *
 * Get whether the layer surface is currently mapped
 *
 * Returns: `TRUE` if the surface is currently mapped, otherwise `FALSE`
 */
gboolean
phoc_layer_surface_get_mapped (PhocLayerSurface *self)
{
  g_assert (PHOC_IS_LAYER_SURFACE (self));

  return self->mapped;
}
