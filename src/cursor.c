/*
 * Copyright (C) 2021 Purism SPC
 *               2023-2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later or MIT
 */

#define G_LOG_DOMAIN "phoc-cursor"

#include "phoc-config.h"
#include "color-rect.h"
#include "server.h"
#include "timed-animation.h"
#include "gesture.h"
#include "gesture-drag.h"
#include "gesture-swipe.h"
#include "layer-shell-effects.h"

#define _XOPEN_SOURCE 700
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <linux/input-event-codes.h>
#include "cursor.h"
#include "desktop.h"
#include "input-method-relay.h"
#include "utils.h"
#include "view.h"

#define PHOC_ANIM_SUGGEST_STATE_CHANGE_COLOR    (PhocColor){0.0f, 0.6f, 1.0f, 0.5f}
#define PHOC_ANIM_DURATION_SUGGEST_STATE_CHANGE 200

enum {
  PROP_0,
  PROP_SEAT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PhocCursorPrivate {
  /* Would be good to store on the surface itself */
  PhocDraggableLayerSurface *drag_surface;
  GSList                    *gestures;

  /* The compositor tracked touch points */
  GHashTable                *touch_points;

  gboolean                   has_pointer_motion;

  /* State of the animated view when cursor touches a screen edge */
  struct {
    PhocColorRect         *rect;
    PhocView              *view;
    PhocViewState          state;
    PhocViewTileDirection  tile_dir;
    PhocOutput            *output;
    PhocTimedAnimation    *anim;
  } view_state;

  /* The cursor */
  PhocCursorMode              mode;
  struct wl_client           *image_client;
  struct wlr_surface         *image_surface;
  struct wl_listener          image_surface_destroy;
  const char                 *image_name;
  int32_t                     hotspot_x;
  int32_t                     hotspot_y;
  struct wlr_xcursor_manager *xcursor_manager;
  GSettings                  *interface_settings;
} PhocCursorPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (PhocCursor, phoc_cursor, G_TYPE_OBJECT)

#define PHOC_CURSOR_SELF(p) PHOC_PRIV_CONTAINER(PHOC_CURSOR, PhocCursor, (p))

static void handle_pointer_motion_relative (struct wl_listener *listener, void *data);
static void handle_pointer_motion_absolute (struct wl_listener *listener, void *data);
static void handle_pointer_button (struct wl_listener *listener, void *data);
static void handle_pointer_axis (struct wl_listener *listener, void *data);
static void handle_pointer_frame (struct wl_listener *listener, void *data);
static void handle_touch_frame (struct wl_listener *listener, void *data);

/* {{{ Cursor image */

static void
phoc_cursor_show (PhocCursor *self)
{
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  if (!phoc_seat_has_pointer (self->seat) || !priv->has_pointer_motion)
    return;

  if (priv->image_surface) {
    phoc_cursor_set_image (self,
                           priv->image_client,
                           priv->image_surface,
                           priv->hotspot_x,
                           priv->hotspot_y);
  } else {
    phoc_cursor_set_name (self, priv->image_client, priv->image_name);
  }
}


static void
phoc_cursor_set_image_surface (PhocCursor *self, struct wlr_surface *surface)
{
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  wl_list_remove (&priv->image_surface_destroy.link);
  priv->image_surface = surface;

  wl_list_init (&priv->image_surface_destroy.link);
  if (surface)
    wl_signal_add (&surface->events.destroy, &priv->image_surface_destroy);
}


static void
handle_image_surface_destroy (struct wl_listener *listener, void *data)
{
  PhocCursorPrivate *priv = wl_container_of (listener, priv, image_surface_destroy);
  PhocCursor *self = PHOC_CURSOR_SELF (priv);

  phoc_cursor_set_name (self, priv->image_client, priv->image_name);
}


static void
handle_request_set_cursor (struct wl_listener *listener,
                           void               *data)
{
  PhocCursor *self = wl_container_of (listener, self, request_set_cursor);
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_surface *focused_surface = event->seat_client->seat->pointer_state.focused_surface;
  bool has_focused = focused_surface != NULL && focused_surface->resource != NULL;
  struct wl_client *focused_client = NULL;
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  if (has_focused)
    focused_client = wl_resource_get_client (focused_surface->resource);

  if (event->seat_client->client != focused_client || priv->mode != PHOC_CURSOR_PASSTHROUGH) {
    g_debug ("Denying request to set cursor from unfocused client");
    return;
  }

  phoc_cursor_set_image (self, focused_client, event->surface, event->hotspot_x, event->hotspot_y);
}


static void
on_cursor_theme_changed (PhocCursor *self, const char *key, GSettings *settings)
{
  g_autofree char* theme = NULL;
  int size;

  g_assert (PHOC_IS_CURSOR (self));
  g_assert (G_IS_SETTINGS (settings));

  theme = g_settings_get_string (settings, "cursor-theme");
  size = g_settings_get_int (settings, "cursor-size");
  size = size > 0 ? size : PHOC_XCURSOR_SIZE;
  g_debug ("Setting cursor theme to %s, size: %d", theme, size);

  phoc_cursor_set_xcursor_theme (self, theme, size);
}

/* {{{ Animated view */

static void
phoc_cursor_view_state_set_view (PhocCursor *self, PhocView *view)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  if (priv->view_state.view == view)
    return;

  if (priv->view_state.view)
    g_object_remove_weak_pointer (G_OBJECT (priv->view_state.view),
                                  (gpointer *)&priv->view_state.view);

  priv->view_state.view = view;

  if (priv->view_state.view) {
    g_object_add_weak_pointer (G_OBJECT (priv->view_state.view),
                               (gpointer *)&priv->view_state.view);
  }
}


static void
phoc_cursor_view_state_set_output (PhocCursor *self, PhocOutput *output)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  if (priv->view_state.output == output)
    return;

  if (priv->view_state.output)
    g_object_remove_weak_pointer (G_OBJECT (priv->view_state.output),
                                  (gpointer *)&priv->view_state.output);

  priv->view_state.output = output;

  if (priv->view_state.output) {
    g_object_add_weak_pointer (G_OBJECT (priv->view_state.output),
                               (gpointer *)&priv->view_state.output);
  }
}


static void
phoc_cursor_suggest_view_state_change (PhocCursor            *self,
                                       PhocView              *view,
                                       PhocOutput            *output,
                                       PhocViewState          state,
                                       PhocViewTileDirection  dir)
{
  PhocCursorPrivate *priv;
  struct wlr_box view_box, suggested_box;
  g_autoptr (PhocPropertyEaser) easer = NULL;

  g_assert (PHOC_IS_CURSOR (self));
  g_assert (PHOC_IS_VIEW (view));
  g_assert (PHOC_IS_OUTPUT (output));
  priv = phoc_cursor_get_instance_private (self);

  if (priv->view_state.view)
    return;

  switch (state) {
  case PHOC_VIEW_STATE_TILED:
  case PHOC_VIEW_STATE_MAXIMIZED:
    priv->view_state.state = state;
    priv->view_state.tile_dir = dir;
    break;
  case PHOC_VIEW_STATE_FLOATING:
  default:
    g_assert_not_reached ();
  }

  phoc_cursor_view_state_set_view (self, view);
  phoc_cursor_view_state_set_output (self, output);
  phoc_view_get_box (view, &view_box);
  priv->view_state.rect = phoc_color_rect_new ((PhocBox *)&view_box,
                                               &PHOC_ANIM_SUGGEST_STATE_CHANGE_COLOR);
  phoc_view_add_bling (view, PHOC_BLING (priv->view_state.rect));

  switch (state) {
  case PHOC_VIEW_STATE_MAXIMIZED:
    if (!phoc_view_get_maximized_box (view, output, &suggested_box)) {
      g_warning ("Failed to get target box for %d on %s", state, phoc_output_get_name (output));
      return;
    }
    break;
  case PHOC_VIEW_STATE_TILED:
    if (!phoc_view_get_tiled_box (view, dir, output, &suggested_box)) {
      g_warning ("Failed to get target box for %d on %s", state, phoc_output_get_name (output));
      return;
    }
    break;
  default:
    g_assert_not_reached ();
  }

  g_debug ("Suggest %s: %d,%d %dx%d for %p on %s",
           state == PHOC_VIEW_STATE_MAXIMIZED ? "maximize" : "tile",
           suggested_box.x, suggested_box.y, suggested_box.width, suggested_box.height,
           view, phoc_output_get_name (output));
  easer = g_object_new (PHOC_TYPE_PROPERTY_EASER,
                        "target", priv->view_state.rect,
                        "easing", PHOC_EASING_EASE_OUT_QUAD,
                        NULL);

  phoc_property_easer_set_props (easer,
                                 "x", view_box.x, suggested_box.x,
                                 "y", view_box.y, suggested_box.y,
                                 "width", view_box.width, suggested_box.width,
                                 "height", view_box.height, suggested_box.height,
                                 NULL);

  priv->view_state.anim = g_object_new (PHOC_TYPE_TIMED_ANIMATION,
                                        "duration", PHOC_ANIM_DURATION_SUGGEST_STATE_CHANGE,
                                        "property-easer", easer,
                                        "animatable", output,
                                        "dispose-on-done", FALSE,
                                        NULL);
  phoc_bling_map (PHOC_BLING (priv->view_state.rect));
  phoc_timed_animation_play (priv->view_state.anim);
}


static void
phoc_cursor_clear_view_state_change (PhocCursor *self)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  if (priv->view_state.view) {
    phoc_view_remove_bling (priv->view_state.view, PHOC_BLING (priv->view_state.rect));
    g_clear_object (&priv->view_state.rect);
    g_clear_object (&priv->view_state.anim);
  }

  phoc_cursor_view_state_set_view (self, NULL);
  phoc_cursor_view_state_set_output (self, NULL);
}


static void
phoc_cursor_submit_pending_view_state_change (PhocCursor *self)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  g_assert (PHOC_IS_VIEW (priv->view_state.view));

  switch (priv->view_state.state) {
  case PHOC_VIEW_STATE_MAXIMIZED:
    phoc_view_maximize (priv->view_state.view, priv->view_state.output);
    break;
  case PHOC_VIEW_STATE_TILED:
    phoc_view_tile (priv->view_state.view, priv->view_state.tile_dir, priv->view_state.output);
    break;
  case PHOC_VIEW_STATE_FLOATING:
    /* Nothing to do */
    break;
  default:
    g_return_if_reached();
  }

  /* Dispose animation and color-rect */
  phoc_cursor_clear_view_state_change (self);
}

/* }}} */

static bool
should_ignore_touch_grab (PhocSeat           *seat,
                          struct wlr_surface *surface)
{
  // ignore seat grab when interacting with layer-surface

  if (!surface)
    return false;

  struct wlr_surface *root = wlr_surface_get_root_surface (surface);
  struct wlr_layer_surface_v1 *layer_surface = wlr_layer_surface_v1_try_from_wlr_surface (root);

  // FIXME: return false if the grab comes from a xdg-popup that belongs to a layer-surface
  return layer_surface && wlr_seat_touch_has_grab (seat->seat);
}


static bool
should_ignore_pointer_grab (PhocSeat           *seat,
                            struct wlr_surface *surface)
{
  // ignore seat grab when interacting with layer-surface

  if (!surface)
    return false;

  struct wlr_surface *root = wlr_surface_get_root_surface (surface);
  struct wlr_layer_surface_v1 *layer_surface = wlr_layer_surface_v1_try_from_wlr_surface (root);

  // FIXME: return false if the grab comes from a xdg-popup that belongs to a layer-surface
  return layer_surface && wlr_seat_pointer_has_grab (seat->seat);
}


static void
send_pointer_enter (PhocSeat           *seat,
                    struct wlr_surface *surface,
                    double              sx,
                    double              sy)
{
  if (should_ignore_pointer_grab (seat, surface)) {
    wlr_seat_pointer_enter (seat->seat, surface, sx, sy);
    return;
  }

  wlr_seat_pointer_notify_enter (seat->seat, surface, sx, sy);
}


static void
send_pointer_clear_focus (PhocSeat           *seat,
                          struct wlr_surface *surface)
{
  if (should_ignore_pointer_grab (seat, surface)) {
    wlr_seat_pointer_clear_focus (seat->seat);
    return;
  }

  wlr_seat_pointer_notify_clear_focus (seat->seat);
}


static void
send_pointer_motion (PhocSeat           *seat,
                     struct wlr_surface *surface,
                     uint32_t            time,
                     double              sx,
                     double              sy)
{
  if (should_ignore_pointer_grab (seat, surface)) {
    wlr_seat_pointer_send_motion (seat->seat, time, sx, sy);
    return;
  }

  wlr_seat_pointer_notify_motion (seat->seat, time, sx, sy);
}

static void
send_pointer_button (PhocSeat             *seat,
                     struct wlr_surface   *surface,
                     uint32_t              time,
                     uint32_t              button,
                     enum wlr_button_state state)
{
  uint32_t serial;

  if (should_ignore_pointer_grab (seat, surface)) {
    serial = wlr_seat_pointer_send_button (seat->seat, time, button, state);
    phoc_seat_update_last_button_serial (seat, serial);
    return;
  }

  serial = wlr_seat_pointer_notify_button (seat->seat, time, button, state);
  phoc_seat_update_last_button_serial (seat, serial);
}


static void
send_pointer_axis (PhocSeat                 *seat,
                   struct wlr_surface       *surface,
                   uint32_t                  time,
                   enum wlr_axis_orientation orientation,
                   double                    value,
                   int32_t                   value_discrete,
                   enum wlr_axis_source      source)
{
  if (should_ignore_pointer_grab (seat, surface)) {
    wlr_seat_pointer_send_axis (seat->seat, time, orientation, value, value_discrete, source);
    return;
  }

  wlr_seat_pointer_notify_axis (seat->seat, time, orientation, value, value_discrete, source);
}


static void
send_touch_down (PhocSeat                    *seat,
                 struct wlr_surface          *surface,
                 struct wlr_touch_down_event *event,
                 double                       sx,
                 double                       sy)
{
  uint32_t serial;

  if (should_ignore_touch_grab (seat, surface)) {
    // currently wlr_seat_touch_send_* functions don't work, so temporarily
    // restore grab to the default one and use notify_* instead
    // See https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3478
    struct wlr_seat_touch_grab *grab = seat->seat->touch_state.grab;
    seat->seat->touch_state.grab = seat->seat->touch_state.default_grab;
    serial = wlr_seat_touch_notify_down (seat->seat, surface, event->time_msec,
                                         event->touch_id, sx, sy);
    phoc_seat_update_last_touch_serial (seat, serial);
    seat->seat->touch_state.grab = grab;
    return;
  }

  serial =  wlr_seat_touch_notify_down (seat->seat, surface, event->time_msec,
                                        event->touch_id, sx, sy);
  phoc_seat_update_last_touch_serial (seat, serial);
}


static void
send_touch_motion (PhocSeat                      *seat,
                   struct wlr_surface            *surface,
                   struct wlr_touch_motion_event *event,
                   double                         sx,
                   double                         sy)
{
  if (should_ignore_touch_grab (seat, surface)) {
    // currently wlr_seat_touch_send_* functions don't work, so temporarily
    // restore grab to the default one and use notify_* instead
    // See https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3478
    struct wlr_seat_touch_grab *grab = seat->seat->touch_state.grab;
    seat->seat->touch_state.grab = seat->seat->touch_state.default_grab;
    wlr_seat_touch_notify_motion (seat->seat, event->time_msec,
                                  event->touch_id, sx, sy);
    seat->seat->touch_state.grab = grab;
    return;
  }

  wlr_seat_touch_notify_motion (seat->seat, event->time_msec,
                                event->touch_id, sx, sy);
}


static void
send_touch_up (PhocSeat                  *seat,
               struct wlr_surface        *surface,
               struct wlr_touch_up_event *event)
{
  if (should_ignore_touch_grab (seat, surface)) {
    // currently wlr_seat_touch_send_* functions don't work, so temporarily
    // restore grab to the default one and use notify_* instead
    // See https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3478
    struct wlr_seat_touch_grab *grab = seat->seat->touch_state.grab;
    seat->seat->touch_state.grab = seat->seat->touch_state.default_grab;
    wlr_seat_touch_notify_up (seat->seat, event->time_msec, event->touch_id);
    seat->seat->touch_state.grab = grab;
    return;
  }

  wlr_seat_touch_notify_up (seat->seat, event->time_msec, event->touch_id);
}


static void
send_touch_cancel (PhocSeat                  *seat,
                   struct wlr_surface        *surface)
{
  if (should_ignore_touch_grab (seat, surface)) {
    // currently, wlr_seat_touch_send_* functions don't work, so temporarily
    // restore grab to the default one and use notify_* instead
    // See https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3478
    struct wlr_seat_touch_grab *grab = seat->seat->touch_state.grab;
    seat->seat->touch_state.grab = seat->seat->touch_state.default_grab;
    wlr_seat_touch_notify_cancel (seat->seat, surface);
    seat->seat->touch_state.grab = grab;
    return;
  }

  wlr_seat_touch_notify_cancel (seat->seat, surface);
}


static PhocTouchPoint *
phoc_cursor_add_touch_point (PhocCursor *self, struct wlr_touch_down_event *event)
{
  PhocTouchPoint *touch_point = g_new0 (PhocTouchPoint, 1);
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);
  double lx, ly;

  wlr_cursor_absolute_to_layout_coords (self->cursor, &event->touch->base,
                                        event->x, event->y, &lx, &ly);
  touch_point->touch_id = event->touch_id;
  touch_point->lx = lx;
  touch_point->ly = ly;

  if (!g_hash_table_insert (priv->touch_points,
                            GINT_TO_POINTER (event->touch_id),
                            touch_point)) {
    g_critical ("Touch point %d already tracked, ignoring", event->touch_id);
  }

  return touch_point;
}


static PhocTouchPoint *
phoc_cursor_update_touch_point (PhocCursor *self, struct wlr_touch_motion_event *event)
{
  PhocTouchPoint *touch_point;
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);
  double lx, ly;

  touch_point = g_hash_table_lookup (priv->touch_points, GINT_TO_POINTER (event->touch_id));
  if (touch_point == NULL) {
    g_critical ("Touch point %d does not exist", event->touch_id);
    return NULL;
  }
  wlr_cursor_absolute_to_layout_coords (self->cursor, &event->touch->base,
                                        event->x, event->y, &lx, &ly);
  touch_point->lx = lx;
  touch_point->ly = ly;

  return touch_point;
}


static void
phoc_cursor_remove_touch_point (PhocCursor *self, int touch_id)
{
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  if (!g_hash_table_remove (priv->touch_points, GINT_TO_POINTER (touch_id)))
    g_critical ("Touch point %d didn't exist", touch_id);
}


static PhocTouchPoint *
phoc_cursor_get_touch_point (PhocCursor *self, int touch_id)
{
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  return g_hash_table_lookup (priv->touch_points, GINT_TO_POINTER (touch_id));
}


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

/**
 * handle_gestures_for_event_at:
 *
 * Feed an event that has layout coordinates into the gesture system.
 */
static void
handle_gestures_for_event_at (PhocCursor   *self,
                              double        lx,
                              double        ly,
                              PhocEventType type,
                              gpointer      wlr_event,
                              gsize         size)
{
  g_autoptr (PhocEvent) event = phoc_event_new (type, wlr_event, size);
  GSList *gestures = phoc_cursor_get_gestures (self);

  if (gestures == NULL)
    return;

  for (GSList *elem = gestures; elem; elem = elem->next) {
    PhocGesture *gesture = PHOC_GESTURE (elem->data);

    g_assert (PHOC_IS_GESTURE (gesture));
    phoc_gesture_handle_event (gesture, event, lx, ly);
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

  PhocViewDecoPart parts = phoc_view_get_deco_part (view->view, sx, sy);

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
      phoc_cursor_set_name (self, NULL, PHOC_XCURSOR_DEFAULT);
    } else if (edges) {
      const char *resize_name = wlr_xcursor_get_resize_name (edges);
      phoc_cursor_set_name (self, NULL, resize_name);
    }
  }
}

static void
seat_view_deco_leave (PhocSeatView *view)
{
  PhocCursor *self = phoc_seat_get_cursor (view->seat);

  phoc_cursor_set_name (self, NULL, PHOC_XCURSOR_DEFAULT);
  view->has_button_grab = false;
}

static void
seat_view_deco_button (PhocSeatView *view, double sx,
                       double sy, uint32_t button, uint32_t state)
{
  PhocCursor *self = phoc_seat_get_cursor (view->seat);

  if (button == BTN_LEFT && state == WLR_BUTTON_PRESSED) {
    view->has_button_grab = true;
    view->grab_sx = sx;
    view->grab_sy = sy;
  } else {
    view->has_button_grab = false;
  }

  PhocViewDecoPart parts = phoc_view_get_deco_part (view->view, sx, sy);

  if (state == WLR_BUTTON_RELEASED && (parts & PHOC_VIEW_DECO_PART_TITLEBAR))
    phoc_cursor_set_name (self, NULL, PHOC_XCURSOR_DEFAULT);
}

static bool
phoc_handle_shell_reveal (struct wlr_surface *surface, double lx, double ly, int threshold)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());

  if (surface) {
    struct wlr_xdg_surface *wlr_xdg_surface;
    struct wlr_surface *iter = wlr_surface_get_root_surface (surface);

    wlr_xdg_surface = wlr_xdg_surface_try_from_wlr_surface (iter);
    while (wlr_xdg_surface) {

      if (wlr_xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        iter = wlr_xdg_surface->popup->parent;
        wlr_xdg_surface = wlr_xdg_surface_try_from_wlr_surface (iter);
      } else {
        break;
      }
    }

    if (wlr_layer_surface_v1_try_from_wlr_surface (iter))
      return false;
  }

  PhocOutput *output = phoc_desktop_layout_get_output (desktop, lx, ly);
  if (!output)
    return false;

  struct wlr_box output_box;
  wlr_output_layout_get_box (desktop->layout, output->wlr_output, &output_box);

  PhocLayerSurface *layer_surface;
  bool left = false, right = false, top = false, bottom = false;

  wl_list_for_each (layer_surface, &output->layer_surfaces, link) {
    if (layer_surface->layer != ZWLR_LAYER_SHELL_V1_LAYER_TOP)
      continue;

    struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->layer_surface;
    struct wlr_layer_surface_v1_state *state = &wlr_layer_surface->current;
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

  if ((top    && ly <= output_box.y + threshold) ||
      (bottom && ly >= output_box.y + output_box.height - 1 - threshold) ||
      (left   && lx <= output_box.x + threshold) ||
      (right  && lx >= output_box.x + output_box.width - 1 - threshold)) {
    if (output->fullscreen_view) {
      phoc_output_force_shell_reveal (output, true);
    }
    return true;
  }

  phoc_output_force_shell_reveal (output, false);
  return false;
}


static void
phoc_passthrough_cursor (PhocCursor *self, uint32_t time)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  double sx, sy;
  PhocView *view = NULL;
  PhocSeat *seat = self->seat;
  struct wl_client *client = NULL;
  struct wlr_surface *surface;
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  surface = phoc_desktop_wlr_surface_at (desktop, self->cursor->x, self->cursor->y, &sx, &sy, &view);
  if (surface)
    client = wl_resource_get_client (surface->resource);

  if (surface && !phoc_seat_allow_input (seat, surface->resource))
    return;

  if (priv->image_client != client || !client) {
    phoc_cursor_set_name (self, NULL, PHOC_XCURSOR_DEFAULT);
    priv->image_client = client;
  }

  if (view) {
    PhocSeatView *seat_view = phoc_seat_view_from_view (seat, view);

    if (self->pointer_view && !self->wlr_surface && (surface || seat_view != self->pointer_view))
      seat_view_deco_leave (self->pointer_view);

    self->pointer_view = seat_view;

    if (!surface)
      seat_view_deco_motion (seat_view, sx, sy);

  } else {
    self->pointer_view = NULL;
  }

  self->wlr_surface = surface;

  if (surface) {
    send_pointer_enter (seat, surface, sx, sy);
    send_pointer_motion (seat, surface, time, sx,sy);
  } else {
    send_pointer_clear_focus (seat, seat->seat->pointer_state.focused_surface);
  }

  if (seat->drag_icon != NULL)
    phoc_drag_icon_update_position (seat->drag_icon);
}


static inline int64_t
timespec_to_msec (const struct timespec *a)
{
  return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}


static void
phoc_cursor_constructed (GObject *object)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursor *self = PHOC_CURSOR (object);
  struct wlr_cursor *wlr_cursor = self->cursor;
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  G_OBJECT_CLASS (phoc_cursor_parent_class)->constructed (object);

  g_assert (self->cursor);

  wl_signal_add (&wlr_cursor->events.motion, &self->motion);
  self->motion.notify = handle_pointer_motion_relative;

  wl_signal_add (&wlr_cursor->events.motion_absolute, &self->motion_absolute);
  self->motion_absolute.notify = handle_pointer_motion_absolute;

  wl_signal_add (&wlr_cursor->events.button, &self->button);
  self->button.notify = handle_pointer_button;

  wl_signal_add (&wlr_cursor->events.axis, &self->axis);
  self->axis.notify = handle_pointer_axis;

  wl_signal_add (&wlr_cursor->events.frame, &self->frame);
  self->frame.notify = handle_pointer_frame;

  wl_signal_add (&wlr_cursor->events.touch_frame, &self->touch_frame);
  self->touch_frame.notify = handle_touch_frame;

  g_assert (PHOC_IS_SEAT (self->seat));
  wl_signal_add (&self->seat->seat->events.request_set_cursor, &self->request_set_cursor);
  self->request_set_cursor.notify = handle_request_set_cursor;

  wl_list_init (&priv->image_surface_destroy.link);
  priv->image_surface_destroy.notify = handle_image_surface_destroy;

  wlr_cursor_attach_output_layout (wlr_cursor, desktop->layout);

  priv->interface_settings = g_settings_new ("org.gnome.desktop.interface");
  g_signal_connect_swapped (priv->interface_settings, "changed::cursor-size",
                              G_CALLBACK (on_cursor_theme_changed), self);
  g_signal_connect_swapped (priv->interface_settings, "changed::cursor-theme",
                              G_CALLBACK (on_cursor_theme_changed), self);
  on_cursor_theme_changed (self, NULL, priv->interface_settings);
}


static void
free_gestures (GSList *gestures)
{
  g_slist_free_full (gestures, g_object_unref);
}


static void
phoc_cursor_finalize (GObject *object)
{
  PhocCursor *self = PHOC_CURSOR (object);
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  phoc_cursor_clear_view_state_change (self);
  g_clear_pointer (&priv->touch_points, g_hash_table_destroy);
  g_clear_pointer (&priv->gestures, free_gestures);

  g_clear_object (&priv->interface_settings);
  phoc_cursor_set_image_surface (self, NULL);
  wl_list_remove (&self->request_set_cursor.link);

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
  wl_list_remove (&self->touch_frame.link);
  wl_list_remove (&self->tool_axis.link);
  wl_list_remove (&self->tool_tip.link);
  wl_list_remove (&self->tool_proximity.link);
  wl_list_remove (&self->tool_button.link);
  wl_list_remove (&self->focus_change.link);

  g_clear_pointer (&priv->xcursor_manager, wlr_xcursor_manager_destroy);
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
on_drag_begin (PhocGesture *gesture, double lx, double ly, PhocCursor *self)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocLayerSurface *layer_surface;
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);
  PhocDraggableLayerSurface *drag_surface;

  g_assert (PHOC_IS_GESTURE (gesture));
  g_assert (PHOC_IS_CURSOR (self));

  priv->drag_surface = NULL;
  layer_surface = phoc_desktop_layer_surface_at (desktop, lx, ly, NULL, NULL);
  if (!layer_surface)
    return;

  drag_surface = phoc_desktop_get_draggable_layer_surface (desktop, layer_surface);
  if (!drag_surface)
    return;

  if (phoc_draggable_layer_surface_drag_start (drag_surface, lx, ly) ==
      PHOC_DRAGGABLE_SURFACE_STATE_REJECTED) {
    priv->drag_surface = NULL;
  } else {
    priv->drag_surface = drag_surface;
  }
}


static void
on_drag_update (PhocGesture *gesture, double off_x, double off_y, PhocCursor *self)
{
  PhocCursorPrivate *priv;
  PhocDraggableSurfaceState state;

  g_assert (PHOC_IS_GESTURE (gesture));
  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  if (!priv->drag_surface)
    return;

  state = phoc_draggable_layer_surface_drag_update (priv->drag_surface, off_x, off_y);
  switch (state) {
  case PHOC_DRAGGABLE_SURFACE_STATE_DRAGGING:
    if (phoc_seat_has_touch (self->seat)) {
      PhocLayerSurface *layer_surface =
        phoc_draggable_layer_surface_get_layer_surface (priv->drag_surface);
      GList *seqs = phoc_gesture_get_sequences (gesture);
      g_assert (g_list_length (seqs) == 1);
      int touch_id = GPOINTER_TO_INT (seqs->data);
      struct wlr_touch_point *point = wlr_seat_touch_get_point (self->seat->seat, touch_id);
      if (!point)
        break;

      g_debug ("Cancelling drag gesture for %s",
               phoc_layer_surface_get_namespace (layer_surface));
      send_touch_cancel (self->seat, layer_surface->layer_surface->surface);
    }
    break;
  case PHOC_DRAGGABLE_SURFACE_STATE_REJECTED:
    phoc_gesture_reset (gesture);
    phoc_draggable_layer_surface_drag_end (priv->drag_surface, off_x, off_y);
    break;
  default:
    /* nothing todo */
    break;
  }
}


static void
on_drag_end (PhocGesture *gesture, double off_x, double off_y, PhocCursor *self)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_GESTURE (gesture));
  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  if (!priv->drag_surface)
    return;

  phoc_draggable_layer_surface_drag_end (priv->drag_surface, off_x, off_y);
}


static void
on_drag_cancel (PhocGesture *gesture, gpointer sequence, PhocCursor *self)
{
  g_assert (PHOC_IS_GESTURE (gesture));
  g_assert (PHOC_IS_CURSOR (self));

  /* Nothing to do here yet */
  g_debug ("%s", __func__);
}


static void
on_swipe (PhocGestureSwipe *swipe_gesture, double vx, double vy, gpointer data)
{
  PhocCursor *self = PHOC_CURSOR (data);
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocLayerSurface *layer_surface;
  PhocDraggableLayerSurface *drag_surface;
  PhocEventSequence *sequence;
  double lx, ly;

  g_assert (PHOC_IS_GESTURE_SWIPE (swipe_gesture));

  sequence = phoc_gesture_get_last_updated_sequence (PHOC_GESTURE (swipe_gesture));
  if (!phoc_gesture_get_point (PHOC_GESTURE (swipe_gesture), sequence, &lx, &ly)) {
    g_warning ("Failed to get event point for %p", sequence);
    return;
  }

  layer_surface = phoc_desktop_layer_surface_at (desktop, lx, ly, NULL, NULL);
  if (!layer_surface)
    return;

  drag_surface = phoc_desktop_get_draggable_layer_surface (desktop, layer_surface);
  if (!drag_surface)
    return;

  if (!phoc_draggable_layer_surface_fling (drag_surface, lx, ly, vx, vy))
    return;

  send_touch_cancel (self->seat, layer_surface->layer_surface->surface);
}


static void
phoc_cursor_init (PhocCursor *self)
{
  g_autoptr (PhocGestureDrag) drag_gesture = NULL;
  g_autoptr (PhocGestureSwipe) swipe_gesture = NULL;
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  self->cursor = wlr_cursor_create ();

  priv->touch_points = g_hash_table_new_full (g_direct_hash,
                                              g_direct_equal,
                                              NULL,
                                              g_free);
  /*
   * Drag gesture starting at the current cursor position
   */
  drag_gesture = phoc_gesture_drag_new ();
  g_object_connect (drag_gesture,
                    "signal::drag-begin", on_drag_begin, self,
                    "signal::drag-update", on_drag_update, self,
                    "signal::drag-end", on_drag_end, self,
                    "signal::cancel", on_drag_cancel, self,
                    NULL);
  phoc_cursor_add_gesture (self, PHOC_GESTURE (drag_gesture));

  swipe_gesture = phoc_gesture_swipe_new ();
  g_signal_connect (swipe_gesture, "swipe", G_CALLBACK (on_swipe), self);
  phoc_cursor_add_gesture (self, PHOC_GESTURE (swipe_gesture));
}


void
phoc_cursor_update_focus (PhocCursor *self)
{
  struct timespec now;

  clock_gettime (CLOCK_MONOTONIC, &now);

  phoc_passthrough_cursor (self, timespec_to_msec (&now));
}

void
phoc_cursor_update_position (PhocCursor *self, uint32_t time)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);
  PhocSeat *seat = self->seat;
  PhocView *view;

  switch (priv->mode) {
  case PHOC_CURSOR_PASSTHROUGH:
    phoc_passthrough_cursor (self, time);
    break;
  case PHOC_CURSOR_MOVE:
    view = phoc_seat_get_focus_view (seat);
    if (view != NULL) {
      struct wlr_box geom;
      phoc_view_get_geometry (view, &geom);
      double dx = self->cursor->x - self->offs_x;
      double dy = self->cursor->y - self->offs_y;
      PhocOutput *output = phoc_desktop_layout_get_output (desktop, self->cursor->x, self->cursor->y);
      struct wlr_box output_box;
      wlr_output_layout_get_box (desktop->layout, output->wlr_output, &output_box);

      bool output_is_landscape = output_box.width > output_box.height;

      if (phoc_view_is_fullscreen (view)) {
        phoc_view_set_fullscreen (view, true, output);
      } else if (self->cursor->y < output_box.y + PHOC_EDGE_SNAP_THRESHOLD) {
        phoc_cursor_suggest_view_state_change (self, view, output, PHOC_VIEW_STATE_MAXIMIZED, -1);
      } else if (output_is_landscape &&
                 self->cursor->x < output_box.x + PHOC_EDGE_SNAP_THRESHOLD) {
        phoc_cursor_suggest_view_state_change (self,
                                               view,
                                               output,
                                               PHOC_VIEW_STATE_TILED,
                                               PHOC_VIEW_TILE_LEFT);
      } else if (output_is_landscape &&
                 self->cursor->x > output_box.x + output_box.width - PHOC_EDGE_SNAP_THRESHOLD) {
        phoc_cursor_suggest_view_state_change (self,
                                               view,
                                               output,
                                               PHOC_VIEW_STATE_TILED,
                                               PHOC_VIEW_TILE_RIGHT);
      } else {
        phoc_cursor_clear_view_state_change (self);
        phoc_view_restore (view);
        phoc_view_move (view, self->view_x + dx - geom.x * phoc_view_get_scale (view),
                        self->view_y + dy - geom.y * phoc_view_get_scale (view));
      }
    }
    break;
  case PHOC_CURSOR_RESIZE:
    view = phoc_seat_get_focus_view (seat);
    if (view != NULL) {
      struct wlr_box geom;
      phoc_view_get_geometry (view, &geom);
      double dx = self->cursor->x - self->offs_x;
      double dy = self->cursor->y - self->offs_y;
      double x = view->box.x;
      double y = view->box.y;
      int width = self->view_width;
      int height = self->view_height;
      if (self->resize_edges & WLR_EDGE_TOP) {
        y = self->view_y + dy - geom.y * phoc_view_get_scale (view);
        height -= dy;
        if (height < 1) {
          y += height;
        }
      } else if (self->resize_edges & WLR_EDGE_BOTTOM) {
        height += dy;
      }
      if (self->resize_edges & WLR_EDGE_LEFT) {
        x = self->view_x + dx - geom.x * phoc_view_get_scale (view);
        width -= dx;
        if (width < 1) {
          x += width;
        }
      } else if (self->resize_edges & WLR_EDGE_RIGHT) {
        width += dx;
      }
      phoc_view_move_resize (view, x, y, MAX (1, width), MAX (1, height));
    }
    break;
  default:
    g_error ("Invalid cursor mode %d", priv->mode);
  }
}


static void
phoc_cursor_press_button (PhocCursor              *self,
                          struct wlr_input_device *device,
                          uint32_t                 time,
                          uint32_t                 button,
                          uint32_t                 state,
                          double                   lx,
                          double                   ly)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);
  PhocSeat *seat = self->seat;
  bool is_touch = device->type == WLR_INPUT_DEVICE_TOUCH;
  double sx, sy;
  PhocView *view;
  struct wlr_surface *surface;

  surface = phoc_desktop_wlr_surface_at (desktop, lx, ly, &sx, &sy, &view);
  if (state == WLR_BUTTON_PRESSED && view && phoc_seat_grab_meta_press (seat)) {
    phoc_seat_set_focus_view (seat, view);

    switch (button) {
    case BTN_LEFT:
      phoc_seat_begin_move (seat, view);
      break;
    case BTN_RIGHT: {
      uint32_t edges = 0;

      if (sx < view->wlr_surface->current.width/2)
        edges |= WLR_EDGE_LEFT;
      else
        edges |= WLR_EDGE_RIGHT;

      if (sy < view->wlr_surface->current.height/2)
        edges |= WLR_EDGE_TOP;
      else
        edges |= WLR_EDGE_BOTTOM;

      phoc_seat_begin_resize (seat, view, edges);
      break;
    }
    default:
      /* don't care */
      break;
    }
  } else {
    /* Mouse press inside server side window decoration */
    if (view && !surface && self->pointer_view)
      seat_view_deco_button (self->pointer_view, sx, sy, button, state);

    if (state == WLR_BUTTON_RELEASED && priv->mode != PHOC_CURSOR_PASSTHROUGH) {
      if (priv->view_state.view)
        phoc_cursor_submit_pending_view_state_change (self);
      priv->mode = PHOC_CURSOR_PASSTHROUGH;
      phoc_cursor_update_focus (self);
    }

    if (state == WLR_BUTTON_PRESSED) {
      if (view)
        phoc_seat_set_focus_view (seat, view);

      if (surface) {
        struct wlr_layer_surface_v1 *layer = wlr_layer_surface_v1_try_from_wlr_surface (surface);
        if (layer && layer->current.keyboard_interactive)
          phoc_seat_set_focus_layer (seat, layer);
      }
    }
  }

  if (!phoc_handle_shell_reveal (surface, lx, ly, PHOC_SHELL_REVEAL_POINTER_THRESHOLD) && !is_touch)
    send_pointer_button (seat, surface, time, button, state);

  if (surface)
    phoc_input_method_relay_im_submit (&seat->im_relay, surface);
}


static void
phoc_cursor_pointer_motion (PhocCursor              *self,
                            struct wlr_input_device *device,
                            double                   dx,
                            double                   dy,
                            double                   dx_unaccel,
                            double                   dy_unaccel,
                            guint32                  time_msec)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  if (!priv->has_pointer_motion) {
    priv->has_pointer_motion = TRUE;
    phoc_cursor_show (self);
  }
  phoc_desktop_notify_activity (desktop, self->seat);

  wlr_relative_pointer_manager_v1_send_relative_motion (desktop->relative_pointer_manager,
                                                        self->seat->seat,
                                                        (uint64_t)time_msec * 1000,
                                                        dx, dy,
                                                        dx_unaccel, dy_unaccel);

  if (self->active_constraint && device->type == WLR_INPUT_DEVICE_POINTER) {
    struct wlr_surface *wlr_surface;
    double sx, sy, sx_out, sy_out;

    wlr_surface = phoc_desktop_wlr_surface_at (desktop,
                                               self->cursor->x, self->cursor->y,
                                               &sx, &sy,
                                               NULL);

    if (self->active_constraint->surface != wlr_surface)
      return;

    if (!wlr_region_confine (&self->confine, sx, sy, sx + dx, sy + dy, &sx_out, &sy_out))
      return;

    dx = sx_out - sx;
    dy = sy_out - sy;
  }

  wlr_cursor_move (self->cursor, device, dx, dy);
  phoc_cursor_update_position (self, time_msec);
}


static void
handle_pointer_motion_relative (struct wl_listener *listener, void *data)
{
  PhocCursor *self = wl_container_of (listener, self, motion);
  struct wlr_pointer_motion_event *event = data;
  double dx = event->delta_x;
  double dy = event->delta_y;

  phoc_cursor_pointer_motion (self,
                              &event->pointer->base,
                              dx,
                              dy,
                              event->unaccel_dx,
                              event->unaccel_dy,
                              event->time_msec);
}


static void
handle_pointer_motion_absolute (struct wl_listener *listener, void *data)
{
  PhocCursor *self = wl_container_of (listener, self, motion_absolute);
  struct wlr_pointer_motion_absolute_event *event = data;
  double dx, dy, lx, ly;

  wlr_cursor_absolute_to_layout_coords (self->cursor,
                                        &event->pointer->base,
                                        event->x, event->y,
                                        &lx, &ly);

  handle_gestures_for_event_at (self, lx, ly, PHOC_EVENT_MOTION_NOTIFY, event, sizeof (*event));

  dx = lx - self->cursor->x;
  dy = ly - self->cursor->y;

  phoc_cursor_pointer_motion (self, &event->pointer->base, dx, dy, dx, dy, event->time_msec);
}


static void
handle_pointer_button (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursor *self = wl_container_of (listener, self, button);
  struct wlr_pointer_button_event *event = data;
  PhocEventType type;
  bool is_touch = event->pointer->base.type == WLR_INPUT_DEVICE_TOUCH;

  phoc_desktop_notify_activity (desktop, self->seat);
  g_debug ("%s %d is_touch: %d", __func__, __LINE__, is_touch);
  if (!is_touch) {
    type = event->state ? PHOC_EVENT_BUTTON_PRESS : PHOC_EVENT_BUTTON_RELEASE;
    handle_gestures_for_event_at (self, self->cursor->x, self->cursor->y, type, event, sizeof (*event));
  }

  phoc_cursor_press_button (self, &event->pointer->base, event->time_msec,
                            event->button, event->state, self->cursor->x, self->cursor->y);
}

/**
 * phoc_cursor_handle_event:
 * @self: The phoc cursor
 * @type: The event type
 * @event: The event
 * @size: The size of the event
 *
 * Feed an event that happened at the cursor coordinates of type `type`
 * to the event system. This gives all gestures that are registered in the compositor
 * a chance to handle the event.
 */
void
phoc_cursor_handle_event (PhocCursor   *self,
                          PhocEventType type,
                          gpointer      event,
                          gsize         size)
{
  handle_gestures_for_event_at (self, self->cursor->x, self->cursor->y, type, event, size);
}


static void
handle_pointer_axis (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursor *self = wl_container_of (listener, self, axis);
  struct wlr_pointer_axis_event *event = data;
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  if (!priv->has_pointer_motion) {
    priv->has_pointer_motion = TRUE;
    phoc_cursor_show (self);
  }
  phoc_desktop_notify_activity (desktop, self->seat);

  send_pointer_axis (self->seat, self->seat->seat->pointer_state.focused_surface, event->time_msec,
                     event->orientation, event->delta, event->delta_discrete, event->source);
}


static void
handle_pointer_frame (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursor *self = wl_container_of (listener, self, frame);

  phoc_desktop_notify_activity (desktop, self->seat);
  wlr_seat_pointer_notify_frame (self->seat->seat);

  // make sure to always send frame events when necessary even when bypassing seat grabs
  wlr_seat_pointer_send_frame (self->seat->seat);
}



void
phoc_cursor_handle_touch_down (PhocCursor                  *self,
                               struct wlr_touch_down_event *event)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = phoc_server_get_desktop (server);
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);
  PhocSeat *seat = self->seat;
  PhocTouchPoint *touch_point;
  double lx, ly;

  touch_point = phoc_cursor_add_touch_point (self, event);
  lx = touch_point->lx;
  ly = touch_point->ly;
  handle_gestures_for_event_at (self, lx, ly, PHOC_EVENT_TOUCH_BEGIN, event, sizeof (*event));

  if (seat->touch_id == -1 && priv->mode == PHOC_CURSOR_PASSTHROUGH) {
    seat->touch_id = event->touch_id;
    seat->touch_x = lx;
    seat->touch_y = ly;
  }

  double sx, sy;
  PhocView *view;
  struct wlr_surface *surface = phoc_desktop_wlr_surface_at (desktop, lx, ly, &sx, &sy, &view);
  bool shell_revealed = phoc_handle_shell_reveal (surface, lx, ly, PHOC_SHELL_REVEAL_TOUCH_THRESHOLD);

  if (!shell_revealed && surface && phoc_seat_allow_input (seat, surface->resource)) {
    struct wlr_surface *root = wlr_surface_get_root_surface (surface);

    send_touch_down (seat, surface, event, sx, sy);

    if (view)
      phoc_seat_set_focus_view (seat, view);

    struct wlr_layer_surface_v1 *wlr_layer = wlr_layer_surface_v1_try_from_wlr_surface (root);
    if (wlr_layer) {
      /* TODO: Use press gesture */
      if (wlr_layer->current.keyboard_interactive) {
        phoc_seat_set_focus_layer (seat, wlr_layer);
      }
    }

    phoc_input_method_relay_im_submit (&seat->im_relay, surface);
  }

  if (G_UNLIKELY (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS))) {
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
        wlr_damage_ring_add_box (&output->damage_ring, &box);
      }
    }
  }
}


void
phoc_cursor_handle_touch_up (PhocCursor                *self,
                             struct wlr_touch_up_event *event)
{
  struct wlr_touch_point *point =
    wlr_seat_touch_get_point (self->seat->seat, event->touch_id);
  PhocTouchPoint *touch_point;
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  touch_point = phoc_cursor_get_touch_point (self, event->touch_id);

  /* Don't process unknown touch points */
  if (!touch_point)
    return;

  handle_gestures_for_event_at (self, touch_point->lx, touch_point->ly,
                                PHOC_EVENT_TOUCH_END, event, sizeof (*event));
  phoc_cursor_remove_touch_point (self, event->touch_id);

  if (self->seat->touch_id == event->touch_id)
    self->seat->touch_id = -1;

  /* If the gesture got canceled don't notify any clients */
  if (!point)
    return;

  if (priv->mode != PHOC_CURSOR_PASSTHROUGH) {
    if (priv->view_state.view)
      phoc_cursor_submit_pending_view_state_change (self);

    priv->mode = PHOC_CURSOR_PASSTHROUGH;
    phoc_cursor_update_focus (self);
  }

  send_touch_up (self->seat, point->surface, event);
}


void
phoc_cursor_handle_touch_motion (PhocCursor                    *self,
                                 struct wlr_touch_motion_event *event)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);
  struct wlr_touch_point *point;
  PhocTouchPoint *touch_point;
  double lx, ly;

  touch_point = phoc_cursor_update_touch_point (self, event);
  g_return_if_fail (touch_point);
  lx = touch_point->lx;
  ly = touch_point->ly;
  handle_gestures_for_event_at (self, lx, ly, PHOC_EVENT_TOUCH_UPDATE, event, sizeof (*event));

  point = wlr_seat_touch_get_point (self->seat->seat, event->touch_id);
  /* If the gesture got canceled don't notify any clients */
  if (!point)
    return;

  PhocOutput *output = phoc_desktop_layout_get_output (desktop, lx, ly);
  if (!output)
    return;

  double sx, sy;
  struct wlr_surface *surface = point->surface;

  // TODO: test with input regions
  if (surface) {
    bool found = false;
    float scale = 1.0;

    struct wlr_surface *root = wlr_surface_get_root_surface (surface);
    struct wlr_layer_surface_v1 *wlr_layer_surface = wlr_layer_surface_v1_try_from_wlr_surface (root);
    if (wlr_layer_surface) {

      struct wlr_box output_box;
      wlr_output_layout_get_box (desktop->layout, output->wlr_output, &output_box);

      PhocLayerSurface *layer_surface;
      wl_list_for_each_reverse (layer_surface, &output->layer_surfaces, link)
      {
        if (layer_surface->layer != wlr_layer_surface->current.layer)
          continue;

        if (layer_surface->layer_surface->surface == root) {
          sx = lx - layer_surface->geo.x - output_box.x;
          sy = ly - layer_surface->geo.y - output_box.y;
          found = true;
          break;
        }
      }
      // try the overlay layer as well since the on-screen keyboard might have been elevated there
      wl_list_for_each_reverse (layer_surface, &output->layer_surfaces, link)
      {
        if (layer_surface->layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
          continue;

        if (layer_surface->layer_surface->surface == root) {
          sx = lx - layer_surface->geo.x - output_box.x;
          sy = ly - layer_surface->geo.y - output_box.y;
          found = true;
          break;
        }
      }
    } else {
      PhocView *view = phoc_view_from_wlr_surface (root);
      if (view) {
        scale = phoc_view_get_scale (view);
        sx = lx / scale - view->box.x;
        sy = ly / scale - view->box.y;
        found = true;
      } else {
        // FIXME: buggy fallback, but at least handles xdg_popups for now...
        surface = phoc_desktop_wlr_surface_at (desktop, lx, ly, &sx, &sy, NULL);
      }
    }

    if (found) {
      struct wlr_surface *sub = surface;
      while (sub) {
        struct wlr_subsurface *subsurface = wlr_subsurface_try_from_wlr_surface (sub);
        if (subsurface == NULL)
          break;

        sx -= subsurface->current.x;
        sy -= subsurface->current.y;
        sub = subsurface->parent;
      }
    }

    if (phoc_seat_allow_input (self->seat, surface->resource))
      send_touch_motion (self->seat, surface, event, sx, sy);
  }

  if (event->touch_id == self->seat->touch_id) {
    self->seat->touch_x = lx;
    self->seat->touch_y = ly;

    if (priv->mode != PHOC_CURSOR_PASSTHROUGH) {
      wlr_cursor_warp (self->cursor, NULL, lx, ly);
      phoc_cursor_update_position (self, event->time_msec);
    }

    if (self->seat->drag_icon != NULL)
      phoc_drag_icon_update_position (self->seat->drag_icon);
  }
}


static void
handle_touch_frame (struct wl_listener *listener, void *data)
{
  PhocCursor *self = PHOC_CURSOR (wl_container_of (listener, self, touch_frame));
  struct wlr_seat *wlr_seat = self->seat->seat;

  wlr_seat_touch_notify_frame(wlr_seat);

  // make sure to always send frame events when necessary even when bypassing seat grabs
  wlr_seat_touch_send_frame (wlr_seat);
}


void
phoc_cursor_handle_tool_axis (PhocCursor                        *self,
                              struct wlr_tablet_tool_axis_event *event)
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

  wlr_cursor_absolute_to_layout_coords (self->cursor, &event->tablet->base,
                                        x, y, &lx, &ly);


  if (self->pointer_view) {
    PhocView *view = self->pointer_view->view;

    if (self->active_constraint &&
        !pixman_region32_contains_point (&self->confine,
                                         floor (lx - view->box.x), floor (ly - view->box.y), NULL)) {
      return;
    }
  }

  wlr_cursor_warp_closest (self->cursor, &event->tablet->base, lx, ly);
  phoc_cursor_update_position (self, event->time_msec);
}

void
phoc_cursor_handle_tool_tip (PhocCursor                       *self,
                             struct wlr_tablet_tool_tip_event *event)
{
  phoc_cursor_press_button (self, &event->tablet->base,
                            event->time_msec, BTN_LEFT, event->state, self->cursor->x,
                            self->cursor->y);
}


void
phoc_cursor_handle_focus_change (PhocCursor                                 *self,
                                 struct wlr_seat_pointer_focus_change_event *event)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  double sx = event->sx;
  double sy = event->sy;

  double lx = self->cursor->x;
  double ly = self->cursor->y;

  g_debug ("entered surface %p, lx: %f, ly: %f, sx: %f, sy: %f",
           event->new_surface, lx, ly, sx, sy);

  phoc_cursor_constrain (self,
                         wlr_pointer_constraints_v1_constraint_for_surface (
                           desktop->pointer_constraints,
                           event->new_surface, self->seat->seat),
                         sx, sy);
}

static void
phoc_cursor_handle_constraint_commit (PhocCursor *self)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());

  double sx, sy;
  struct wlr_surface *surface = phoc_desktop_wlr_surface_at (desktop,
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

  g_assert (self->active_constraint->surface == data);
  phoc_cursor_handle_constraint_commit (self);
}

void
phoc_cursor_constrain (PhocCursor *self,
                       struct wlr_pointer_constraint_v1 *constraint,
                       double sx, double sy)
{
  if (self->active_constraint == constraint)
    return;

  g_debug ("cursor constrain: %p, new: %p, old: %p", self, constraint, self->active_constraint);

  wl_list_remove (&self->constraint_commit.link);
  wl_list_init (&self->constraint_commit.link);

  if (self->active_constraint)
    wlr_pointer_constraint_v1_send_deactivated (self->active_constraint);

  self->active_constraint = constraint;
  if (constraint == NULL)
    return;

  wlr_pointer_constraint_v1_send_activated (constraint);

  wl_signal_add (&constraint->surface->events.commit, &self->constraint_commit);
  self->constraint_commit.notify = handle_constraint_commit;

  pixman_region32_clear (&self->confine);

  pixman_region32_t *region = &constraint->region;

  if (!pixman_region32_contains_point (region, floor (sx), floor (sy), NULL)) {
    /* Warp into region if possible */
    int nboxes;
    pixman_box32_t *boxes = pixman_region32_rectangles (region, &nboxes);

    if (nboxes > 0) {
      PhocView *view = self->pointer_view->view;

      double lx = view->box.x + (boxes[0].x1 + boxes[0].x2) / 2.0;
      double ly = view->box.y + (boxes[0].y1 + boxes[0].y2) / 2.0;

      wlr_cursor_warp_closest (self->cursor, NULL, lx, ly);
    }
  }

  /* A locked pointer will result in an empty region, thus disallowing
   * all movement */
  if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED)
    pixman_region32_copy (&self->confine, region);
}


PhocCursor *
phoc_cursor_new (PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_CURSOR,
                       "seat", seat,
                       NULL);
}


/**
 * phoc_cursor_add_gesture:
 * @self: The cursor
 * @gesture: A gesture
 *
 * Adds a gesture to the list of gestures handled by @self.
 */
void
phoc_cursor_add_gesture (PhocCursor   *self,
                         PhocGesture  *gesture)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  priv->gestures = g_slist_append (priv->gestures, g_object_ref (gesture));
}

/**
 * phoc_cursor_get_gestures:
 * @self: The cursor
 *
 * Gets the currently registered gestures of @self.
 *
 * Returns: (transfer none) (nullable) (element-type PhocGesture): The cursor's gestures
 */
GSList *
phoc_cursor_get_gestures (PhocCursor *self)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  return priv->gestures;
}

/**
 * phoc_cursor_is_active_touch_id:
 * @self: The cursor
 * @touch_id: touch point ID
 *
 * Checks whether the given touch is is in the list of active
 * touch points.
 *
 * Returns: %TRUE if the touch point is active, otherwise %FALSE
 */
gboolean
phoc_cursor_is_active_touch_id (PhocCursor *self, int touch_id)
{
  PhocCursorPrivate *priv = phoc_cursor_get_instance_private (self);

  return !!g_hash_table_lookup (priv->touch_points, GINT_TO_POINTER (touch_id));
}

/**
 * phoc_cursor_set_name:
 * @self: The cursor
 * @name: (nullable): a cursor name
 *
 * Select a cursor from the cursor theme by its name. To use a surface see
 * [method@Cursor.set_image].
 */
void
phoc_cursor_set_name (PhocCursor *self, struct wl_client *client, const char *name)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  phoc_cursor_set_image_surface (self, NULL);
  priv->hotspot_x = priv->hotspot_y = 0;
  priv->image_name = name;
  priv->image_client = client;

  /* Seat does not have a usable pointing device */
  if (!phoc_seat_has_pointer (self->seat) || !priv->has_pointer_motion) {
    wlr_cursor_unset_image (self->cursor);
    return;
  }

  if (!priv->image_name) {
    wlr_cursor_unset_image (self->cursor);
    return;
  }

  wlr_cursor_set_xcursor (self->cursor, priv->xcursor_manager, priv->image_name);
}

/**
 * phoc_cursor_set_image:
 * @self: The cursor
 * @client: The client to set the image force
 * @surface: The image surface to use
 * @hotspot_x: The x coordinate of the hotspot on the surface
 * @hotspot_y: The y coordinate of the hotspot on the surface
 *
 * Set the cursor image via a surface. To use an image from the cursor
 * theme see [method@Cursor.set_image].
 */
void
phoc_cursor_set_image (PhocCursor         *self,
                       struct wl_client   *client,
                       struct wlr_surface *surface,
                       int32_t             hotspot_x,
                       int32_t             hotspot_y)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  phoc_cursor_set_image_surface (self, surface);
  priv->image_name = NULL;
  priv->hotspot_x = hotspot_x;
  priv->hotspot_y = hotspot_y;
  priv->image_client = client;

  /* Seat does not have a usable pointing device */
  if (!phoc_seat_has_pointer (self->seat) || !priv->has_pointer_motion)
    return;

  wlr_cursor_set_surface (self->cursor, surface, hotspot_x, hotspot_y);
}

/**
 * phoc_cursor_set_mode:
 * @self: The cursor
 * @mode: The cursor mode
 *
 * Set the cursor mode
 */
void
phoc_cursor_set_mode (PhocCursor *self, PhocCursorMode mode)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  priv->mode = mode;
}

/**
 * phoc_cursor_get_mode:
 * @self: The cursor
 *
 * Get the current cursor mode
 *
 * Returns: The cursor mode
 */
PhocCursorMode
phoc_cursor_get_mode (PhocCursor *self)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  return priv->mode;
}

/**
 * phoc_cursor_set_xcursor_theme:
 * @self: The cursor
 * @theme: The theme to set
 *
 * Set the current cursor theme
 */
void
phoc_cursor_set_xcursor_theme (PhocCursor *self, const char *theme, uint32_t size)
{
  PhocCursorPrivate *priv;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  g_clear_pointer (&priv->xcursor_manager, wlr_xcursor_manager_destroy);
  priv->xcursor_manager = wlr_xcursor_manager_create (theme, size);
  g_assert (priv->xcursor_manager);

  phoc_cursor_configure_xcursor (self);
}

/**
 * phoc_cursor_configure_xcursor:
 * @self: The cursor
 *
 * Load cursor theme for the current output scales and set a default
 * cursor.
 */
void
phoc_cursor_configure_xcursor (PhocCursor *self)
{
  PhocCursorPrivate *priv;
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocOutput *output;

  g_assert (PHOC_IS_CURSOR (self));
  priv = phoc_cursor_get_instance_private (self);

  wl_list_for_each (output, &desktop->outputs, link) {
    float scale = phoc_output_get_scale (output);
    if (!wlr_xcursor_manager_load (priv->xcursor_manager, scale)) {
      g_critical ("Cannot load xcursor theme for output '%s' "
                  "with scale %f", output->wlr_output->name, scale);
    }
  }

  phoc_cursor_set_name (self, NULL, PHOC_XCURSOR_DEFAULT);
  wlr_cursor_warp (self->cursor, NULL, self->cursor->x, self->cursor->y);
}
