/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-xdg-popup"

#include "phoc-config.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "cursor.h"
#include "desktop.h"
#include "input.h"
#include "server.h"
#include "view.h"
#include "view-child-private.h"
#include "utils.h"
#include "xdg-popup.h"

enum {
  PROP_0,
  PROP_WLR_POPUP,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PhocXdgPopup:
 *
 * A popup as defined in the xdg-shell protocol
 */
typedef struct _PhocXdgPopup {
  PhocViewChild         parent_instance;

  struct wlr_xdg_popup *wlr_popup;

  struct wl_listener    destroy;
  struct wl_listener    new_popup;
  struct wl_listener    reposition;
  struct wl_listener    surface_commit;

  gboolean              repositioned;
} PhocXdgPopup;

G_DEFINE_FINAL_TYPE (PhocXdgPopup, phoc_xdg_popup, PHOC_TYPE_VIEW_CHILD)


static void
popup_get_pos (PhocViewChild *child, int *sx, int *sy)
{
  PhocXdgPopup *self = PHOC_XDG_POPUP (child);
  struct wlr_xdg_popup *wlr_popup = self->wlr_popup;
  struct wlr_xdg_surface *base = self->wlr_popup->base;

  wlr_xdg_popup_get_toplevel_coords (wlr_popup,
                                     wlr_popup->current.geometry.x - base->current.geometry.x,
                                     wlr_popup->current.geometry.y - base->current.geometry.y,
                                     sx, sy);
}


static void
popup_unconstrain (PhocXdgPopup* self)
{
  PhocView *view = phoc_view_child_get_view (PHOC_VIEW_CHILD (self));
  struct wlr_box output_box;
  struct wlr_box usable_area;
  PhocOutput *output;

  output = phoc_desktop_layout_get_output (view->desktop, view->box.x, view->box.y);
  if (output == NULL)
    return;

  wlr_output_layout_get_box (view->desktop->layout, output->wlr_output, &output_box);
  usable_area = output->usable_area;
  usable_area.x += output_box.x;
  usable_area.y += output_box.y;

  /* the output box expressed in the coordinate system of the toplevel parent
   * of the popup */
  struct wlr_box output_toplevel_sx_box = {
    .x = usable_area.x - view->box.x,
    .y = usable_area.y - view->box.y,
    .width = usable_area.width,
    .height = usable_area.height,
  };

  wlr_xdg_popup_unconstrain_from_box (self->wlr_popup, &output_toplevel_sx_box);
}


static void
popup_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocXdgPopup *self = wl_container_of (listener, self, destroy);

  g_object_unref (self);
}


static void
popup_handle_new_popup (struct wl_listener *listener, void *data)
{
  PhocXdgPopup *self = wl_container_of (listener, self, new_popup);
  struct wlr_xdg_popup *wlr_popup = data;

  phoc_xdg_popup_new (phoc_view_child_get_view (PHOC_VIEW_CHILD (self)), wlr_popup);
}


static void
popup_handle_reposition (struct wl_listener *listener, void *data)
{
  PhocXdgPopup *self = wl_container_of (listener, self, reposition);

  self->repositioned = TRUE;
  popup_unconstrain (self);
}


static void
popup_handle_surface_commit (struct wl_listener *listener, void *data)
{
  PhocXdgPopup *self = wl_container_of (listener, self, surface_commit);

  if (self->wlr_popup->base->initial_commit)
    popup_unconstrain (self);

  if (self->repositioned) {
    /* clear the old popup position */
    /* TODO: this is too much damage */
    phoc_view_damage_whole (phoc_view_child_get_view (PHOC_VIEW_CHILD (self)));
    self->repositioned = FALSE;
  }
}


static void
phoc_xdg_popup_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PhocXdgPopup *self = PHOC_XDG_POPUP (object);

  switch (property_id) {
  case PROP_WLR_POPUP:
    self->wlr_popup = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_xdg_popup_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PhocXdgPopup *self = PHOC_XDG_POPUP (object);

  switch (property_id) {
  case PROP_WLR_POPUP:
    g_value_set_pointer (value, self->wlr_popup);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_xdg_popup_constructed (GObject *object)
{
  PhocXdgPopup *self = PHOC_XDG_POPUP (object);
  struct wlr_xdg_surface *xdg_surface = self->wlr_popup->base;

  G_OBJECT_CLASS (phoc_xdg_popup_parent_class)->constructed (object);

  self->destroy.notify = popup_handle_destroy;
  wl_signal_add (&self->wlr_popup->base->events.destroy, &self->destroy);

  self->new_popup.notify = popup_handle_new_popup;
  wl_signal_add (&self->wlr_popup->base->events.new_popup, &self->new_popup);

  self->reposition.notify = popup_handle_reposition;
  wl_signal_add (&self->wlr_popup->events.reposition, &self->reposition);

  wl_signal_add (&xdg_surface->surface->events.commit, &self->surface_commit);
  self->surface_commit.notify = popup_handle_surface_commit;
}


static void
phoc_xdg_popup_finalize (GObject *object)
{
  PhocXdgPopup *self = PHOC_XDG_POPUP (object);

  wl_list_remove (&self->surface_commit.link);
  wl_list_remove (&self->reposition.link);
  wl_list_remove (&self->new_popup.link);
  wl_list_remove (&self->destroy.link);

  G_OBJECT_CLASS (phoc_xdg_popup_parent_class)->finalize (object);
}


static void
phoc_xdg_popup_class_init (PhocXdgPopupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocViewChildClass *view_child_class = PHOC_VIEW_CHILD_CLASS (klass);

  object_class->constructed = phoc_xdg_popup_constructed;
  object_class->finalize = phoc_xdg_popup_finalize;
  object_class->get_property = phoc_xdg_popup_get_property;
  object_class->set_property = phoc_xdg_popup_set_property;

  view_child_class->get_pos = popup_get_pos;

  props[PROP_WLR_POPUP] =
    g_param_spec_pointer ("wlr-popup", "", "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_xdg_popup_init (PhocXdgPopup *self)
{
}


PhocXdgPopup *
phoc_xdg_popup_new (PhocView *view, struct wlr_xdg_popup *wlr_xdg_popup)
{
  return g_object_new (PHOC_TYPE_XDG_POPUP,
                       "view", view,
                       "wlr-popup", wlr_xdg_popup,
                       "wlr-surface", wlr_xdg_popup->base->surface,
                       NULL);
}
