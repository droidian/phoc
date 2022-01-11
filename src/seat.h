/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <wayland-server-core.h>
#include "input.h"
#include "layers.h"
#include "switch.h"
#include "text_input.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_SEAT (phoc_seat_get_type ())

G_DECLARE_FINAL_TYPE (PhocSeat, phoc_seat, PHOC, SEAT, GObject)

typedef struct _PhocCursor PhocCursor;
typedef struct _PhocDragIcon PhocDragIcon;
typedef struct _PhocTablet PhocTablet;

/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
typedef struct _PhocSeat {
  GObject                         parent;

  PhocInput                      *input;
  char                           *name;

  struct wlr_seat                *seat;
  PhocCursor                     *cursor;

  // coordinates of the first touch point if it exists
  int32_t                         touch_id;
  double                          touch_x, touch_y;

  // If the focused layer is set, views cannot receive keyboard focus
  struct wlr_layer_surface_v1    *focused_layer;

  struct roots_input_method_relay im_relay;

  // If non-null, only this client can receive input events
  struct wl_client               *exclusive_client;

  struct wl_list                  views; // PhocSeatView::link
  bool                            has_focus;

  PhocDragIcon                   *drag_icon; // can be NULL

  GSList                         *keyboards;
  GSList                         *pointers;
  struct wl_list                  switches;
  GSList                         *touch;
  GSList                         *tablets;
  struct wl_list                  tablet_pads;

  struct wl_listener              request_set_selection;
  struct wl_listener              request_set_primary_selection;
  struct wl_listener              request_start_drag;
  struct wl_listener              start_drag;
  struct wl_listener              destroy;

  GHashTable                     *input_mapping_settings;
} PhocSeat;

typedef struct _PhocSeatView {
  PhocSeat          *seat;
  struct roots_view *view;

  bool               has_button_grab;
  double             grab_sx;
  double             grab_sy;

  struct wl_list     link;   // PhocSeat::views

  struct wl_listener view_unmap;
  struct wl_listener view_destroy;
} PhocSeatView;

struct _PhocDragIcon {
  PhocSeat             *seat;
  struct wlr_drag_icon *wlr_drag_icon;

  double                x, y;

  struct wl_listener    surface_commit;
  struct wl_listener    map;
  struct wl_listener    unmap;
  struct wl_listener    destroy;
};

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

void               phoc_seat_add_device (PhocSeat                *seat,
                                         struct wlr_input_device *device);

void               phoc_seat_configure_cursor (PhocSeat *seat);
PhocCursor        *phoc_seat_get_cursor (PhocSeat *self);

void               phoc_seat_configure_xcursor (PhocSeat *seat);

bool               phoc_seat_has_meta_pressed (PhocSeat *seat);

struct roots_view *phoc_seat_get_focus (PhocSeat *seat);

void               phoc_seat_set_focus (PhocSeat *seat, struct roots_view *view);

void               phoc_seat_set_focus_layer (PhocSeat                    *seat,
                                              struct wlr_layer_surface_v1 *layer);

void               phoc_seat_cycle_focus (PhocSeat *seat);

void               phoc_seat_begin_move (PhocSeat *seat, struct roots_view *view);

void               phoc_seat_begin_resize (PhocSeat *seat, struct roots_view *view,
                                           uint32_t edges);

void               phoc_seat_end_compositor_grab (PhocSeat *seat);

PhocSeatView      *phoc_seat_view_from_view (PhocSeat          *seat,
                                             struct roots_view *view);

void               phoc_drag_icon_update_position (PhocDragIcon *icon);

void               phoc_drag_icon_damage_whole (PhocDragIcon *icon);

void               phoc_seat_set_exclusive_client (PhocSeat         *seat,
                                                   struct wl_client *client);

bool               phoc_seat_allow_input (PhocSeat           *seat,
                                          struct wl_resource *resource);

void               phoc_seat_maybe_set_cursor (PhocSeat *self, const char *name);
