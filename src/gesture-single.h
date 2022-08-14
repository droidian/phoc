/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "gesture.h"

G_BEGIN_DECLS

#define PHOC_TYPE_GESTURE_SINGLE (phoc_gesture_single_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhocGestureSingle, phoc_gesture_single, PHOC, GESTURE_SINGLE, PhocGesture)

/**
 * PhocGestureSingleClass:
 * @parent_class: The parent class
 */
struct _PhocGestureSingleClass
{
  PhocGestureClass parent_class;

};

PhocGestureSingle     *phoc_gesture_single_new                      (void);
PhocEventSequence     *phoc_gesture_single_get_current_sequence     (PhocGestureSingle *self);

G_END_DECLS




