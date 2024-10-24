/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "drag-icon.h"
#include "input.h"
#include "input-method-relay.h"
#include "layer-shell.h"

#include <wlr/types/wlr_switch.h>

#include <glib-object.h>

#include <wayland-server-core.h>

G_BEGIN_DECLS

#define PHOC_TYPE_SEAT (phoc_seat_get_type ())

G_DECLARE_FINAL_TYPE (PhocSeat, phoc_seat, PHOC, SEAT, GObject)

typedef struct _PhocCursor PhocCursor;
typedef struct _PhocTablet PhocTablet;

/**
 * PhocSeat:
 * @im_relay: The input method relay for this seat
 *
 * Represents a seat
 */

/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
typedef struct _PhocSeat {
  GObject                         parent;

  struct wlr_seat                *seat;
  PhocCursor                     *cursor;

  /* Coordinates of the first touch point if it exists */
  int32_t                         touch_id;
  double                          touch_x, touch_y;

  /*  If the focused layer is set, views cannot receive keyboard focus */
  struct wlr_layer_surface_v1    *focused_layer;

  PhocInputMethodRelay            im_relay;

  PhocDragIcon                   *drag_icon; /* (nullable) */

  GSList                         *keyboards; /* (element-type PhocKeyboard) */
  GSList                         *pointers;  /* (element-type PhocPointer) */
  GSList                         *switches;  /* (element-type PhocSwitch) */
  GSList                         *touch;     /* (element-type PhocTouch) */
  GSList                         *tablets;   /* (element-type PhocTablet) */
  struct wl_list                  tablet_pads;

  struct wl_listener              request_set_selection;
  struct wl_listener              request_set_primary_selection;
  struct wl_listener              request_start_drag;
  struct wl_listener              start_drag;
  struct wl_listener              destroy;
} PhocSeat;

/**
 * PhocSeatView:
 *
 * Structure used by [type@Seat] and [type@Cursor] to track its
 * views.
 */
typedef struct _PhocSeatView {
  PhocSeat          *seat;
  PhocView          *view;

  bool               has_button_grab;
  double             grab_sx;
  double             grab_sy;
} PhocSeatView;


typedef struct _PhocTabletPad {
  struct wl_list                   link;
  struct wlr_tablet_v2_tablet_pad *tablet_v2_pad;

  PhocSeat                        *seat;
  struct wlr_input_device         *device;

  struct wl_listener               device_destroy;
  struct wl_listener               attach;
  struct wl_listener               button;
  struct wl_listener               ring;
  struct wl_listener               strip;

  PhocTablet                      *tablet;
  struct wl_listener               tablet_destroy;
} PhocTabletPad;


typedef struct _PhocTabletTool {
  struct wl_list                    link;
  struct wl_list                    tool_link;
  struct wlr_tablet_v2_tablet_tool *tablet_v2_tool;

  PhocSeat                         *seat;
  double                            tilt_x, tilt_y;

  struct wl_listener                set_cursor;
  struct wl_listener                tool_destroy;

  PhocTablet                       *current_tablet;
  struct wl_listener                tablet_destroy;
} PhocTabletTool;


typedef struct PhocPointerConstraint {
  struct wlr_pointer_constraint_v1 *constraint;

  struct wl_listener                destroy;
} PhocPointerConstraint;


PhocSeat          *phoc_seat_new (PhocInput *input, const char *name);
PhocSeat          *phoc_seat_from_wlr_seat (struct wlr_seat *wlr_seat);

void               phoc_seat_add_device (PhocSeat                *seat,
                                         struct wlr_input_device *device);

void               phoc_seat_configure_cursor (PhocSeat *seat);
PhocCursor        *phoc_seat_get_cursor (PhocSeat *self);

bool               phoc_seat_grab_meta_press (PhocSeat *seat);

PhocView          *phoc_seat_get_focus_view  (PhocSeat *seat);
void               phoc_seat_set_focus_view  (PhocSeat *seat, PhocView *view);
void               phoc_seat_set_focus_layer (PhocSeat                    *seat,
                                              struct wlr_layer_surface_v1 *layer);

void               phoc_seat_cycle_focus (PhocSeat *seat, gboolean forward);

void               phoc_seat_begin_move (PhocSeat *seat, PhocView *view);

void               phoc_seat_begin_resize (PhocSeat *seat, PhocView *view,
                                           uint32_t edges);

void               phoc_seat_end_compositor_grab (PhocSeat *seat);

PhocSeatView      *phoc_seat_view_from_view (PhocSeat *seat, PhocView *view);

void               phoc_seat_set_exclusive_client (PhocSeat         *seat,
                                                   struct wl_client *client);

bool               phoc_seat_allow_input (PhocSeat           *seat,
                                          struct wl_resource *resource);

void               phoc_seat_maybe_set_cursor (PhocSeat *self, const char *name);

gboolean           phoc_seat_has_touch    (PhocSeat *self);
gboolean           phoc_seat_has_pointer  (PhocSeat *self);
gboolean           phoc_seat_has_keyboard (PhocSeat *self);
gboolean           phoc_seat_has_hw_keyboard (PhocSeat *self);
gboolean           phoc_seat_has_switch   (PhocSeat *self, enum wlr_switch_type type);

void               phoc_seat_update_last_touch_serial (PhocSeat *self, uint32_t serial);
void               phoc_seat_update_last_button_serial (PhocSeat *self, uint32_t serial);
uint32_t           phoc_seat_get_last_button_or_touch_serial (PhocSeat *self);
