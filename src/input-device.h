/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "seat.h"

#include <glib-object.h>
#include <wlr/types/wlr_input_device.h>


G_BEGIN_DECLS

#define PHOC_TYPE_INPUT_DEVICE (phoc_input_device_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhocInputDevice, phoc_input_device, PHOC, INPUT_DEVICE, GObject)

/**
 * PhocInputDeviceClass:
 * @parent_class: The parent class
 */
struct _PhocInputDeviceClass {
  GObjectClass parent_class;

  /* Signals
   */
  void (*device_destroy)   (PhocInputDevice    *self);
};

PhocSeat                *phoc_input_device_get_seat                       (PhocInputDevice *self);
struct wlr_input_device *phoc_input_device_get_device                     (PhocInputDevice *self);
gboolean                 phoc_input_device_get_is_touchpad                (PhocInputDevice *self);
gboolean                 phoc_input_device_get_is_keyboard                (PhocInputDevice *self);
gboolean                 phoc_input_device_get_is_libinput                (PhocInputDevice *self);
struct libinput_device  *phoc_input_device_get_libinput_device_handle     (PhocInputDevice *self);
const char              *phoc_input_device_get_name                       (PhocInputDevice *self);
enum wlr_input_device_type phoc_input_device_get_device_type              (PhocInputDevice *self);
const char              *phoc_input_device_get_vendor_id                  (PhocInputDevice *self);
const char              *phoc_input_device_get_product_id                 (PhocInputDevice *self);

G_END_DECLS
