/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-drag-icon"

#include "cursor.h"
#include "server.h"
#include "desktop.h"
#include "drag-icon.h"

#include <wlr/types/wlr_compositor.h>

/**
 * PhocDragIcon:
 *
 * The icon used during drag and drop operations
 */
struct _PhocDragIcon {
  PhocSeat             *seat;
  struct wlr_drag_icon *wlr_drag_icon;

  double                x, y;
  double                dx, dy;

  struct wl_listener    surface_commit;
  struct wl_listener    map;
  struct wl_listener    unmap;
  struct wl_listener    destroy;
};


static void
phoc_drag_icon_damage_whole (PhocDragIcon *icon)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocOutput *output;

  wl_list_for_each (output, &desktop->outputs, link)
    phoc_output_damage_whole_drag_icon (output, icon);
}


void
phoc_drag_icon_update_position (PhocDragIcon *self)
{
  phoc_drag_icon_damage_whole (self);

  PhocSeat *seat = self->seat;
  struct wlr_drag *wlr_drag = self->wlr_drag_icon->drag;

  g_assert (wlr_drag != NULL);

  switch (seat->seat->drag->grab_type) {
  case WLR_DRAG_GRAB_KEYBOARD_POINTER:;
    struct wlr_cursor *cursor = seat->cursor->cursor;
    self->x = cursor->x + self->dx;
    self->y = cursor->y + self->dy;
    break;
  case WLR_DRAG_GRAB_KEYBOARD_TOUCH:;
    struct wlr_touch_point *point = wlr_seat_touch_get_point (seat->seat, wlr_drag->touch_id);
    if (point == NULL)
      return;

    self->x = seat->touch_x + self->dx;
    self->y = seat->touch_y + self->dy;
    break;
  case WLR_DRAG_GRAB_KEYBOARD:
  default:
    g_error ("Invalid drag grab type %d", seat->seat->drag->grab_type);
  }

  phoc_drag_icon_damage_whole (self);
}


static void
phoc_drag_icon_handle_surface_commit (struct wl_listener *listener, void *data)
{
  PhocDragIcon *self = wl_container_of (listener, self, surface_commit);
  struct wlr_drag_icon *wlr_icon = self->wlr_drag_icon;

  self->dx += wlr_icon->surface->current.dx;
  self->dy += wlr_icon->surface->current.dy;

  phoc_drag_icon_update_position (self);
}


static void
phoc_drag_icon_handle_map (struct wl_listener *listener, void *data)
{
  PhocDragIcon *icon = wl_container_of (listener, icon, map);

  phoc_drag_icon_damage_whole (icon);
}


static void
phoc_drag_icon_handle_unmap (struct wl_listener *listener, void *data)
{
  PhocDragIcon *icon = wl_container_of (listener, icon, unmap);

  phoc_drag_icon_damage_whole (icon);
}


static void
phoc_drag_icon_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocDragIcon *self = wl_container_of (listener, self, destroy);

  phoc_drag_icon_damage_whole (self);

  g_assert (self->seat->drag_icon == self);
  self->seat->drag_icon = NULL;

  wl_list_remove (&self->surface_commit.link);
  wl_list_remove (&self->unmap.link);
  wl_list_remove (&self->destroy.link);
  free (self);
}

/**
 * phoc_drag_icon_create: (skip)
 * @seat: The seat the drag icon is on
 * @icon: The WLR drag icon this icon should be created from
 *
 * Create a new drag icon.
 *
 * Return: The new drag icon
 */
PhocDragIcon *
phoc_drag_icon_create (PhocSeat *seat, struct wlr_drag_icon *wlr_drag_icon)
{
  PhocDragIcon *self = g_new0 (PhocDragIcon, 1);

  self->seat = seat;
  self->wlr_drag_icon = wlr_drag_icon;

  self->surface_commit.notify = phoc_drag_icon_handle_surface_commit;
  wl_signal_add (&wlr_drag_icon->surface->events.commit, &self->surface_commit);
  self->unmap.notify = phoc_drag_icon_handle_unmap;
  wl_signal_add (&wlr_drag_icon->surface->events.unmap, &self->unmap);
  self->map.notify = phoc_drag_icon_handle_map;
  wl_signal_add (&wlr_drag_icon->surface->events.map, &self->map);
  self->destroy.notify = phoc_drag_icon_handle_destroy;
  wl_signal_add (&wlr_drag_icon->events.destroy, &self->destroy);

  phoc_drag_icon_update_position (self);

  return self;
}

/**
 * phoc_drag_icon_is_mapped:
 * @self: (nullable): The drag icon to check
 *
 * Check if a [type@DragIcon] is currently mapped
 *
 * Returns: %TRUE if a view is currently mapped, otherwise %FALSE
 */
gboolean
phoc_drag_icon_is_mapped (PhocDragIcon *self)
{
  return self && self->wlr_drag_icon->surface->mapped;
}


double
phoc_drag_icon_get_x (PhocDragIcon *self)
{
  return self->x;
}


double
phoc_drag_icon_get_y (PhocDragIcon *self)
{
  return self->y;
}


struct wlr_surface *
phoc_drag_icon_get_wlr_surface (PhocDragIcon *self)
{
  return self->wlr_drag_icon->surface;
}
