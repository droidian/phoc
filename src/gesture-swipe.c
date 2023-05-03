/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on gtk's gtkgesturesingle.c which is
 * Copyright (C) 2012, One Laptop Per Child.
 * Copyright (C) 2014, Red Hat, Inc.
 * Author(s): Carlos Garnacho <carlosg@gnome.org>
 */

#define G_LOG_DOMAIN "phoc-gesture-swipe"

#include "phoc-config.h"

#include "gesture-swipe.h"
#include "phoc-marshalers.h"

#define CAPTURE_THRESHOLD_MS 150

/**
 * PhocGestureSwipe:
 *
 * `PhocGestureSwipe` is a `PhocGesture` for swipe gestures.
 *
 * After a press/move/.../move/release sequence happens, the
 * [signal@Phoc.GestureSwipe::swipe] signal will be emitted,
 * providing the velocity and directionality of the sequence
 * at the time it was lifted.
 *
 * If the velocity is desired in intermediate points,
 * [method@Phoc.GestureSwipe.get_velocity] can be called in a
 * [signal@Phoc.Gesture::update] handler.
 *
 * All velocities are reported in pixels/sec units.
 */

enum {
  SWIPE,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

typedef struct _EventData
{
  guint32 evtime;
  int x;
  int y;
} EventData;

typedef struct _PhocGestureSwipePrivate {
  GArray *events;
} PhocGestureSwipePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhocGestureSwipe, phoc_gesture_swipe, PHOC_TYPE_GESTURE_SINGLE)


static void
phoc_gesture_swipe_finalize (GObject *object)
{
  PhocGestureSwipe *self = PHOC_GESTURE_SWIPE(object);
  PhocGestureSwipePrivate *priv = phoc_gesture_swipe_get_instance_private (self);

  g_array_free (priv->events, TRUE);

  G_OBJECT_CLASS (phoc_gesture_swipe_parent_class)->finalize (object);
}


static gboolean
phoc_gesture_swipe_filter_event (PhocGesture     *gesture,
                                 const PhocEvent *event)
{
  /* Let touchpad swipe events go through, only if they match n-points  */
  if (phoc_event_is_touchpad_gesture (event)) {
    guint n_points;
    guint n_fingers;

    g_object_get (G_OBJECT (gesture), "n-points", &n_points, NULL);

    n_fingers = phoc_event_get_touchpad_gesture_n_fingers (event);

    /* FIXME: Let 0 fingers pass since UP events currently lack fingers */
    if (n_fingers == n_points || n_fingers == 0)
      return FALSE;
    else
      return TRUE;
  }

  return PHOC_GESTURE_CLASS (phoc_gesture_swipe_parent_class)->filter_event (gesture, event);
}

static void
phoc_gesture_swipe_clear_backlog (PhocGestureSwipe *gesture,
                                  guint32           evtime)
{
  PhocGestureSwipePrivate *priv;
  int i, length = 0;

  priv = phoc_gesture_swipe_get_instance_private (gesture);

  for (i = 0; i < (int) priv->events->len; i++) {
    EventData *data;

    data = &g_array_index (priv->events, EventData, i);
    if (data->evtime >= evtime - CAPTURE_THRESHOLD_MS) {
      length = i - 1;
      break;
    }
  }

  if (length > 0)
    g_array_remove_range (priv->events, 0, length);
}

static void
phoc_gesture_swipe_append_event (PhocGestureSwipe  *swipe,
                                PhocEventSequence *sequence)
{
  PhocGestureSwipePrivate *priv;
  EventData new;
  double x, y;

  priv = phoc_gesture_swipe_get_instance_private (swipe);
  phoc_gesture_get_last_update_time (PHOC_GESTURE (swipe), sequence, &new.evtime);
  phoc_gesture_get_point (PHOC_GESTURE (swipe), sequence, &x, &y);

  new.x = x;
  new.y = y;

  phoc_gesture_swipe_clear_backlog (swipe, new.evtime);
  g_array_append_val (priv->events, new);
}

static void
phoc_gesture_swipe_update (PhocGesture       *gesture,
                           PhocEventSequence *sequence)
{
  PhocGestureSwipe *self = PHOC_GESTURE_SWIPE (gesture);

  phoc_gesture_swipe_append_event (self, sequence);
}

static void
_phoc_gesture_swipe_calculate_velocity (PhocGestureSwipe *gesture,
                                        double           *velocity_x,
                                        double           *velocity_y)
{
  PhocGestureSwipePrivate *priv;
  PhocEventSequence *sequence;
  guint32 evtime, diff_time;
  EventData *start, *end;
  double diff_x, diff_y;

  priv = phoc_gesture_swipe_get_instance_private (gesture);
  *velocity_x = *velocity_y = 0;

  sequence = phoc_gesture_single_get_current_sequence (PHOC_GESTURE_SINGLE (gesture));
  phoc_gesture_get_last_update_time (PHOC_GESTURE (gesture), sequence, &evtime);
  phoc_gesture_swipe_clear_backlog (gesture, evtime);

  if (priv->events->len == 0)
    return;

  start = &g_array_index (priv->events, EventData, 0);
  end = &g_array_index (priv->events, EventData, priv->events->len - 1);

  diff_time = end->evtime - start->evtime;
  diff_x = end->x - start->x;
  diff_y = end->y - start->y;

  if (diff_time == 0)
    return;

  /* Velocity in pixels/sec */
  *velocity_x = diff_x * 1000 / diff_time;
  *velocity_y = diff_y * 1000 / diff_time;
}

static void
phoc_gesture_swipe_end (PhocGesture       *gesture,
                       PhocEventSequence *sequence)
{
  PhocGestureSwipe *swipe = PHOC_GESTURE_SWIPE (gesture);
  PhocGestureSwipePrivate *priv;
  double velocity_x, velocity_y;
  PhocEventSequence *seq;

  seq = phoc_gesture_single_get_current_sequence (PHOC_GESTURE_SINGLE (gesture));

  if (phoc_gesture_get_sequence_state (gesture, seq) == PHOC_EVENT_SEQUENCE_DENIED)
    return;

  if (phoc_gesture_is_active (gesture))
    return;

  phoc_gesture_swipe_append_event (swipe, sequence);

  priv = phoc_gesture_swipe_get_instance_private (swipe);
  _phoc_gesture_swipe_calculate_velocity (swipe, &velocity_x, &velocity_y);
  g_signal_emit (gesture, signals[SWIPE], 0, velocity_x, velocity_y);

  if (priv->events->len > 0)
    g_array_remove_range (priv->events, 0, priv->events->len);
}


static void
phoc_gesture_swipe_class_init (PhocGestureSwipeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocGestureClass *gesture_class = PHOC_GESTURE_CLASS (klass);

  object_class->finalize = phoc_gesture_swipe_finalize;

  gesture_class->filter_event = phoc_gesture_swipe_filter_event;
  gesture_class->update = phoc_gesture_swipe_update;
  gesture_class->end = phoc_gesture_swipe_end;

  /**
   * PhocGestureSwipe::swipe:
   * @gesture: object which received the signal
   * @velocity_x: velocity in the X axis, in pixels/sec
   * @velocity_y: velocity in the Y axis, in pixels/sec
   *
   * This signal is emitted when the recognized gesture is finished, velocity
   * and direction are a product of previously recorded events.
   */
  signals[SWIPE] =
    g_signal_new ("swipe",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureSwipeClass, swipe),
                  NULL, NULL,
                  _phoc_marshal_VOID__DOUBLE_DOUBLE,
                  G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  g_signal_set_va_marshaller (signals[SWIPE],
                              G_TYPE_FROM_CLASS (klass),
                              _phoc_marshal_VOID__DOUBLE_DOUBLEv);
}


static void
phoc_gesture_swipe_init (PhocGestureSwipe *self)
{
  PhocGestureSwipePrivate *priv;

  priv = phoc_gesture_swipe_get_instance_private (self);
  priv->events = g_array_new (FALSE, FALSE, sizeof (EventData));
}


PhocGestureSwipe *
phoc_gesture_swipe_new (void)
{
  return PHOC_GESTURE_SWIPE (g_object_new (PHOC_TYPE_GESTURE_SWIPE, NULL));
}

/**
 * phoc_gesture_swipe_get_velocity:
 * @self: a #PhocGestureSwipe
 * @velocity_x: (out): return value for the velocity in the X axis, in pixels/sec
 * @velocity_y: (out): return value for the velocity in the Y axis, in pixels/sec
 *
 * If the gesture is recognized, this function returns %TRUE and fill in
 * @velocity_x and @velocity_y with the recorded velocity, as per the
 * last event(s) processed.
 *
 * Returns: whether velocity could be calculated
 **/
gboolean
phoc_gesture_swipe_get_velocity (PhocGestureSwipe *self,
                                 gdouble          *velocity_x,
                                 gdouble          *velocity_y)
{
  gdouble vel_x, vel_y;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);

  if (!phoc_gesture_is_recognized (PHOC_GESTURE (self)))
    return FALSE;

  _phoc_gesture_swipe_calculate_velocity (self, &vel_x, &vel_y);

  if (velocity_x)
    *velocity_x = vel_x;
  if (velocity_y)
    *velocity_y = vel_y;

  return TRUE;
}
