/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "gesture-single.h"

G_BEGIN_DECLS

#define PHOC_TYPE_GESTURE_SWIPE (phoc_gesture_swipe_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhocGestureSwipe, phoc_gesture_swipe, PHOC, GESTURE_SWIPE, PhocGestureSingle)

/**
 * PhocGestureSwipeClass:
 * @parent_class: The parent class
 */
struct _PhocGestureSwipeClass
{
  PhocGestureSingleClass parent_class;

  void (*swipe)    (PhocGestureSwipe *self, double velocity_x, double velocity_y);
};

PhocGestureSwipe *phoc_gesture_swipe_new (void);
gboolean          phoc_gesture_swipe_get_velocity (PhocGestureSwipe *self,
                                                   gdouble          *velocity_x,
                                                   gdouble          *velocity_y);


G_END_DECLS
