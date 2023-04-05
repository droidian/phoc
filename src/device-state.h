/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "seat.h"

#include <phoc-device-state-unstable-v1-protocol.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_DEVICE_STATE (phoc_device_state_get_type ())

G_DECLARE_FINAL_TYPE (PhocDeviceState, phoc_device_state, PHOC, DEVICE_STATE, GObject)

PhocDeviceState     *phoc_device_state_new                          (PhocSeat        *seat);
void                 phoc_device_state_update_capabilities          (PhocDeviceState *self);
void                 phoc_device_state_notify_lid_change            (PhocDeviceState *self,
                                                                     gboolean         closed);
void                 phoc_device_state_notify_keypad_switch_change  (PhocDeviceState *self,
                                                                     gboolean         closed);
void                 phoc_device_state_notify_tablet_mode_change    (PhocDeviceState *self,
                                                                     gboolean         closed);

G_END_DECLS
