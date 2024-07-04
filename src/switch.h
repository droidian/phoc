/* Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "input.h"
#include "input-device.h"

#include <wlr/types/wlr_switch.h>

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PhocSwitchState:
 * @PHOC_SWITCH_STATE_OFF: Switch is off
 * @PHOC_SWITCH_STATE_ON: Swtich is on
 *
 * State of a switch (e.g. a tablet mode or lid switch).
 */
typedef enum _PhocSwitchState {
  PHOC_SWITCH_STATE_OFF     =  0,
  PHOC_SWITCH_STATE_ON      =  1,
} PhocSwitchState;

#define PHOC_TYPE_SWITCH (phoc_switch_get_type ())

G_DECLARE_FINAL_TYPE (PhocSwitch, phoc_switch, PHOC, SWITCH, PhocInputDevice);

PhocSwitch             *phoc_switch_new                            (struct wlr_input_device *device,
                                                                    PhocSeat                *seat);
gboolean                phoc_switch_is_tablet_mode_switch          (PhocSwitch              *self);
gboolean                phoc_switch_is_lid_switch                  (PhocSwitch              *self);
gboolean                phoc_switch_is_type                        (PhocSwitch              *self,
                                                                    enum wlr_switch_type     type);

G_END_DECLS
