/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on gtk's gtkgesturezoom.c which is
 * Copyright (C) 2014, Red Hat, Inc.
 * Author(s): Carlos Garnacho <carlosg@gnome.org>
 */

#include "phoc-config.h"

#include "gesture-zoom.h"

#include <math.h>

enum {
  SCALE_CHANGED,
  LAST_SIGNAL
};

/**
 * PhocGestureZoom:
 *
 * A zoom gesture
 *
 * #PhocGestureZoom is a #PhocGesture implementation able to recognize
 * pinch/zoom gestures, whenever the distance between both tracked
 * sequences changes, the #PhocGestureZoom::scale-changed signal is
 * emitted to report the scale factor.
 */
typedef struct _PhocGestureZoomPrivate {
  gdouble initial_distance;
} PhocGestureZoomPrivate;

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (PhocGestureZoom, phoc_gesture_zoom, PHOC_TYPE_GESTURE)

static void
phoc_gesture_zoom_init (PhocGestureZoom *gesture)
{
}

static GObject *
phoc_gesture_zoom_constructor (GType type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
  GObject *object;

  object = G_OBJECT_CLASS (phoc_gesture_zoom_parent_class)->constructor (type,
                                                                         n_construct_properties,
                                                                         construct_properties);
  g_object_set (object, "n-points", 2, NULL);

  return object;
}

static gboolean
_phoc_gesture_zoom_get_distance (PhocGestureZoom *zoom,
                                 gdouble         *distance)
{
  const PhocEvent *last_event;
  gdouble x1, y1, x2, y2;
  PhocGesture *gesture;
  GList *sequences = NULL;
  gdouble dx, dy;
  gboolean retval = FALSE;

  gesture = PHOC_GESTURE (zoom);

  if (!phoc_gesture_is_recognized (gesture))
    goto out;

  sequences = phoc_gesture_get_sequences (gesture);
  if (!sequences)
    goto out;

  last_event = phoc_gesture_get_last_event (gesture, sequences->data);

  /* TODO: Handle PHOC_EVENT_TOUCHPAD_PINCH_{BEGIN,END} too
     (we don't have scale in the event struct atm */
  if (last_event->type == PHOC_EVENT_TOUCHPAD_PINCH_UPDATE) {
    *distance = last_event->touchpad_pinch_update.scale;
  } else {
    if (!sequences->next)
      goto out;

    phoc_gesture_get_point (gesture, sequences->data, &x1, &y1);
    phoc_gesture_get_point (gesture, sequences->next->data, &x2, &y2);

    dx = x1 - x2;
    dy = y1 - y2;;
    *distance = sqrt ((dx * dx) + (dy * dy));
  }

  retval = TRUE;
out:
  g_list_free (sequences);
  return retval;
}

static gboolean
_phoc_gesture_zoom_check_emit (PhocGestureZoom *gesture)
{
  PhocGestureZoomPrivate *priv;
  gdouble distance, zoom;

  if (!_phoc_gesture_zoom_get_distance (gesture, &distance))
    return FALSE;

  priv = phoc_gesture_zoom_get_instance_private (gesture);

  if (distance == 0 || priv->initial_distance == 0)
    return FALSE;

  zoom = distance / priv->initial_distance;
  g_signal_emit (gesture, signals[SCALE_CHANGED], 0, zoom);

  return TRUE;
}

static gboolean
phoc_gesture_zoom_filter_event (PhocGesture     *gesture,
                                const PhocEvent *event)
{
  /* Let 2-finger touchpad pinch events go through */
  if (event->type == PHOC_EVENT_TOUCHPAD_PINCH_BEGIN ||
      event->type == PHOC_EVENT_TOUCHPAD_PINCH_UPDATE ||
      event->type == PHOC_EVENT_TOUCHPAD_PINCH_END) {
    return !(event->touchpad_pinch_begin.fingers == 2);
  }

  return PHOC_GESTURE_CLASS (phoc_gesture_zoom_parent_class)->filter_event (gesture, event);
}

static void
phoc_gesture_zoom_begin (PhocGesture       *gesture,
                         PhocEventSequence *sequence)
{
  PhocGestureZoom *zoom = PHOC_GESTURE_ZOOM (gesture);
  PhocGestureZoomPrivate *priv;

  priv = phoc_gesture_zoom_get_instance_private (zoom);
  _phoc_gesture_zoom_get_distance (zoom, &priv->initial_distance);
}

static void
phoc_gesture_zoom_update (PhocGesture       *gesture,
                          PhocEventSequence *sequence)
{
  _phoc_gesture_zoom_check_emit (PHOC_GESTURE_ZOOM (gesture));
}

static void
phoc_gesture_zoom_class_init (PhocGestureZoomClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocGestureClass *gesture_class = PHOC_GESTURE_CLASS (klass);

  object_class->constructor = phoc_gesture_zoom_constructor;

  gesture_class->filter_event = phoc_gesture_zoom_filter_event;
  gesture_class->begin = phoc_gesture_zoom_begin;
  gesture_class->update = phoc_gesture_zoom_update;

  /**
   * PhocGestureZoom::scale-changed:
   * @controller: the object on which the signal is emitted
   * @scale: Scale delta, taking the initial state as 1:1
   *
   * This signal is emitted whenever the distance between both tracked
   * sequences changes.
   */
  signals[SCALE_CHANGED] =
    g_signal_new ("scale-changed",
                  PHOC_TYPE_GESTURE_ZOOM,
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (PhocGestureZoomClass, scale_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_DOUBLE);
}

/**
 * phoc_gesture_zoom_new:
 *
 * Returns a newly created #PhocGesture that recognizes zoom
 * in/out gestures (usually known as pinch/zoom).
 *
 * Returns: a newly created #PhocGestureZoom
 **/
PhocGestureZoom *
phoc_gesture_zoom_new (void)
{
  return g_object_new (PHOC_TYPE_GESTURE_ZOOM, NULL);
}

/**
 * phoc_gesture_zoom_get_scale_delta:
 * @gesture: The zoom gesture
 *
 * If @gesture is active, this function returns the zooming difference
 * since the gesture was recognized (hence the starting point is
 * considered 1:1). If @gesture is not active, 1 is returned.
 *
 * Returns: the scale delta
 **/
gdouble
phoc_gesture_zoom_get_scale_delta (PhocGestureZoom *gesture)
{
  PhocGestureZoomPrivate *priv;
  gdouble distance;

  g_return_val_if_fail (PHOC_IS_GESTURE_ZOOM (gesture), 1.0);

  if (!_phoc_gesture_zoom_get_distance (gesture, &distance))
    return 1.0;

  priv = phoc_gesture_zoom_get_instance_private (gesture);

  return distance / priv->initial_distance;
}
