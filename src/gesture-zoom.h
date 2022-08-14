/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "gesture.h"

#pragma once

G_BEGIN_DECLS

#define PHOC_TYPE_GESTURE_ZOOM (phoc_gesture_zoom_get_type ())
G_DECLARE_DERIVABLE_TYPE (PhocGestureZoom, phoc_gesture_zoom, PHOC, GESTURE_ZOOM, PhocGesture)

struct _PhocGestureZoomClass {
  PhocGestureClass parent_class;

  void             (* scale_changed) (PhocGestureZoom *self, gdouble scale);
};

PhocGestureZoom *phoc_gesture_zoom_new (void);
gdouble          phoc_gesture_zoom_get_scale_delta (PhocGestureZoom *gesture);

G_END_DECLS
