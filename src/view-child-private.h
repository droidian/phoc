/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>

G_BEGIN_DECLS

/**
 * PhocViewChild:
 * @link: Link to PhocView::child_surfaces
 * @view: The [type@PhocView] this child belongs to
 * @parent: (nullable): The parent of this child if another child
 * @children: (nullable): children of this child
 *
 * A child of a [type@View], e.g. a [type@XdgPopup] or subsurface
 */
typedef struct _PhocView PhocView;

typedef struct _PhocViewChild PhocViewChild;
struct _PhocViewChild {
  GObject                      parent_instance;

  PhocView                     *view;
  PhocViewChild                *parent;
  GSList                       *children;
  struct wlr_surface           *wlr_surface;
  struct wl_list                link; // PhocViewPrivate::child_surfaces
  bool                          mapped;

  struct wl_listener            map;
  struct wl_listener            unmap;
  struct wl_listener            commit;
  struct wl_listener            new_subsurface;
};

/**
 * PhocViewChildClass:
 * @parent_class: The object class structure needs to be the first
 *   element in the widget class structure in order for the class mechanism
 *   to work correctly. This allows a PhocViewClass pointer to be cast to
 *   a GObjectClass pointer.
 * @map: Invoked on map. Chain up to parent.
 * @unmap: Invoked on unmap. Chain up to parent.
 * @get_pos: Get the child's position relative to it's parent.
 */
typedef struct _PhocViewChildClass
{
  GObjectClass parent_class;

  void               (*map) (PhocViewChild *self);
  void               (*unmap) (PhocViewChild *self);
  void               (*get_pos) (PhocViewChild *self, int *sx, int *sy);
} PhocViewChildClass;

#define PHOC_TYPE_VIEW_CHILD (phoc_view_child_get_type ())

GType phoc_view_child_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PhocViewChild, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PhocViewChildClass, g_type_class_unref)
static inline PhocViewChild * PHOC_VIEW_CHILD (gpointer ptr) {
  return G_TYPE_CHECK_INSTANCE_CAST (ptr, phoc_view_child_get_type (), PhocViewChild); }
static inline PhocViewChildClass * PHOC_VIEW_CHILD_CLASS (gpointer ptr) {
  return G_TYPE_CHECK_CLASS_CAST (ptr, phoc_view_child_get_type (), PhocViewChildClass); }
static inline gboolean PHOC_IS_VIEW_CHILD (gpointer ptr) {
  return G_TYPE_CHECK_INSTANCE_TYPE (ptr, phoc_view_child_get_type ()); }
static inline gboolean PHOC_IS_VIEW_CHILD_CLASS (gpointer ptr) {
  return G_TYPE_CHECK_CLASS_TYPE (ptr, phoc_view_child_get_type ()); }
static inline PhocViewChildClass * PHOC_VIEW_CHILD_GET_CLASS (gpointer ptr) {
  return G_TYPE_INSTANCE_GET_CLASS (ptr, phoc_view_child_get_type (), PhocViewChildClass); }

PhocView *            phoc_view_child_get_view (PhocViewChild *self);
void                  phoc_view_child_apply_damage (PhocViewChild *self);
void                  phoc_view_child_damage_whole (PhocViewChild *self);
void                  phoc_view_child_get_pos (PhocViewChild *self, int *sx, int *sy);

G_END_DECLS
