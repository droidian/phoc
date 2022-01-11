#pragma once

#include "input.h"
#include "input-device.h"

#include <gio/gio.h>
#include <glib-object.h>

#define PHOC_TYPE_POINTER (phoc_pointer_get_type ())

G_DECLARE_FINAL_TYPE (PhocPointer, phoc_pointer, PHOC, POINTER, PhocInputDevice);

PhocPointer *phoc_pointer_new (struct wlr_input_device *device, PhocSeat *seat);
