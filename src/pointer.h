#pragma once

#include "input.h"

#include <gio/gio.h>
#include <glib-object.h>

#define PHOC_TYPE_POINTER (phoc_pointer_get_type ())

G_DECLARE_FINAL_TYPE (PhocPointer, phoc_pointer, PHOC, POINTER, GObject);

/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
struct _PhocPointer {
  GObject                  parent;

  struct wlr_input_device *device;
  struct wl_listener       device_destroy;
  struct wl_list           link;

  /* private */
  GSettings               *input_settings;
  PhocSeat                *seat;
  gboolean                 touchpad;
  GSettings               *touchpad_settings;
  GSettings               *mouse_settings;
};

PhocPointer *phoc_pointer_new (struct wlr_input_device *device, PhocSeat *seat);
