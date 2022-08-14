/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "gesture-single.h"

G_BEGIN_DECLS

#define PHOC_TYPE_GESTURE_DRAG (phoc_gesture_drag_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhocGestureDrag, phoc_gesture_drag, PHOC, GESTURE_DRAG, PhocGestureSingle)

PhocGestureDrag *phoc_gesture_drag_new (void);

/**
 * PhocGestureDragClass:
 * @parent_class: The parent class
 */
struct _PhocGestureDragClass
{
  PhocGestureSingleClass parent_class;

  void (*drag_begin)    (PhocGestureDrag *self, double x, double y);
  void (*drag_update)   (PhocGestureDrag *self, double x, double y);
  void (*drag_end)      (PhocGestureDrag *self, double x, double y);
};

G_END_DECLS




