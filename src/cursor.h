/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_tablet_tool.h>
#include "seat.h"
#include "event.h"
#include "gesture.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_XCURSOR_SIZE 24

#define PHOC_XCURSOR_DEFAULT "left_ptr"
#define PHOC_XCURSOR_MOVE "grabbing"
#define PHOC_XCURSOR_ROTATE "grabbing"

#define PHOC_TYPE_CURSOR (phoc_cursor_get_type ())

G_DECLARE_FINAL_TYPE (PhocCursor, phoc_cursor, PHOC, CURSOR, GObject)

#define PHOC_SHELL_REVEAL_TOUCH_THRESHOLD 10
#define PHOC_SHELL_REVEAL_POINTER_THRESHOLD 0
#define PHOC_EDGE_SNAP_THRESHOLD 20

/**
 * PhocCursorMode:
 * @PHOC_CURSOR_PASSTHROUGH: Cursor is passed through to the client
 * @PHOC_CUSROR_MOVE: Cursor is used for a window move operation
 * @PHOC_CURSOR_RESIZE: Cursor is used for a window resize operation
 *
 * The mode of the cursor.
 */
typedef enum {
  PHOC_CURSOR_PASSTHROUGH = 0,
  PHOC_CURSOR_MOVE = 1,
  PHOC_CURSOR_RESIZE = 2,
} PhocCursorMode;

typedef struct _PhocSeatView PhocSeatView;

/**
 * PhocTouchPoint:
 *
 * A touch point tracked compositor side.
 */
typedef struct PhocTouchPoint {
  int   touch_id;

  double lx;
  double ly;
} PhocTouchPoint;

/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
typedef struct _PhocCursor {
  GObject                           parent;

  PhocSeat                         *seat;
  struct wlr_cursor                *cursor;

  struct wlr_pointer_constraint_v1 *active_constraint;
  pixman_region32_t                 confine; // invalid if active_constraint == NULL

  /* For moving and resizing surfaces */
  int                               offs_x, offs_y;
  int                               view_x, view_y, view_width, view_height;
  uint32_t                          resize_edges;

  /* SeatView (nullable) under the cursor */
  PhocSeatView                     *pointer_view;
  /* Surface (nullable) under the cursor */
  struct wlr_surface               *wlr_surface;

  struct wl_listener                motion;
  struct wl_listener                motion_absolute;
  struct wl_listener                button;
  struct wl_listener                axis;
  struct wl_listener                frame;
  struct wl_listener                swipe_begin;
  struct wl_listener                swipe_update;
  struct wl_listener                swipe_end;
  struct wl_listener                pinch_begin;
  struct wl_listener                pinch_update;
  struct wl_listener                pinch_end;

  struct wl_listener                touch_down;
  struct wl_listener                touch_up;
  struct wl_listener                touch_motion;
  struct wl_listener                touch_frame;

  struct wl_listener                tool_axis;
  struct wl_listener                tool_tip;
  struct wl_listener                tool_proximity;
  struct wl_listener                tool_button;

  struct wl_listener                request_set_cursor;

  struct wl_listener                focus_change;

  struct wl_listener                constraint_commit;


} PhocCursor;

PhocCursor *phoc_cursor_new (PhocSeat                                                    *seat);
void        phoc_cursor_handle_touch_down (PhocCursor                                    *self,
                                           struct wlr_touch_down_event                   *event);
void        phoc_cursor_handle_touch_up (PhocCursor                                      *self,
                                         struct wlr_touch_up_event                       *event);
void        phoc_cursor_handle_touch_motion (PhocCursor                                  *self,
                                             struct wlr_touch_motion_event               *event);
void        phoc_cursor_handle_tool_axis (PhocCursor                                     *self,
                                          struct wlr_tablet_tool_axis_event              *event);
void        phoc_cursor_handle_tool_tip (PhocCursor                                      *self,
                                         struct wlr_tablet_tool_tip_event                *event);
void        phoc_cursor_handle_focus_change (PhocCursor                                  *self,
                                             struct wlr_seat_pointer_focus_change_event  *event);
void        phoc_cursor_update_position (PhocCursor                                      *self,
                                         uint32_t                                         time);
void        phoc_cursor_update_focus (PhocCursor                                         *self);
void        phoc_cursor_constrain (PhocCursor                                            *self,
                                   struct wlr_pointer_constraint_v1                      *constraint,
                                   double                                                 sx,
                                   double                                                 sy);
void        phoc_maybe_set_cursor (PhocCursor                                            *self);
void        phoc_cursor_handle_event             (PhocCursor                             *self,
                                                  PhocEventType                           type,
                                                  gpointer                                event,
                                                  gsize                                   size);

void        phoc_cursor_add_gesture              (PhocCursor                             *self,
                                                  PhocGesture                            *gesture);
GSList     *phoc_cursor_get_gestures             (PhocCursor                             *self);

gboolean    phoc_cursor_is_active_touch_id       (PhocCursor                             *self,
                                                  int                                     touch_id);
void        phoc_cursor_set_name (PhocCursor *self, struct wl_client *client, const char *name);
void        phoc_cursor_set_image (PhocCursor         *self,
                                   struct wl_client   *client,
                                   struct wlr_surface *surface,
                                   int32_t             hotspot_x,
                                   int32_t             hotspot_y);

PhocCursorMode phoc_cursor_get_mode (PhocCursor *self);
void        phoc_cursor_set_mode (PhocCursor *self, PhocCursorMode mode);
void        phoc_cursor_set_xcursor_theme (PhocCursor *self, const char *theme, uint32_t size);
void        phoc_cursor_configure_xcursor (PhocCursor *self);

G_END_DECLS
