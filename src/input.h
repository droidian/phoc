#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include "settings.h"

#define PHOC_TYPE_INPUT (phoc_input_get_type ())

G_DECLARE_FINAL_TYPE (PhocInput, phoc_input, PHOC, INPUT, GObject);

/* These need to know about PhocInput so we have them after the type definition.
 * This will fix itself once output / view / phosh are gobjects and most of
 * their members are non-public. */
#include "output.h"
#include "cursor.h"
#include "server.h"
#include "view.h"

/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
struct _PhocInput {
  GObject              parent;

  struct wl_listener   new_input;
  struct wl_list       seats; // roots_seat::link */

  /* private */
  struct roots_config *config;
};

PhocInput         *phoc_input_new (struct roots_config *config);
bool               phoc_input_view_has_focus (PhocInput         *self,
                                              struct roots_view *view);
const char        *phoc_input_get_device_type (enum wlr_input_device_type type);
struct roots_seat *phoc_input_get_seat (PhocInput *self, char *name);
struct roots_seat *phoc_input_last_active_seat (PhocInput *self);
void               phoc_input_update_cursor_focus (PhocInput *self);
struct roots_seat *phoc_input_seat_from_wlr_seat (PhocInput       *self,
                                                  struct wlr_seat *seat);
