/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on gtk's gtkgesture.c which is
 * Copyright (C) 2014, Red Hat, Inc.
 * Author(s): Carlos Garnacho <carlosg@gnome.org>
 */

#define G_LOG_DOMAIN "phoc-gesture-drag"

#include "phoc-config.h"

#include "gesture-drag.h"
#include "phoc-marshalers.h"

enum {
  DRAG_BEGIN,
  DRAG_END,
  DRAG_UPDATE,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

/**
 * PhocGestureDrag:
 *
 * A drag gesture.
 */
typedef struct _PhocGestureDragPrivate {
  double start_x;
  double start_y;
  double last_x;
  double last_y;
} PhocGestureDragPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhocGestureDrag, phoc_gesture_drag, PHOC_TYPE_GESTURE_SINGLE)


static gboolean
phoc_gesture_drag_filter_event (PhocGesture       *gesture,
                                const PhocEvent   *event)
{
  /* Let touchpad swipe events go through, only if they match n-points  */
  if (phoc_event_is_touchpad_gesture (event)) {
      guint n_points;
      guint n_fingers;

      g_object_get (G_OBJECT (gesture), "n-points", &n_points, NULL);
      /* FIXME: wlr end events don't have n_fingers so this always fails */
      n_fingers = phoc_event_get_touchpad_gesture_n_fingers (event);

      if (n_fingers == n_points)
        return FALSE;
      else
        return TRUE;
  }

  return PHOC_GESTURE_CLASS (phoc_gesture_drag_parent_class)->filter_event (gesture, event);
}


static void
phoc_gesture_drag_begin (PhocGesture       *gesture,
                        PhocEventSequence  *sequence)
{
  PhocGestureDragPrivate *priv;
  PhocEventSequence *current;

  g_debug ("%s (%d):", __func__, __LINE__);

  current = phoc_gesture_single_get_current_sequence (PHOC_GESTURE_SINGLE (gesture));

  priv = phoc_gesture_drag_get_instance_private (PHOC_GESTURE_DRAG (gesture));
  phoc_gesture_get_point (gesture, current, &priv->start_x, &priv->start_y);
  priv->last_x = priv->start_x;
  priv->last_y = priv->start_y;

  g_signal_emit (gesture, signals[DRAG_BEGIN], 0, priv->start_x, priv->start_y);
}


static void
phoc_gesture_drag_update (PhocGesture       *gesture,
                         PhocEventSequence *sequence)
{
  PhocGestureDragPrivate *priv;
  double x, y;

  priv = phoc_gesture_drag_get_instance_private (PHOC_GESTURE_DRAG (gesture));
  phoc_gesture_get_point (gesture, sequence, &priv->last_x, &priv->last_y);
  x = priv->last_x - priv->start_x;
  y = priv->last_y - priv->start_y;

  g_signal_emit (gesture, signals[DRAG_UPDATE], 0, x, y);
}


static void
phoc_gesture_drag_end (PhocGesture       *gesture,
                       PhocEventSequence *sequence)
{
  PhocGestureDragPrivate *priv;
  PhocEventSequence *current;
  double x, y;

  current = phoc_gesture_single_get_current_sequence (PHOC_GESTURE_SINGLE (gesture));

  priv = phoc_gesture_drag_get_instance_private (PHOC_GESTURE_DRAG (gesture));
  phoc_gesture_get_point (gesture, current, &priv->last_x, &priv->last_y);
  x = priv->last_x - priv->start_x;
  y = priv->last_y - priv->start_y;

  g_signal_emit (gesture, signals[DRAG_END], 0, x, y);
}


static void
phoc_gesture_drag_class_init (PhocGestureDragClass *klass)
{
  PhocGestureClass *gesture_class = PHOC_GESTURE_CLASS (klass);

  gesture_class->filter_event = phoc_gesture_drag_filter_event;
  gesture_class->begin = phoc_gesture_drag_begin;
  gesture_class->update = phoc_gesture_drag_update;
  gesture_class->end = phoc_gesture_drag_end;

  /**
   * PhocGestureDrag::drag-begin:
   * @gesture: the object which received the signal
   * @start_x: X coordinate in layout coordinates
   * @start_y: Y coordinate in layout coordinates
   *
   * This signal is emitted whenever dragging starts.
   */
  signals[DRAG_BEGIN] =
    g_signal_new ("drag-begin",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureDragClass, drag_begin),
                  NULL, NULL,
                  _phoc_marshal_VOID__DOUBLE_DOUBLE,
                  G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  g_signal_set_va_marshaller (signals[DRAG_BEGIN],
                              G_TYPE_FROM_CLASS (klass),
                              _phoc_marshal_VOID__DOUBLE_DOUBLEv);
  /**
   * PhocGestureDrag::drag-update:
   * @gesture: the object which received the signal
   * @offset_x: X offset, relative to the start point
   * @offset_y: Y offset, relative to the start point
   *
   * This signal is emitted whenever the dragging point moves.
   */
  signals[DRAG_UPDATE] =
    g_signal_new ("drag-update",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureDragClass, drag_update),
                  NULL, NULL,
                  _phoc_marshal_VOID__DOUBLE_DOUBLE,
                  G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  g_signal_set_va_marshaller (signals[DRAG_UPDATE],
                              G_TYPE_FROM_CLASS (klass),
                              _phoc_marshal_VOID__DOUBLE_DOUBLEv);
  /**
   * PhocGestureDrag::drag-end:
   * @gesture: the object which received the signal
   * @offset_x: X offset, relative to the start point
   * @offset_y: Y offset, relative to the start point
   *
   * This signal is emitted whenever the dragging is finished.
   */
  signals[DRAG_END] =
    g_signal_new ("drag-end",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureDragClass, drag_end),
                  NULL, NULL,
                  _phoc_marshal_VOID__DOUBLE_DOUBLE,
                  G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  g_signal_set_va_marshaller (signals[DRAG_END],
                              G_TYPE_FROM_CLASS (klass),
                              _phoc_marshal_VOID__DOUBLE_DOUBLEv);
}


static void
phoc_gesture_drag_init (PhocGestureDrag *self)
{
}


PhocGestureDrag *
phoc_gesture_drag_new (void)
{
  return g_object_new (PHOC_TYPE_GESTURE_DRAG, NULL);
}
