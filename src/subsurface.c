/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-subsurface"

#include "phoc-config.h"

#include "subsurface.h"


enum {
  PROP_0,
  PROP_WLR_SUBSURFACE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PhocSubsurface:
 *
 * A subsurface attached to a [type@View] or [type@ViewChild].
 */
typedef struct _PhocSubsurface {
  PhocViewChild          parent_instance;

  struct wlr_subsurface *wlr_subsurface;

  struct wl_listener     destroy;
} PhocSubsurface;


G_DEFINE_FINAL_TYPE (PhocSubsurface, phoc_subsurface, PHOC_TYPE_VIEW_CHILD)

static void
subsurface_get_pos (PhocViewChild *child, int *sx, int *sy)
{
  struct wlr_surface *wlr_surface;
  struct wlr_subsurface *wlr_subsurface;

  wlr_surface = child->wlr_surface;
  if (child->parent)
    phoc_view_child_get_pos (child->parent, sx, sy);
  else
    *sx = *sy = 0;

  wlr_subsurface = wlr_subsurface_try_from_wlr_surface (wlr_surface);
  g_assert (wlr_subsurface);
  *sx += wlr_subsurface->current.x;
  *sy += wlr_subsurface->current.y;
}


static void
phoc_subsurface_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PhocSubsurface *self = PHOC_SUBSURFACE (object);

  switch (property_id) {
  case PROP_WLR_SUBSURFACE:
    self->wlr_subsurface = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_subsurface_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PhocSubsurface *self = PHOC_SUBSURFACE (object);

  switch (property_id) {
  case PROP_WLR_SUBSURFACE:
    g_value_set_pointer (value, self->wlr_subsurface);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
subsurface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocSubsurface *self = wl_container_of (listener, self, destroy);

  g_object_unref (self);
}



static void
phoc_subsurface_constructed (GObject *object)
{
  PhocSubsurface *self = PHOC_SUBSURFACE (object);

  G_OBJECT_CLASS (phoc_subsurface_parent_class)->constructed (object);

  PHOC_VIEW_CHILD (self)->mapped = self->wlr_subsurface->surface->mapped;

  self->destroy.notify = subsurface_handle_destroy;
  wl_signal_add (&self->wlr_subsurface->events.destroy, &self->destroy);
}


static void
phoc_subsurface_finalize (GObject *object)
{
  PhocSubsurface *self = PHOC_SUBSURFACE (object);

  wl_list_remove (&self->destroy.link);

  G_OBJECT_CLASS (phoc_subsurface_parent_class)->finalize (object);
}


static void
phoc_subsurface_class_init (PhocSubsurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocViewChildClass *view_child_class = PHOC_VIEW_CHILD_CLASS (klass);

  object_class->constructed = phoc_subsurface_constructed;
  object_class->finalize = phoc_subsurface_finalize;
  object_class->get_property = phoc_subsurface_get_property;
  object_class->set_property = phoc_subsurface_set_property;

  view_child_class->get_pos = subsurface_get_pos;

  props[PROP_WLR_SUBSURFACE] =
    g_param_spec_pointer ("wlr-subsurface", "", "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_subsurface_init (PhocSubsurface *self)
{
}


PhocSubsurface *
phoc_subsurface_new (PhocView *view, struct wlr_subsurface *wlr_subsurface)
{
  return g_object_new (PHOC_TYPE_SUBSURFACE,
                       "view", view,
                       "wlr-surface", wlr_subsurface->surface,
                       "wlr-subsurface", wlr_subsurface,
                       NULL);
}
