/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3-or-later or MIT
 */

#define G_LOG_DOMAIN "phoc-cursor"

#include "config.h"
#include "server.h"

#define _XOPEN_SOURCE 700
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <linux/input-event-codes.h>
#include "cursor.h"
#include "desktop.h"
#include "utils.h"
#include "view.h"
#include "xcursor.h"


enum {
  PROP_0,
  PROP_SEAT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


G_DEFINE_TYPE (PhocCursor, phoc_cursor, G_TYPE_OBJECT)

static void handle_pointer_motion (struct wl_listener *listener, void *data);
static void handle_pointer_motion_absolute (struct wl_listener *listener, void *data);
static void handle_pointer_button (struct wl_listener *listener, void *data);
static void handle_pointer_axis (struct wl_listener *listener, void *data);
static void handle_pointer_frame (struct wl_listener *listener, void *data);

static void
phoc_cursor_set_property (GObject      *object,
			  guint         property_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
  PhocCursor *self = PHOC_CURSOR (object);

  switch (property_id) {
  case PROP_SEAT:
    self->seat = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_cursor_get_property (GObject    *object,
			  guint       property_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
  PhocCursor *self = PHOC_CURSOR (object);

  switch (property_id) {
  case PROP_SEAT:
    g_value_set_pointer (value, self->seat);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
seat_view_deco_motion (PhocSeatView *view, double deco_sx, double deco_sy)
{
  PhocCursor *self = phoc_seat_get_cursor (view->seat);

  double sx = deco_sx;
  double sy = deco_sy;

  if (view->has_button_grab) {
    sx = view->grab_sx;
    sy = view->grab_sy;
  }

  PhocViewDecoPart parts = view_get_deco_part (view->view, sx, sy);

  bool is_titlebar = (parts & PHOC_VIEW_DECO_PART_TITLEBAR);
  uint32_t edges = 0;

  if (parts & PHOC_VIEW_DECO_PART_LEFT_BORDER) {
    edges |= WLR_EDGE_LEFT;
  } else if (parts & PHOC_VIEW_DECO_PART_RIGHT_BORDER) {
    edges |= WLR_EDGE_RIGHT;
  } else if (parts & PHOC_VIEW_DECO_PART_BOTTOM_BORDER) {
    edges |= WLR_EDGE_BOTTOM;
  } else if (parts & PHOC_VIEW_DECO_PART_TOP_BORDER) {
    edges |= WLR_EDGE_TOP;
  }

  if (view->has_button_grab) {
    if (is_titlebar) {
      phoc_seat_begin_move (view->seat, view->view);
    } else if (edges) {
      phoc_seat_begin_resize (view->seat, view->view, edges);
    }
    view->has_button_grab = false;
  } else {
    if (is_titlebar) {
      phoc_seat_maybe_set_cursor (self->seat, NULL);
    } else if (edges) {
      const char *resize_name = wlr_xcursor_get_resize_name (edges);
      phoc_seat_maybe_set_cursor (self->seat, resize_name);
    }
  }
}

static void
seat_view_deco_leave (PhocSeatView *view)
{
  PhocCursor *self = phoc_seat_get_cursor (view->seat);

  phoc_seat_maybe_set_cursor (self->seat, NULL);
  view->has_button_grab = false;
}

static void
seat_view_deco_button (PhocSeatView *view, double sx,
                       double sy, uint32_t button, uint32_t state)
{
  if (button == BTN_LEFT && state == WLR_BUTTON_PRESSED) {
    view->has_button_grab = true;
    view->grab_sx = sx;
    view->grab_sy = sy;
  } else {
    view->has_button_grab = false;
  }

  PhocViewDecoPart parts = view_get_deco_part (view->view, sx, sy);

  if (state == WLR_BUTTON_RELEASED && (parts & PHOC_VIEW_DECO_PART_TITLEBAR)) {
    phoc_seat_maybe_set_cursor (view->seat, NULL);
  }
}

static bool
roots_handle_shell_reveal (struct wlr_surface *surface, double lx, double ly, int threshold)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;

  if (surface) {
    struct wlr_surface *root = wlr_surface_get_root_surface (surface), *iter = root;

    while (wlr_surface_is_xdg_surface (iter)) {
      struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_from_wlr_surface (iter);
      if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        iter = xdg_surface->popup->parent;
      } else {
        break;
      }
    }

    if (wlr_surface_is_layer_surface (iter)) {
      return false;
    }
  }

  struct wlr_output *wlr_output = wlr_output_layout_output_at (desktop->layout, lx, ly);

  if (!wlr_output) {
    return false;
  }

  PhocOutput *output = wlr_output->data;

  struct wlr_box *output_box =
    wlr_output_layout_get_box (desktop->layout, wlr_output);

  PhocLayerSurface *roots_surface;
  bool left = false, right = false, top = false, bottom = false;

  wl_list_for_each (roots_surface, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], link) {
    struct wlr_layer_surface_v1 *layer = roots_surface->layer_surface;
    struct wlr_layer_surface_v1_state *state = &layer->current;
    const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                                | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                               | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    if (state->anchor == (both_horiz | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
      top = true;
    }
    if (state->anchor == (both_horiz | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
      bottom = true;
    }
    if (state->anchor == (both_vert | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
      left = true;
    }
    if (state->anchor == (both_vert | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
      right = true;
    }
  }

  if ((top    && ly <= output_box->y + threshold) ||
      (bottom && ly >= output_box->y + output_box->height - 1 - threshold) ||
      (left   && lx <= output_box->x + threshold) ||
      (right  && lx >= output_box->x + output_box->width - 1 - threshold)) {
    if (output->fullscreen_view) {
      output->force_shell_reveal = true;
      phoc_layer_shell_update_focus ();
      phoc_output_damage_whole (output);
    }
    return true;
  } else {
    if (output->force_shell_reveal) {
      output->force_shell_reveal = false;
      phoc_layer_shell_update_focus ();
      phoc_output_damage_whole (output);
    }
  }

  return false;
}

static void
roots_passthrough_cursor (PhocCursor *self,
                          uint32_t    time)
{
  PhocServer *server = phoc_server_get_default ();
  double sx, sy;
  PhocView *view = NULL;
  PhocSeat *seat = self->seat;
  PhocDesktop *desktop = server->desktop;
  struct wlr_surface *surface = phoc_desktop_surface_at (desktop,
                                                         self->cursor->x, self->cursor->y, &sx, &sy, &view);

  struct wl_client *client = NULL;

  if (surface) {
    client = wl_resource_get_client (surface->resource);
  }

  if (surface && !phoc_seat_allow_input (seat, surface->resource)) {
    return;
  }

  if (self->cursor_client != client || !client) {
    phoc_seat_maybe_set_cursor (seat, NULL);
    self->cursor_client = client;
  }

  if (view) {
    PhocSeatView *seat_view = phoc_seat_view_from_view (seat, view);

    if (self->pointer_view &&
        !self->wlr_surface && (surface || seat_view != self->pointer_view)) {
      seat_view_deco_leave (self->pointer_view);
    }

    self->pointer_view = seat_view;

    if (!surface) {
      seat_view_deco_motion (seat_view, sx, sy);
    }
  } else {
    self->pointer_view = NULL;
  }

  self->wlr_surface = surface;

  if (surface) {
    wlr_seat_pointer_notify_enter (seat->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion (seat->seat, time, sx, sy);
  } else {
    wlr_seat_pointer_clear_focus (seat->seat);
  }

  if (seat->drag_icon != NULL) {
    phoc_drag_icon_update_position (seat->drag_icon);
  }
}

static inline int64_t
timespec_to_msec (const struct timespec *a)
{
  return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}


static void
phoc_cursor_constructed (GObject *object)
{
  PhocCursor *self = PHOC_CURSOR (object);
  struct wlr_cursor *wlr_cursor = self->cursor;

  g_assert (self->cursor);
  self->xcursor_manager = wlr_xcursor_manager_create (NULL, PHOC_XCURSOR_SIZE);
  g_assert (self->xcursor_manager);

  wl_signal_add (&wlr_cursor->events.motion, &self->motion);
  self->motion.notify = handle_pointer_motion;

  wl_signal_add (&wlr_cursor->events.motion_absolute, &self->motion_absolute);
  self->motion_absolute.notify = handle_pointer_motion_absolute;

  wl_signal_add (&wlr_cursor->events.button, &self->button);
  self->button.notify = handle_pointer_button;

  wl_signal_add (&wlr_cursor->events.axis, &self->axis);
  self->axis.notify = handle_pointer_axis;

  wl_signal_add (&wlr_cursor->events.frame, &self->frame);
  self->frame.notify = handle_pointer_frame;

  G_OBJECT_CLASS (phoc_cursor_parent_class)->constructed (object);
}


static void
phoc_cursor_finalize (GObject *object)
{
  PhocCursor *self = PHOC_CURSOR (object);

  wl_list_remove (&self->motion.link);
  wl_list_remove (&self->motion_absolute.link);
  wl_list_remove (&self->button.link);
  wl_list_remove (&self->axis.link);
  wl_list_remove (&self->frame.link);
  wl_list_remove (&self->swipe_begin.link);
  wl_list_remove (&self->swipe_update.link);
  wl_list_remove (&self->swipe_end.link);
  wl_list_remove (&self->pinch_begin.link);
  wl_list_remove (&self->pinch_update.link);
  wl_list_remove (&self->pinch_end.link);
  wl_list_remove (&self->touch_down.link);
  wl_list_remove (&self->touch_up.link);
  wl_list_remove (&self->touch_motion.link);
  wl_list_remove (&self->tool_axis.link);
  wl_list_remove (&self->tool_tip.link);
  wl_list_remove (&self->tool_proximity.link);
  wl_list_remove (&self->tool_button.link);
  wl_list_remove (&self->request_set_cursor.link);
  wl_list_remove (&self->focus_change.link);

  g_clear_pointer (&self->xcursor_manager, wlr_xcursor_manager_destroy);
  g_clear_pointer (&self->cursor, wlr_cursor_destroy);

  G_OBJECT_CLASS (phoc_cursor_parent_class)->finalize (object);
}


static void
phoc_cursor_class_init (PhocCursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phoc_cursor_constructed;
  object_class->finalize = phoc_cursor_finalize;
  object_class->get_property = phoc_cursor_get_property;
  object_class->set_property = phoc_cursor_set_property;

  props[PROP_SEAT] =
    g_param_spec_pointer ("seat",
			  "",
			  "",
			  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_cursor_init (PhocCursor *self)
{
  self->cursor = wlr_cursor_create ();
  self->default_xcursor = PHOC_XCURSOR_DEFAULT;
}


void
phoc_cursor_update_focus (PhocCursor *self)
{
  struct timespec now;

  clock_gettime (CLOCK_MONOTONIC, &now);

  roots_passthrough_cursor (self, timespec_to_msec (&now));
}

void
phoc_cursor_update_position (PhocCursor *self,
                             uint32_t    time)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  PhocSeat *seat = self->seat;
  PhocView *view;

  switch (self->mode) {
  case PHOC_CURSOR_PASSTHROUGH:
    roots_passthrough_cursor (self, time);
    break;
  case PHOC_CURSOR_MOVE:
    view = phoc_seat_get_focus (seat);
    if (view != NULL) {
      struct wlr_box geom;
      view_get_geometry (view, &geom);
      double dx = self->cursor->x - self->offs_x;
      double dy = self->cursor->y - self->offs_y;

      struct wlr_output *wlr_output = wlr_output_layout_output_at (desktop->layout, self->cursor->x, self->cursor->y);
      struct wlr_box *output_box = wlr_output_layout_get_box (desktop->layout, wlr_output);

      bool output_is_landscape = output_box->width > output_box->height;

      if (view_is_fullscreen (view)) {
        phoc_view_set_fullscreen (view, true, wlr_output);
      } else if (self->cursor->y < output_box->y + PHOC_EDGE_SNAP_THRESHOLD) {
        view_maximize (view, wlr_output);
      } else if (output_is_landscape && self->cursor->x < output_box->x + PHOC_EDGE_SNAP_THRESHOLD) {
        view_tile (view, PHOC_VIEW_TILE_LEFT, wlr_output);
      } else if (output_is_landscape && self->cursor->x > output_box->x + output_box->width - PHOC_EDGE_SNAP_THRESHOLD) {
        view_tile (view, PHOC_VIEW_TILE_RIGHT, wlr_output);
      } else {
        view_restore (view);
        view_move (view, self->view_x + dx - geom.x * view->scale,
                   self->view_y + dy - geom.y * view->scale);
      }
    }
    break;
  case PHOC_CURSOR_RESIZE:
    view = phoc_seat_get_focus (seat);
    if (view != NULL) {
      struct wlr_box geom;
      view_get_geometry (view, &geom);
      double dx = self->cursor->x - self->offs_x;
      double dy = self->cursor->y - self->offs_y;
      double x = view->box.x;
      double y = view->box.y;
      int width = self->view_width;
      int height = self->view_height;
      if (self->resize_edges & WLR_EDGE_TOP) {
        y = self->view_y + dy - geom.y * view->scale;
        height -= dy;
        if (height < 1) {
          y += height;
        }
      } else if (self->resize_edges & WLR_EDGE_BOTTOM) {
        height += dy;
      }
      if (self->resize_edges & WLR_EDGE_LEFT) {
        x = self->view_x + dx - geom.x * view->scale;
        width -= dx;
        if (width < 1) {
          x += width;
        }
      } else if (self->resize_edges & WLR_EDGE_RIGHT) {
        width += dx;
      }
      view_move_resize (view, x, y,
                        width < 1 ? 1 : width,
                        height < 1 ? 1 : height);
    }
    break;
  default:
    g_error ("Invalid cursor mode %d", self->mode);
  }
}

static void
phoc_cursor_press_button (PhocCursor *self,
                          struct wlr_input_device *device, uint32_t time, uint32_t button,
                          uint32_t state, double lx, double ly)
{
  PhocServer *server = phoc_server_get_default ();
  PhocSeat *seat = self->seat;
  PhocDesktop *desktop = server->desktop;

  bool is_touch = device->type == WLR_INPUT_DEVICE_TOUCH;

  double sx, sy;
  PhocView *view;
  struct wlr_surface *surface = phoc_desktop_surface_at (desktop,
                                                         lx, ly, &sx, &sy, &view);

  if (state == WLR_BUTTON_PRESSED && view &&
      phoc_seat_has_meta_pressed (seat)) {
    phoc_seat_set_focus (seat, view);

    uint32_t edges;
    switch (button) {
    case BTN_LEFT:
      phoc_seat_begin_move (seat, view);
      break;
    case BTN_RIGHT:
      edges = 0;
      if (sx < view->wlr_surface->current.width/2) {
        edges |= WLR_EDGE_LEFT;
      } else {
        edges |= WLR_EDGE_RIGHT;
      }
      if (sy < view->wlr_surface->current.height/2) {
        edges |= WLR_EDGE_TOP;
      } else {
        edges |= WLR_EDGE_BOTTOM;
      }
      phoc_seat_begin_resize (seat, view, edges);
      break;
    default:
      /* don't care */
      break;
    }
  } else {
    if (view && !surface && self->pointer_view) {
      seat_view_deco_button (self->pointer_view,
                             sx, sy, button, state);
    }

    if (state == WLR_BUTTON_RELEASED &&
        self->mode != PHOC_CURSOR_PASSTHROUGH) {
      self->mode = PHOC_CURSOR_PASSTHROUGH;
      phoc_cursor_update_focus (self);
    }

    if (state == WLR_BUTTON_PRESSED) {
      if (view) {
        phoc_seat_set_focus (seat, view);
      }
      if (surface && wlr_surface_is_layer_surface (surface)) {
        struct wlr_layer_surface_v1 *layer =
          wlr_layer_surface_v1_from_wlr_surface (surface);
        if (layer->current.keyboard_interactive) {
          phoc_seat_set_focus_layer (seat, layer);
        }
      }
    }
  }

  if (!roots_handle_shell_reveal (surface, lx, ly, PHOC_SHELL_REVEAL_POINTER_THRESHOLD) && !is_touch) {
    wlr_seat_pointer_notify_button (seat->seat, time, button, state);
  }
}


static void
handle_pointer_motion (struct wl_listener *listener, void *data)
{
  PhocCursor *self = wl_container_of (listener, self, motion);
  struct wlr_event_pointer_motion *event = data;
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  double dx = event->delta_x;
  double dy = event->delta_y;

  double dx_unaccel = event->unaccel_dx;
  double dy_unaccel = event->unaccel_dy;

  wlr_idle_notify_activity (desktop->idle, self->seat->seat);

  wlr_relative_pointer_manager_v1_send_relative_motion (
    server->desktop->relative_pointer_manager,
    self->seat->seat, (uint64_t)event->time_msec * 1000, dx, dy,
    dx_unaccel, dy_unaccel);

  if (self->active_constraint) {
    PhocView *view = self->pointer_view->view;
    assert (view);

    double lx1 = self->cursor->x;
    double ly1 = self->cursor->y;

    double lx2 = lx1 + dx;
    double ly2 = ly1 + dy;

    double sx1 = lx1 - view->box.x;
    double sy1 = ly1 - view->box.y;

    double sx2 = lx2 - view->box.x;
    double sy2 = ly2 - view->box.y;

    double sx2_confined, sy2_confined;
    if (!wlr_region_confine (&self->confine, sx1, sy1, sx2, sy2,
                             &sx2_confined, &sy2_confined)) {
      return;
    }

    dx = sx2_confined - sx1;
    dy = sy2_confined - sy1;
  }

  wlr_cursor_move (self->cursor, event->device, dx, dy);
  phoc_cursor_update_position (self, event->time_msec);
}

static void
handle_pointer_motion_absolute (struct wl_listener *listener, void *data)
{
  PhocCursor *self = wl_container_of (listener, self, motion_absolute);
  struct wlr_event_pointer_motion_absolute *event = data;
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  double lx, ly;

  wlr_idle_notify_activity (desktop->idle, self->seat->seat);
  wlr_cursor_absolute_to_layout_coords (self->cursor, event->device, event->x,
                                        event->y, &lx, &ly);

  double dx = lx - self->cursor->x;
  double dy = ly - self->cursor->y;

  wlr_relative_pointer_manager_v1_send_relative_motion (
    server->desktop->relative_pointer_manager,
    self->seat->seat, (uint64_t)event->time_msec * 1000, dx, dy, dx, dy);

  if (self->pointer_view) {
    PhocView *view = self->pointer_view->view;

    if (self->active_constraint &&
        !pixman_region32_contains_point (&self->confine,
                                         floor (lx - view->box.x), floor (ly - view->box.y), NULL)) {
      return;
    }
  }

  wlr_cursor_warp_closest (self->cursor, event->device, lx, ly);
  phoc_cursor_update_position (self, event->time_msec);
}

static void
handle_pointer_button (struct wl_listener *listener, void *data)
{
  PhocCursor *self = wl_container_of (listener, self, button);
  struct wlr_event_pointer_button *event = data;
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, self->seat->seat);
  phoc_cursor_press_button (self, event->device, event->time_msec,
                            event->button, event->state, self->cursor->x, self->cursor->y);
}

static void
handle_pointer_axis (struct wl_listener *listener, void *data)
{
  PhocCursor *self = wl_container_of (listener, self, axis);
  struct wlr_event_pointer_axis *event = data;
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, self->seat->seat);
  wlr_seat_pointer_notify_axis (self->seat->seat, event->time_msec,
                                event->orientation, event->delta, event->delta_discrete, event->source);
}

static void
handle_pointer_frame (struct wl_listener *listener, void *data)
{
  PhocCursor *self = wl_container_of (listener, self, frame);
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, self->seat->seat);
  wlr_seat_pointer_notify_frame (self->seat->seat);
}

void
phoc_cursor_handle_touch_down (PhocCursor                  *self,
                               struct wlr_event_touch_down *event)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  PhocSeat *seat = self->seat;
  double lx, ly;

  wlr_cursor_absolute_to_layout_coords (self->cursor, event->device,
                                        event->x, event->y, &lx, &ly);

  if (seat->touch_id == -1 && self->mode == PHOC_CURSOR_PASSTHROUGH) {
    seat->touch_id = event->touch_id;
    seat->touch_x = lx;
    seat->touch_y = ly;
  }

  double sx, sy;
  PhocView *view;
  struct wlr_surface *surface = phoc_desktop_surface_at (
    desktop, lx, ly, &sx, &sy, &view);
  bool shell_revealed = roots_handle_shell_reveal (surface, lx, ly, PHOC_SHELL_REVEAL_TOUCH_THRESHOLD);

  if (!shell_revealed && surface && phoc_seat_allow_input (seat, surface->resource)) {
    wlr_seat_touch_notify_down (seat->seat, surface,
                                event->time_msec, event->touch_id, sx, sy);
    wlr_seat_touch_point_focus (seat->seat, surface,
                                event->time_msec, event->touch_id, sx, sy);

    if (view)
      phoc_seat_set_focus (seat, view);

    if (wlr_surface_is_layer_surface (surface)) {
      struct wlr_layer_surface_v1 *layer =
        wlr_layer_surface_v1_from_wlr_surface (surface);
      if (layer->current.keyboard_interactive) {
        phoc_seat_set_focus_layer (seat, layer);
      }
    }
  }

  if (server->debug_flags & PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS) {
    PhocOutput *output;
    wl_list_for_each (output, &desktop->outputs, link) {
      if (wlr_output_layout_contains_point (desktop->layout, output->wlr_output, lx, ly)) {
        double ox = lx, oy = ly;
        wlr_output_layout_output_coords (desktop->layout, output->wlr_output, &ox, &oy);
        struct wlr_box box = {
          .x = ox,
          .y = oy,
          .width = 1,
          .height = 1
        };
        wlr_output_damage_add_box (output->damage, &box);
      }
    }
  }
}

void
phoc_cursor_handle_touch_up (PhocCursor                *self,
                             struct wlr_event_touch_up *event)
{
  struct wlr_touch_point *point =
    wlr_seat_touch_get_point (self->seat->seat, event->touch_id);

  if (self->seat->touch_id == event->touch_id)
    self->seat->touch_id = -1;

  if (!point)
    return;

  if (self->mode != PHOC_CURSOR_PASSTHROUGH) {
    self->mode = PHOC_CURSOR_PASSTHROUGH;
    phoc_cursor_update_focus (self);
  }

  wlr_seat_touch_notify_up (self->seat->seat, event->time_msec,
                            event->touch_id);
}

void
phoc_cursor_handle_touch_motion (PhocCursor                    *self,
                                 struct wlr_event_touch_motion *event)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  struct wlr_touch_point *point =
    wlr_seat_touch_get_point (self->seat->seat, event->touch_id);

  if (!point)
    return;

  double lx, ly;

  wlr_cursor_absolute_to_layout_coords (self->cursor, event->device,
                                        event->x, event->y, &lx, &ly);
  struct wlr_output *wlr_output =
    wlr_output_layout_output_at (desktop->layout, lx, ly);

  if (!wlr_output)
    return;

  PhocOutput *phoc_output = wlr_output->data;

  double sx, sy;
  struct wlr_surface *surface = point->focus_surface;

  // TODO: test with input regions

  if (surface) {
    bool found = false;
    float scale = 1.0;

    struct wlr_surface *root = wlr_surface_get_root_surface (surface);
    if (wlr_surface_is_layer_surface (root)) {
      struct wlr_layer_surface_v1 *layer_surface = wlr_layer_surface_v1_from_wlr_surface (root);
      struct wlr_box *output_box = wlr_output_layout_get_box (desktop->layout, wlr_output);

      PhocLayerSurface *layer;
      wl_list_for_each_reverse (layer, &phoc_output->layers[layer_surface->current.layer], link)
      {
        if (layer->layer_surface->surface == root) {
          sx = lx - layer->geo.x - output_box->x;
          sy = ly - layer->geo.y - output_box->y;
          found = true;
          break;
        }
      }
      // try the overlay layer as well since the on-screen keyboard might have been elevated there
      wl_list_for_each_reverse (layer, &phoc_output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], link)
      {
        if (layer->layer_surface->surface == root) {
          sx = lx - layer->geo.x - output_box->x;
          sy = ly - layer->geo.y - output_box->y;
          found = true;
          break;
        }
      }
    } else {
      PhocView *view = phoc_view_from_wlr_surface (root);
      if (view) {
        scale = view->scale;
        sx = lx / scale - view->box.x;
        sy = ly / scale - view->box.y;
        found = true;
      } else {
        // FIXME: buggy fallback, but at least handles xdg_popups for now...
        surface = phoc_desktop_surface_at (desktop, lx, ly, &sx, &sy, NULL);
      }
    }

    if (found) {
      struct wlr_surface *sub = surface;
      while (sub && wlr_surface_is_subsurface (sub)) {
        struct wlr_subsurface *subsurface = wlr_subsurface_from_wlr_surface (sub);
        sx -= subsurface->current.x;
        sy -= subsurface->current.y;
        sub = subsurface->parent;
      }
    }
  }

  if (surface && phoc_seat_allow_input (self->seat, surface->resource)) {
    wlr_seat_touch_notify_motion (self->seat->seat, event->time_msec,
                                  event->touch_id, sx, sy);
  }

  if (event->touch_id == self->seat->touch_id) {
    self->seat->touch_x = lx;
    self->seat->touch_y = ly;

    if (self->mode != PHOC_CURSOR_PASSTHROUGH) {
      wlr_cursor_warp (self->cursor, NULL, lx, ly);
      phoc_cursor_update_position (self, event->time_msec);
    }
  }
}

void
phoc_cursor_handle_tool_axis (PhocCursor                        *self,
                              struct wlr_event_tablet_tool_axis *event)
{
  double x = NAN, y = NAN;

  if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X) &&
      (event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
    x = event->x;
    y = event->y;
  } else if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X)) {
    x = event->x;
  } else if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
    y = event->y;
  }

  double lx, ly;

  wlr_cursor_absolute_to_layout_coords (self->cursor, event->device,
                                        x, y, &lx, &ly);


  if (self->pointer_view) {
    PhocView *view = self->pointer_view->view;

    if (self->active_constraint &&
        !pixman_region32_contains_point (&self->confine,
                                         floor (lx - view->box.x), floor (ly - view->box.y), NULL)) {
      return;
    }
  }

  wlr_cursor_warp_closest (self->cursor, event->device, lx, ly);
  phoc_cursor_update_position (self, event->time_msec);
}

void
phoc_cursor_handle_tool_tip (PhocCursor                       *self,
                             struct wlr_event_tablet_tool_tip *event)
{
  phoc_cursor_press_button (self, event->device,
                            event->time_msec, BTN_LEFT, event->state, self->cursor->x,
                            self->cursor->y);
}

void
phoc_cursor_handle_request_set_cursor (PhocCursor                                       *self,
                                       struct wlr_seat_pointer_request_set_cursor_event *event)
{
  struct wlr_surface *focused_surface =
    event->seat_client->seat->pointer_state.focused_surface;
  bool has_focused =
    focused_surface != NULL && focused_surface->resource != NULL;
  struct wl_client *focused_client = NULL;

  if (has_focused) {
    focused_client = wl_resource_get_client (focused_surface->resource);
  }
  if (event->seat_client->client != focused_client ||
      self->mode != PHOC_CURSOR_PASSTHROUGH) {
    g_debug ("Denying request to set cursor from unfocused client");
    return;
  }

  wlr_cursor_set_surface (self->cursor, event->surface, event->hotspot_x,
                          event->hotspot_y);
  self->cursor_client = event->seat_client->client;
}

void
phoc_cursor_handle_focus_change (PhocCursor                                 *self,
                                 struct wlr_seat_pointer_focus_change_event *event)
{
  PhocServer *server = phoc_server_get_default ();
  double sx = event->sx;
  double sy = event->sy;

  double lx = self->cursor->x;
  double ly = self->cursor->y;

  g_debug ("entered surface %p, lx: %f, ly: %f, sx: %f, sy: %f",
           event->new_surface, lx, ly, sx, sy);

  phoc_cursor_constrain (self,
                         wlr_pointer_constraints_v1_constraint_for_surface (
                           server->desktop->pointer_constraints,
                           event->new_surface, self->seat->seat),
                         sx, sy);
}

void
phoc_cursor_handle_constraint_commit (PhocCursor *self)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;

  double sx, sy;
  struct wlr_surface *surface = phoc_desktop_surface_at (desktop,
                                                         self->cursor->x,
							 self->cursor->y,
							 &sx, &sy, NULL);

  // This should never happen but views move around right when they're
  // created from (0, 0) to their actual coordinates.
  if (surface != self->active_constraint->surface)
    phoc_cursor_update_focus (self);
  else
    phoc_cursor_constrain (self, self->active_constraint, sx, sy);
}

static void
handle_constraint_commit (struct wl_listener *listener,
                          void               *data)
{
  PhocCursor *self = wl_container_of (listener, self, constraint_commit);

  assert (self->active_constraint->surface == data);
  phoc_cursor_handle_constraint_commit (self);
}

void
phoc_cursor_constrain (PhocCursor *self,
                       struct wlr_pointer_constraint_v1 *constraint,
		       double sx, double sy)
{
  if (self->active_constraint == constraint)
    return;

  g_debug ("phoc_cursor_constrain(%p, %p)",
           self, constraint);
  g_debug ("self->active_constraint: %p",
           self->active_constraint);

  wl_list_remove (&self->constraint_commit.link);
  wl_list_init (&self->constraint_commit.link);
  if (self->active_constraint) {
    wlr_pointer_constraint_v1_send_deactivated (
      self->active_constraint);
  }

  self->active_constraint = constraint;

  if (constraint == NULL)
    return;

  wlr_pointer_constraint_v1_send_activated (constraint);

  wl_list_remove (&self->constraint_commit.link);
  wl_signal_add (&constraint->surface->events.commit,
                 &self->constraint_commit);
  self->constraint_commit.notify = handle_constraint_commit;

  pixman_region32_clear (&self->confine);

  pixman_region32_t *region = &constraint->region;

  if (!pixman_region32_contains_point (region, floor (sx), floor (sy), NULL)) {
    // Warp into region if possible
    int nboxes;
    pixman_box32_t *boxes = pixman_region32_rectangles (region, &nboxes);
    if (nboxes > 0) {
      PhocView *view = self->pointer_view->view;

      double lx = view->box.x + (boxes[0].x1 + boxes[0].x2) / 2.;
      double ly = view->box.y + (boxes[0].y1 + boxes[0].y2) / 2.;

      wlr_cursor_warp_closest (self->cursor, NULL, lx, ly);
    }
  }

  // A locked pointer will result in an empty region, thus disallowing all movement
  if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
    pixman_region32_copy (&self->confine, region);
  }
}


PhocCursor *
phoc_cursor_new (PhocSeat *seat)
{
  return PHOC_CURSOR (g_object_new (PHOC_TYPE_CURSOR,
				    "seat", seat,
				    NULL));
}
