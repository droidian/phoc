#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include "settings.h"

#define PHOC_TYPE_TOUCH (phoc_touch_get_type ())

G_DECLARE_FINAL_TYPE (PhocTouch, phoc_touch, PHOC, TOUCH, GObject);

/* These need to know about PhocTouch so we have them after the type definition.
 * This will fix itself once output / view / phosh are gobjects and most of
 * their members are non-public. */
#include "output.h"
#include "cursor.h"
#include "server.h"
#include "seat.h"
#include "view.h"

/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
struct _PhocTouch {
  GObject                  parent;

  struct wl_list           link; // seat::touch
  struct wl_listener       touch_destroy;
  PhocSeat                *seat;
  struct wlr_input_device *device;

};

PhocTouch *phoc_touch_new (struct wlr_input_device *device, PhocSeat *seat);
