/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-view-child"

#include "phoc-config.h"

#include "desktop.h"
#include "input.h"
#include "output.h"
#include "server.h"
#include "subsurface.h"
#include "utils.h"
#include "view-private.h"
#include "view-child-private.h"


enum {
  PROP_0,
  PROP_VIEW,
  PROP_WLR_SURFACE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


G_DEFINE_TYPE (PhocViewChild, phoc_view_child, G_TYPE_OBJECT)


static void
phoc_view_child_subsurface_create (PhocViewChild *self, struct wlr_subsurface *wlr_subsurface)
{
  PhocSubsurface *subsurface = phoc_subsurface_new (self->view, wlr_subsurface);

  PHOC_VIEW_CHILD (subsurface)->parent = self;
  self->children = g_slist_prepend (self->children, subsurface);

  phoc_view_child_damage_whole (PHOC_VIEW_CHILD (subsurface));
}


static void
phoc_view_child_init_subsurfaces (PhocViewChild *self, struct wlr_surface *surface)
{
  struct wlr_subsurface *subsurface;

  wl_list_for_each (subsurface, &surface->current.subsurfaces_below, current.link)
    phoc_view_child_subsurface_create (self, subsurface);

  wl_list_for_each (subsurface, &surface->current.subsurfaces_above, current.link)
    phoc_view_child_subsurface_create (self, subsurface);
}


static bool
phoc_view_child_is_mapped (PhocViewChild *self)
{
  while (self) {
    if (!self->mapped)
      return false;

    self = self->parent;
  }
  return true;
}


static void
phoc_view_child_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PhocViewChild *self = PHOC_VIEW_CHILD (object);

  switch (property_id) {
  case PROP_VIEW:
    /* TODO: Should hold a ref */
    self->view = g_value_get_object (value);
    break;
  case PROP_WLR_SURFACE:
    self->wlr_surface = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_view_child_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PhocViewChild *self = PHOC_VIEW_CHILD (object);

  switch (property_id) {
  case PROP_VIEW:
    g_value_set_object (value, self->view);
    break;
  case PROP_WLR_SURFACE:
    g_value_set_pointer (value, self->wlr_surface);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_view_child_handle_map (struct wl_listener *listener, void *data)
{
  PhocViewChild *self = wl_container_of (listener, self, map);

  PHOC_VIEW_CHILD_GET_CLASS (self)->map (self);
}


static void
phoc_view_child_handle_unmap (struct wl_listener *listener, void *data)
{
  PhocViewChild *self = wl_container_of (listener, self, unmap);

  PHOC_VIEW_CHILD_GET_CLASS (self)->unmap (self);
}


static void
phoc_view_child_handle_new_subsurface (struct wl_listener *listener, void *data)
{
  PhocViewChild *self = wl_container_of (listener, self, new_subsurface);
  struct wlr_subsurface *wlr_subsurface = data;

  phoc_view_child_subsurface_create (self, wlr_subsurface);
}


static void
phoc_view_child_handle_commit (struct wl_listener *listener, void *data)
{
  PhocViewChild *self = wl_container_of (listener, self, commit);

  phoc_view_child_apply_damage (self);
}


static void
phoc_view_child_constructed (GObject *object)
{
  PhocViewChild *self = PHOC_VIEW_CHILD (object);

  G_OBJECT_CLASS (phoc_view_child_parent_class)->constructed (object);

  self->map.notify = phoc_view_child_handle_map;
  wl_signal_add (&self->wlr_surface->events.map, &self->map);

  self->unmap.notify = phoc_view_child_handle_unmap;
  wl_signal_add (&self->wlr_surface->events.unmap, &self->unmap);

  self->commit.notify = phoc_view_child_handle_commit;
  wl_signal_add (&self->wlr_surface->events.commit, &self->commit);

  self->new_subsurface.notify = phoc_view_child_handle_new_subsurface;
  wl_signal_add (&self->wlr_surface->events.new_subsurface, &self->new_subsurface);

  phoc_view_add_child (self->view, self);

  phoc_view_child_init_subsurfaces (self, self->wlr_surface);
}


static void
phoc_view_child_finalize (GObject *object)
{
  PhocViewChild *self = PHOC_VIEW_CHILD (object);

  if (phoc_view_child_is_mapped (self) && phoc_view_is_mapped (self->view))
    phoc_view_child_damage_whole (self);

  /* Remove from parent if it's also a PhocViewChild */
  if (self->parent != NULL) {
    self->parent->children = g_slist_remove (self->parent->children, self);
    self->parent = NULL;
  }

  /* Detach us from all children */
  for (GSList *elem = self->children; elem; elem = elem->next) {
    PhocViewChild *subchild = elem->data;
    subchild->parent = NULL;
    /* The subchild lost its parent, so it cannot see that the parent is unmapped. Unmap it directly */
    /* TODO: But then we won't damage them on destroy? */
    subchild->mapped = false;
  }
  g_clear_pointer (&self->children, g_slist_free);

  wl_list_remove (&self->link);

  wl_list_remove (&self->map.link);
  wl_list_remove (&self->unmap.link);
  wl_list_remove (&self->commit.link);
  wl_list_remove (&self->new_subsurface.link);

  self->view = NULL;
  self->wlr_surface = NULL;

  G_OBJECT_CLASS (phoc_view_child_parent_class)->finalize (object);
}


static void
phoc_view_child_map_default (PhocViewChild *self)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocView *view = self->view;

  self->mapped = true;
  phoc_view_child_damage_whole (self);

  struct wlr_box box;
  phoc_view_get_box (view, &box);

  PhocOutput *output;
  wl_list_for_each (output, &view->desktop->outputs, link) {
    bool intersects = wlr_output_layout_intersects (view->desktop->layout,
                                                    output->wlr_output, &box);
    if (intersects)
      phoc_utils_wlr_surface_enter_output (self->wlr_surface, output->wlr_output);
  }

  phoc_input_update_cursor_focus (input);
}


static void
phoc_view_child_unmap_default (PhocViewChild *self)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());

  phoc_view_child_damage_whole (self);
  phoc_input_update_cursor_focus (input);
  self->mapped = false;
}


G_NORETURN
static void
phoc_view_child_get_pos_default (PhocViewChild *self, int *sx, int *sy)
{
  g_assert_not_reached ();
}


static void
phoc_view_child_class_init (PhocViewChildClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocViewChildClass *view_child_class = PHOC_VIEW_CHILD_CLASS (klass);

  object_class->get_property = phoc_view_child_get_property;
  object_class->set_property = phoc_view_child_set_property;
  object_class->constructed = phoc_view_child_constructed;
  object_class->finalize = phoc_view_child_finalize;

  view_child_class->map = phoc_view_child_map_default;
  view_child_class->unmap = phoc_view_child_unmap_default;
  view_child_class->get_pos = phoc_view_child_get_pos_default;

  props[PROP_VIEW] =
    g_param_spec_object ("view", "", "",
                         PHOC_TYPE_VIEW,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  props[PROP_WLR_SURFACE] =
    g_param_spec_pointer ("wlr-surface", "", "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_view_child_init (PhocViewChild *self)
{
}

/**
 * phoc_view_child_apply_damage:
 * @self: A view child
 *
 * This is the equivalent of `phoc_view_apply_damage` but for [type@ViewChild].
 */
void
phoc_view_child_apply_damage (PhocViewChild *self)
{
  if (!self || !phoc_view_child_is_mapped (self) || !phoc_view_is_mapped (self->view))
    return;

  phoc_view_apply_damage (self->view);
}

/**
 * phoc_view_child_damage_whole:
 * @self: A view child
 *
 * This is the equivalent of [method@View.damage_whole] but for
 * [type@ViewChild].
 */
void
phoc_view_child_damage_whole (PhocViewChild *self)
{
  PhocOutput *output;
  int sx, sy;
  struct wlr_box view_box;

  if (!self || !phoc_view_child_is_mapped (self) || !phoc_view_is_mapped (self->view))
    return;

  phoc_view_get_box (self->view, &view_box);
  phoc_view_child_get_pos (self, &sx, &sy);

  wl_list_for_each (output, &self->view->desktop->outputs, link) {
    struct wlr_box output_box;
    wlr_output_layout_get_box (self->view->desktop->layout, output->wlr_output, &output_box);
    phoc_output_damage_whole_surface (output,
                                      self->wlr_surface,
                                      view_box.x + sx - output_box.x,
                                      view_box.y + sy - output_box.y);
  }
}


void
phoc_view_child_get_pos (PhocViewChild *self, int *sx, int *sy)
{
  g_assert (PHOC_IS_VIEW_CHILD (self));

  PHOC_VIEW_CHILD_GET_CLASS (self)->get_pos (self, sx, sy);
}

/**
 * phoc_view_child_get_view:
 * @self: A view child
 *
 * Get the view this child belongs to.
 *
 * Returns: (transfer none): The containing view
 */
PhocView *
phoc_view_child_get_view (PhocViewChild *self)
{
  g_assert (PHOC_IS_VIEW_CHILD (self));

  return self->view;
}
