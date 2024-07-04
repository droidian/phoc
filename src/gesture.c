/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on gtk's gtkgesture.c which is
 * Copyright (C) 2012, One Laptop Per Child.
 * Copyright (C) 2014, Red Hat, Inc.
 * Author(s): Carlos Garnacho <carlosg@gnome.org>
 */

#define G_LOG_DOMAIN "phoc-gesture"

#include "phoc-config.h"

#include "gesture.h"
#include "event.h"
#include "phoc-enums.h"
#include "phoc-marshalers.h"

enum {
  PROP_0,
  PROP_N_POINTS,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  BEGIN,
  END,
  UPDATE,
  CANCEL,
  SEQUENCE_STATE_CHANGED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];
typedef struct _PointData PointData;


struct _PointData {
  PhocEvent *event;

  double     lx;
  double     ly;

  /* Acummulators for touchpad events */
  double     accum_dx;
  double     accum_dy;

  guint      press_handled : 1;
  guint      state : 2;
};

/**
 * PhocGesture:
 *
 * `PhocGesture` helps to detect and track ongoing gestures.
 */
typedef struct _PhocGesturePrivate {
  GHashTable        *points;

  PhocEventSequence *last_sequence;
  PhocInputDevice   *device;
  GList             *group_link;
  guint              n_points;
  guint              recognized : 1;
  guint              touchpad : 1;
} PhocGesturePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (PhocGesture, phoc_gesture, G_TYPE_OBJECT)

static void
phoc_gesture_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  PhocGesturePrivate *priv = phoc_gesture_get_instance_private (PHOC_GESTURE (object));

  switch (prop_id) {
  case PROP_N_POINTS:
    g_value_set_uint (value, priv->n_points);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
phoc_gesture_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  PhocGesturePrivate *priv = phoc_gesture_get_instance_private (PHOC_GESTURE (object));

  switch (prop_id) {
  case PROP_N_POINTS:
    priv->n_points = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}


static GList *
phoc_gesture_get_group_link (PhocGesture *self)
{
  PhocGesturePrivate *priv;

  priv = phoc_gesture_get_instance_private (self);

  return priv->group_link;
}


static void
phoc_gesture_finalize (GObject *object)
{
  PhocGesture *self = PHOC_GESTURE (object);
  PhocGesturePrivate *priv = phoc_gesture_get_instance_private (self);

  phoc_gesture_ungroup (self);
  g_clear_pointer (&priv->points, g_hash_table_destroy);
  g_clear_pointer (&priv->group_link, g_list_free);

  G_OBJECT_CLASS (phoc_gesture_parent_class)->finalize (object);

  G_OBJECT_CLASS (phoc_gesture_parent_class)->finalize (object);
}


static guint
phoc_gesture_get_n_touchpad_points (PhocGesture *self,
                                    gboolean     only_active)
{
  PhocGesturePrivate *priv;
  PointData *data;

  priv = phoc_gesture_get_instance_private (self);

  if (!priv->touchpad)
    return 0;

  data = g_hash_table_lookup (priv->points, NULL);

  if (!data)
    return 0;

  if (only_active &&
      (data->state == PHOC_EVENT_SEQUENCE_DENIED ||
       data->event->type == PHOC_EVENT_TOUCHPAD_SWIPE_END ||
       data->event->type == PHOC_EVENT_TOUCHPAD_PINCH_END))
    return 0;

  switch (data->event->type) {
  case PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN:
    return data->event->touchpad_swipe_begin.fingers;
  case PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE:
    return data->event->touchpad_swipe_begin.fingers;
  case PHOC_EVENT_TOUCHPAD_PINCH_BEGIN:
    return data->event->touchpad_pinch_begin.fingers;
  case PHOC_EVENT_TOUCHPAD_PINCH_UPDATE:
    return data->event->touchpad_pinch_begin.fingers;
  default:
    return 0;
  }
}


static PhocEventSequenceState
phoc_gesture_get_group_state (PhocGesture       *self,
                              PhocEventSequence *sequence)
{
  PhocEventSequenceState state = PHOC_EVENT_SEQUENCE_NONE;
  GList *group_elem;

  group_elem = g_list_first (phoc_gesture_get_group_link (self));

  for (; group_elem; group_elem = group_elem->next) {
    if (group_elem->data == self)
      continue;
    if (!phoc_gesture_handles_sequence (group_elem->data, sequence))
      continue;

    state = phoc_gesture_get_sequence_state (group_elem->data, sequence);
    break;
  }

  return state;
}


static guint
phoc_gesture_get_n_touch_points (PhocGesture *self,
                                 gboolean     only_active)
{
  PhocGesturePrivate *priv;
  GHashTableIter iter;
  guint n_points = 0;
  PointData *data;

  priv = phoc_gesture_get_instance_private (self);
  g_hash_table_iter_init (&iter, priv->points);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &data)) {
    if (only_active &&
        (data->state == PHOC_EVENT_SEQUENCE_DENIED ||
         data->event->type == PHOC_EVENT_TOUCH_END ||
         data->event->type == PHOC_EVENT_BUTTON_RELEASE))
      continue;

    n_points++;
  }

  return n_points;
}


static guint
phoc_gesture_get_n_physical_points (PhocGesture *self,
                                    gboolean     only_active)
{
  PhocGesturePrivate *priv;

  priv = phoc_gesture_get_instance_private (self);

  if (priv->touchpad)
    return phoc_gesture_get_n_touchpad_points (self, only_active);
  else
    return phoc_gesture_get_n_touch_points (self, only_active);
}

static gboolean
phoc_gesture_check_impl (PhocGesture *self)
{
  PhocGesturePrivate *priv;
  guint n_points;

  priv = phoc_gesture_get_instance_private (self);
  n_points = phoc_gesture_get_n_physical_points (self, TRUE);

  return n_points == priv->n_points;
}

static void
phoc_gesture_set_recognized (PhocGesture       *self,
                             gboolean           recognized,
                             PhocEventSequence *sequence)
{
  PhocGesturePrivate *priv;

  priv = phoc_gesture_get_instance_private (self);

  if (priv->recognized == recognized)
    return;

  priv->recognized = recognized;

  if (recognized)
    g_signal_emit (self, signals[BEGIN], 0, sequence);
  else
    g_signal_emit (self, signals[END], 0, sequence);
}

static gboolean
phoc_gesture_do_check (PhocGesture *self)
{
  PhocGestureClass *gesture_class;
  gboolean retval = FALSE;

  gesture_class = PHOC_GESTURE_GET_CLASS (self);

  if (!gesture_class->check)
    return retval;

  return gesture_class->check (self);
}

static gboolean
phoc_gesture_has_matching_touchpoints (PhocGesture *self)
{
  PhocGesturePrivate *priv = phoc_gesture_get_instance_private (self);
  guint active_n_points, current_n_points;

  current_n_points = phoc_gesture_get_n_physical_points (self, FALSE);
  active_n_points = phoc_gesture_get_n_physical_points (self, TRUE);

  return (active_n_points == priv->n_points &&
          current_n_points == priv->n_points);
}


static gboolean
phoc_gesture_check_recognized (PhocGesture       *self,
                               PhocEventSequence *sequence)
{
  PhocGesturePrivate *priv = phoc_gesture_get_instance_private (self);
  gboolean has_matching_touchpoints;

  has_matching_touchpoints = phoc_gesture_has_matching_touchpoints (self);

  if (priv->recognized && !has_matching_touchpoints)
    phoc_gesture_set_recognized (self, FALSE, sequence);
  else if (!priv->recognized && has_matching_touchpoints &&
           phoc_gesture_do_check (self))
    phoc_gesture_set_recognized (self, TRUE, sequence);

  return priv->recognized;
}


static void
update_touchpad_deltas (PointData *data)
{
  PhocEvent *event = data->event;
  PhocTouchpadGesturePhase phase;
  double dx;
  double dy;

  if (!event)
    return;

  if (!phoc_event_is_touchpad_gesture (event))
    return;

  phase = phoc_event_get_touchpad_gesture_phase (event);
  if (phase == PHOC_TOUCHPAD_GESTURE_PHASE_BEGIN) {
    data->accum_dx = data->accum_dy = 0;
  } else if (phase == PHOC_TOUCHPAD_GESTURE_PHASE_UPDATE) {
    phoc_event_get_touchpad_gesture_deltas (event, &dx, &dy);
    data->accum_dx += dx;
    data->accum_dy += dy;
  }
}


static gboolean
phoc_gesture_update_point (PhocGesture     *self,
                           const PhocEvent *event,
                           double           lx,
                           double           ly,
                           gboolean         add)
{
  PhocEventSequence *sequence;
  PhocGesturePrivate *priv;
  PhocInputDevice *device;
  gboolean existed, touchpad;
  PointData *data;

  device = phoc_event_get_device (event);
  touchpad = phoc_event_is_touchpad_gesture (event);

  if (!device)
    return FALSE;

  priv = phoc_gesture_get_instance_private (self);

  if (add) {

    /* If the event happens with the wrong device, ignore */
    if (priv->device && priv->device != device)
      return FALSE;

    /* Make touchpad and touchscreen gestures mutually exclusive */
    if (touchpad && g_hash_table_size (priv->points) > 0)
      return FALSE;
    else if (!touchpad && priv->touchpad)
      return FALSE;

  } else if (!priv->device) {
    return FALSE;
  }

  sequence = phoc_event_get_event_sequence (event);
  existed = g_hash_table_lookup_extended (priv->points, sequence,
                                          NULL, (gpointer *) &data);
  if (!existed) {
    if (!add)
      return FALSE;

    if (g_hash_table_size (priv->points) == 0) {
      priv->device = device;
      priv->touchpad = touchpad;
    }

    data = g_new0 (PointData, 1);
    g_hash_table_insert (priv->points, sequence, data);
  }

  if (data->event)
    phoc_event_free (data->event);

  data->event = phoc_event_copy (event);
  update_touchpad_deltas (data);
  data->lx = lx + data->accum_dx;
  data->ly = ly + data->accum_dy;

  if (!existed) {
    PhocEventSequenceState state;

    /* Deny the sequence right away if the expected
     * number of points is exceeded, so this sequence
     * can be tracked with phoc_gesture_handles_sequence().
     *
     * Otherwise, make the sequence inherit the same state
     * from other gestures in the same group.
     */
    if (phoc_gesture_get_n_physical_points (self, FALSE) > priv->n_points)
      state = PHOC_EVENT_SEQUENCE_DENIED;
    else
      state = phoc_gesture_get_group_state (self, sequence);

    phoc_gesture_set_sequence_state (self, sequence, state);
  }

  return TRUE;
}

static void
phoc_gesture_check_empty (PhocGesture *self)
{
  PhocGesturePrivate *priv;

  priv = phoc_gesture_get_instance_private (self);

  if (g_hash_table_size (priv->points) == 0) {
    // priv->window = NULL;
    priv->device = NULL;
    priv->touchpad = FALSE;
  }
}

static void
phoc_gesture_remove_point (PhocGesture     *self,
                           const PhocEvent *event)
{
  PhocEventSequence *sequence;
  PhocGesturePrivate *priv;
  PhocInputDevice *device;

  sequence = phoc_event_get_event_sequence (event);
  device = phoc_event_get_device (event);
  priv = phoc_gesture_get_instance_private (self);

  if (priv->device != device)
    return;

  g_hash_table_remove (priv->points, sequence);
  phoc_gesture_check_empty (self);
}


static void
phoc_gesture_cancel_all (PhocGesture *self)
{
  PhocEventSequence *sequence;
  PhocGesturePrivate *priv;
  GHashTableIter iter;

  priv = phoc_gesture_get_instance_private (self);
  g_hash_table_iter_init (&iter, priv->points);

  while (g_hash_table_iter_next (&iter, (gpointer*) &sequence, NULL)) {
    g_signal_emit (self, signals[CANCEL], 0, sequence);
    g_hash_table_iter_remove (&iter);
    phoc_gesture_check_recognized (self, sequence);
  }

  phoc_gesture_check_empty (self);
}


static gboolean
phoc_gesture_cancel_sequence (PhocGesture       *self,
                              PhocEventSequence *sequence)
{
  PhocGesturePrivate *priv;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);

  priv = phoc_gesture_get_instance_private (self);
  data = g_hash_table_lookup (priv->points, sequence);

  if (!data)
    return FALSE;

  g_signal_emit (self, signals[CANCEL], 0, sequence);
  phoc_gesture_remove_point (self, data->event);
  phoc_gesture_check_recognized (self, sequence);

  return TRUE;
}


static gboolean
phoc_gesture_filter_event_impl (PhocGesture     *self,
                                const PhocEvent *event)
{
  return FALSE;
}


static gboolean
phoc_gesture_handle_event_impl (PhocGesture *self, const PhocEvent *event, double lx, double ly)
{
  PhocEventSequence *sequence;
  PhocGesturePrivate *priv;
  PhocInputDevice *source_device;
  gboolean was_recognized;

  source_device = phoc_event_get_device (event);

  if (!source_device)
    return FALSE;

  priv = phoc_gesture_get_instance_private (self);
  sequence = phoc_event_get_event_sequence (event);
  was_recognized = phoc_gesture_is_recognized (self);

  if (phoc_gesture_get_sequence_state (self, sequence) != PHOC_EVENT_SEQUENCE_DENIED)
    priv->last_sequence = sequence;

  if (event->type == PHOC_EVENT_BUTTON_PRESS ||
      event->type == PHOC_EVENT_TOUCH_BEGIN ||
      event->type == PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN ||
      event->type == PHOC_EVENT_TOUCHPAD_PINCH_BEGIN) {
    if (phoc_gesture_update_point (self, event, lx, ly, TRUE)) {
      gboolean triggered_recognition;

      triggered_recognition =
        !was_recognized && phoc_gesture_has_matching_touchpoints (self);

      if (phoc_gesture_check_recognized (self, sequence)) {
        PointData *data;

        data = g_hash_table_lookup (priv->points, sequence);

        /* If the sequence was claimed early, the press event will be consumed */
        if (phoc_gesture_get_sequence_state (self, sequence) == PHOC_EVENT_SEQUENCE_CLAIMED)
          data->press_handled = TRUE;
      } else if (triggered_recognition && g_hash_table_size (priv->points) == 0) {
        /* Recognition was triggered, but the gesture reset during
         * ::begin emission. Still, recognition was strictly triggered,
         * so the event should be consumed.
         */
        return TRUE;
      }
    }
  } else if (event->type == PHOC_EVENT_BUTTON_RELEASE ||
             event->type == PHOC_EVENT_TOUCH_END ||
             event->type == PHOC_EVENT_TOUCHPAD_SWIPE_END ||
             event->type == PHOC_EVENT_TOUCHPAD_PINCH_END) {
    if (phoc_gesture_update_point (self, event, lx, ly, FALSE)) {
      if (was_recognized && phoc_gesture_check_recognized (self, sequence))
        g_signal_emit (self, signals[UPDATE], 0, sequence);
      phoc_gesture_remove_point (self, event);
    }
  } else if (event->type == PHOC_EVENT_MOTION_NOTIFY ||
             event->type == PHOC_EVENT_TOUCH_UPDATE ||
             event->type == PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE ||
             event->type == PHOC_EVENT_TOUCHPAD_PINCH_UPDATE) {
    /* FIXME: can't check button state */
    if (phoc_gesture_update_point (self, event, lx, ly, FALSE) &&
        phoc_gesture_check_recognized (self, sequence)) {
      g_signal_emit (self, signals[UPDATE], 0, sequence);
    }
  } else if (event->type == PHOC_EVENT_TOUCH_CANCEL) {
    if (!priv->touchpad)
      phoc_gesture_cancel_sequence (self, sequence);
  } else if ((event->type == PHOC_EVENT_TOUCHPAD_SWIPE_END
              && event->touchpad_swipe_end.cancelled) ||
             (event->type == PHOC_EVENT_TOUCHPAD_PINCH_END
              && event->touchpad_pinch_end.cancelled)) {
    if (priv->touchpad)
      phoc_gesture_cancel_sequence (self, sequence);
  } else {
    /* Unhandled event */
    return FALSE;
  }

  if (phoc_gesture_get_sequence_state (self, sequence) != PHOC_EVENT_SEQUENCE_CLAIMED)
    return FALSE;

  return priv->recognized;
}

static void
phoc_gesture_reset_impl (PhocGesture* self)
{
  phoc_gesture_cancel_all (self);
}

static void
phoc_gesture_class_init (PhocGestureClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_gesture_get_property;
  object_class->set_property = phoc_gesture_set_property;
  object_class->finalize = phoc_gesture_finalize;

  klass->handle_event = phoc_gesture_handle_event_impl;
  klass->filter_event = phoc_gesture_filter_event_impl;
  klass->check = phoc_gesture_check_impl;
  klass->reset = phoc_gesture_reset_impl;

  /**
   * PhocGesture:n-points:
   *
   * The number of touch points that trigger
   * recognition on this gesture.
   */
  props[PROP_N_POINTS] = g_param_spec_uint ("n-points", "", "",
                                            1, G_MAXUINT, 1,
                                            G_PARAM_READWRITE |
                                            G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhocGesture::begin:
   * @gesture: the object which received the signal
   * @sequence: (nullable): the `PhocEvent` that made the gesture
   *   to be recognized
   *
   * Emitted when the gesture is recognized.
   */
  signals[BEGIN] =
    g_signal_new ("begin",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureClass, begin),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, PHOC_TYPE_EVENT_SEQUENCE);

  /**
   * PhocGesture::end:
   * @gesture: the object which received the signal
   * @sequence: (nullable): the `PhocEvent` that made gesture
   *   recognition to finish
   *
   * Emitted when @gesture stopped recognizing the event
   * sequences as something to be handled.
   */
  signals[END] =
    g_signal_new ("end",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureClass, end),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, PHOC_TYPE_EVENT_SEQUENCE);

  /**
   * PhocGesture::update:
   * @gesture: the object which received the signal
   * @sequence: (nullable): the `PhocEvent` that was updated
   *
   * Emitted whenever an event is handled while the gesture is recognized.
   *
   * @sequence is guaranteed to pertain to the set of active touches.
   */
  signals[UPDATE] =
    g_signal_new ("update",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureClass, update),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, PHOC_TYPE_EVENT_SEQUENCE);

  /**
   * PhocGesture::cancel:
   * @gesture: the object which received the signal
   * @sequence: (nullable): the `PhocEvent` that was cancelled
   *
   * Emitted whenever a sequence is cancelled.
   *
   * @gesture must forget everything about @sequence as in
   * response to this signal.
   */
  signals[CANCEL] =
    g_signal_new ("cancel",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureClass, cancel),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, PHOC_TYPE_EVENT_SEQUENCE);

  /**
   * PhocGesture::sequence-state-changed:
   * @gesture: the object which received the signal
   * @sequence: (nullable): the #PhocEventSequence that was cancelled
   * @state: the new sequence state
   *
   * This signal is emitted whenever a sequence state changes. See
   * phoc_gesture_set_sequence_state() to know more about the expectable
   * sequence lifetimes.
   */
  signals[SEQUENCE_STATE_CHANGED] =
    g_signal_new ("sequence-state-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhocGestureClass, sequence_state_changed),
                  NULL, NULL,
                  _phoc_marshal_VOID__BOXED_ENUM,
                  G_TYPE_NONE, 2, PHOC_TYPE_EVENT_SEQUENCE,
                  PHOC_TYPE_EVENT_SEQUENCE_STATE);
  g_signal_set_va_marshaller (signals[SEQUENCE_STATE_CHANGED],
                              G_TYPE_FROM_CLASS (klass),
                              _phoc_marshal_VOID__BOXED_ENUMv);
}


static void
free_point_data (gpointer data)
{
  PointData *point = data;

  g_clear_pointer (&point->event, phoc_event_free);
  g_free (point);
}


static void
phoc_gesture_init (PhocGesture *self)
{
  PhocGesturePrivate *priv = phoc_gesture_get_instance_private (self);

  priv->n_points = 1;
  priv->points = g_hash_table_new_full (NULL, NULL, NULL,
                                        (GDestroyNotify) free_point_data);
  priv->group_link = g_list_prepend (NULL, self);
}

/**
 * phoc_gesture_handle_event
 * @self: The gesture
 * @event: the event to handle
 * @lx: event position in layout coordinates, 0 if unavailable
 * @ly: event position in layout coordinates, 0 if unavailable
 *
 * Handle the given event.
 * Returns: Returns: %TRUE if the event was useful for this gesture.
 */
gboolean
phoc_gesture_handle_event (PhocGesture *self, const PhocEvent *event, double lx, double ly)
{
  PhocGestureClass *gesture_class;
  gboolean retval = FALSE;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  gesture_class = PHOC_GESTURE_GET_CLASS (self);

  if (gesture_class->filter_event (self, event))
    return retval;

  if (gesture_class->handle_event) {
    g_object_ref (self);
    retval = gesture_class->handle_event (self, event, lx, ly);
    g_object_unref (self);
  }

  return retval;
}

/**
 * phoc_gesture_reset:
 * @self: a #PhocGesture
 *
 * Resets the gesture to a clean state.
 **/
void
phoc_gesture_reset (PhocGesture *self)
{
  PhocGestureClass *gesture_class;

  g_return_if_fail (PHOC_IS_GESTURE (self));

  gesture_class = PHOC_GESTURE_GET_CLASS (self);

  if (gesture_class->reset)
    gesture_class->reset (self);
}


/**
 * phoc_gesture_get_sequence_state:
 * @self: a #PhocGesture
 * @sequence: a #PhocEventSequence
 *
 * Returns the @sequence state, as seen by @self.
 *
 * Returns: The sequence state in @self
 **/
PhocEventSequenceState
phoc_gesture_get_sequence_state (PhocGesture       *self,
                                 PhocEventSequence *sequence)
{
  PhocGesturePrivate *priv;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), PHOC_EVENT_SEQUENCE_NONE);

  priv = phoc_gesture_get_instance_private (self);
  data = g_hash_table_lookup (priv->points, self);

  if (!data)
    return PHOC_EVENT_SEQUENCE_NONE;

  return data->state;
}

/**
 * phoc_gesture_set_sequence_state:
 * @self: a #PhocGesture
 * @sequence: a #PhocEventSequence
 * @state: the sequence state
 *
 * Sets the state of @sequence in @self. Sequences start
 * in state #PHOC_EVENT_SEQUENCE_NONE, and whenever they change
 * state, they can never go back to that state. Likewise,
 * sequences in state #PHOC_EVENT_SEQUENCE_DENIED cannot turn
 * back to a not denied state. With these rules, the lifetime
 * of an event sequence is constrained to the next four:
 *
 * * None
 * * None → Denied
 * * None → Claimed
 * * None → Claimed → Denied
 *
 * Note: Due to event handling ordering, it may be unsafe to
 * set the state on another gesture within a #PhocGesture::begin
 * signal handler, as the callback might be executed before
 * the other gesture knows about the sequence. A safe way to
 * perform this could be:
 *
 * |[
 * static void
 * first_gesture_begin_cb (PhocGesture       *first_gesture,
 *                         PhocEventSequence *sequence,
 *                         gpointer          user_data)
 * {
 *   phoc_gesture_set_sequence_state (first_gesture, sequence, PHOC_EVENT_SEQUENCE_CLAIMED);
 *   phoc_gesture_set_sequence_state (second_gesture, sequence, PHOC_EVENT_SEQUENCE_DENIED);
 * }
 *
 * static void
 * second_gesture_begin_cb (PhocGesture       *second_gesture,
 *                          PhocEventSequence *sequence,
 *                          gpointer          user_data)
 * {
 *   if (phoc_gesture_get_sequence_state (first_gesture, sequence) == PHOC_EVENT_SEQUENCE_CLAIMED)
 *     phoc_gesture_set_sequence_state (second_gesture, sequence, PHOC_EVENT_SEQUENCE_DENIED);
 * }
 * ]|
 *
 * If both gestures are in the same group, just set the state on
 * the gesture emitting the event, the sequence will already
 * be initialized to the group's global state when the second
 * gesture processes the event.
 *
 * Returns: %TRUE if @sequence is handled by @self,
 *          and the state is changed successfully
 */
gboolean
phoc_gesture_set_sequence_state (PhocGesture           *self,
                                 PhocEventSequence     *sequence,
                                 PhocEventSequenceState state)
{
  PhocGesturePrivate *priv;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);
  g_return_val_if_fail (state >= PHOC_EVENT_SEQUENCE_NONE &&
                        state <= PHOC_EVENT_SEQUENCE_DENIED, FALSE);

  priv = phoc_gesture_get_instance_private (self);
  data = g_hash_table_lookup (priv->points, sequence);

  if (!data)
    return FALSE;

  if (data->state == state)
    return FALSE;

  /* denied sequences remain denied */
  if (data->state == PHOC_EVENT_SEQUENCE_DENIED)
    return FALSE;

  /* Sequences can't go from claimed/denied to none */
  if (state == PHOC_EVENT_SEQUENCE_NONE &&
      data->state != PHOC_EVENT_SEQUENCE_NONE)
    return FALSE;

  data->state = state;
  g_signal_emit (self, signals[SEQUENCE_STATE_CHANGED], 0,
                 sequence, state);

  if (state == PHOC_EVENT_SEQUENCE_DENIED)
    phoc_gesture_check_recognized (self, sequence);

  return TRUE;
}

/**
 * phoch_gesture_set_state:
 * @self: a `PhocGesture`
 * @state: the sequence state
 *
 * Sets the state of all sequences that @self is currently
 * interacting with.
 *
 * See [method@Phoc.Gesture.set_sequence_state] for more details
 * on sequence states.
 *
 * Returns: %TRUE if the state of at least one sequence
 *   was changed successfully
 */
gboolean
phoc_gesture_set_state (PhocGesture            *self,
                        PhocEventSequenceState  state)
{
  gboolean handled = FALSE;
  PhocGesturePrivate *priv;
  GList *sequences, *l;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);
  g_return_val_if_fail (state >= PHOC_EVENT_SEQUENCE_NONE &&
                        state <= PHOC_EVENT_SEQUENCE_DENIED, FALSE);

  priv = phoc_gesture_get_instance_private (self);
  sequences = g_hash_table_get_keys (priv->points);

  for (l = sequences; l; l = l->next)
    handled |= phoc_gesture_set_sequence_state (self, l->data, state);

  g_list_free (sequences);

  return handled;
}


/**
 * phoc_gesture_get_sequences:
 * @self: a #PhocGesture
 *
 * Returns the list of [type@EventSequence]s currently being interpreted
 * by [type@Gesture].
 *
 * Returns: (transfer container) (element-type PhocEventSequence): A list
 *          of #PhocEventSequence s, the list elements are owned by Phoc
 *          and must not be freed or modified, the list itself must be deleted
 *          through g_list_free()
 **/
GList *
phoc_gesture_get_sequences (PhocGesture *self)
{
  PhocEventSequence *sequence;
  PhocGesturePrivate *priv;
  GList *sequences = NULL;
  GHashTableIter iter;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), NULL);

  priv = phoc_gesture_get_instance_private (self);
  g_hash_table_iter_init (&iter, priv->points);

  while (g_hash_table_iter_next (&iter, (gpointer *) &sequence, (gpointer *) &data)) {
    if (data->state == PHOC_EVENT_SEQUENCE_DENIED)
      continue;
    if (data->event->type == PHOC_EVENT_TOUCH_END ||
        data->event->type == PHOC_EVENT_BUTTON_RELEASE)
      continue;

    sequences = g_list_prepend (sequences, sequence);
  }

  return sequences;
}

/**
 * phoc_gesture_get_last_updated_sequence:
 * @self: a #PhocGesture
 *
 * Returns the #PhocEventSequence that was last updated on @self.
 *
 * Returns: (transfer none) (nullable): The last updated sequence
 **/
PhocEventSequence *
phoc_gesture_get_last_updated_sequence (PhocGesture *self)
{
  PhocGesturePrivate *priv;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), NULL);

  priv = phoc_gesture_get_instance_private (self);

  return priv->last_sequence;
}

/**
 * phoc_gesture_get_last_event:
 * @self: a #PhocGesture
 * @sequence: (nullable): a #PhocEventSequence
 *
 * Returns the last event that was processed for @sequence.
 *
 * Note that the returned pointer is only valid as long as the @sequence
 * is still interpreted by the @self. If in doubt, you should make
 * a copy of the event.
 *
 * Returns: (transfer none) (nullable): The last event from @sequence
 **/
const PhocEvent *
phoc_gesture_get_last_event (PhocGesture *self, PhocEventSequence *sequence)
{
  PhocGesturePrivate *priv;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), NULL);

  priv = phoc_gesture_get_instance_private (self);
  data = g_hash_table_lookup (priv->points, sequence);

  if (!data)
    return NULL;

  return data->event;
}


/**
 * phoc_gesture_get_point:
 * @self: a `PhocGesture`
 * @sequence: (nullable): a `PhocEventSequence`, or %NULL for pointer events
 * @lx: (out) (optional): return location for X axis of the sequence coordinates
 * @ly: (out) (optional): return location for Y axis of the sequence coordinates
 *
 * If @sequence is currently being interpreted by @self,
 * returns %TRUE and fills in @x and @y with the last coordinates
 * stored for that event sequence.
 *
 * The coordinates are always layout coordinates
 *
 * Returns: %TRUE if @sequence is currently interpreted
 */
gboolean
phoc_gesture_get_point (PhocGesture       *self,
                        PhocEventSequence *sequence,
                        double            *lx,
                        double            *ly)
{
  PhocGesturePrivate *priv;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);

  priv = phoc_gesture_get_instance_private (self);

  if (!g_hash_table_lookup_extended (priv->points, sequence,
                                     NULL, (gpointer *) &data))
    return FALSE;

  if (lx)
    *lx = data->lx;
  if (ly)
    *ly = data->ly;

  return TRUE;
}

gboolean
phoc_gesture_get_last_update_time (PhocGesture       *self,
                                   PhocEventSequence *sequence,
                                   guint32           *evtime)
{
  PhocGesturePrivate *priv;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);

  priv = phoc_gesture_get_instance_private (self);

  if (!g_hash_table_lookup_extended (priv->points, sequence,
                                     NULL, (gpointer *) &data))
    return FALSE;

  if (evtime)
    *evtime = phoc_event_get_time (data->event);

  return TRUE;
}

/**
 * phoc_gesture_is_recognized:
 * @self: a #PhocGesture
 *
 * Returns %TRUE if the gesture is currently recognized.
 * A gesture is recognized if there are as many interacting
 * touch sequences as required by @self, and #PhocGesture::check
 * returned %TRUE for the sequences being currently interpreted.
 *
 * Returns: %TRUE if gesture is recognized
 **/
gboolean
phoc_gesture_is_recognized (PhocGesture *self)
{
  PhocGesturePrivate *priv;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);

  priv = phoc_gesture_get_instance_private (self);

  return priv->recognized;
}

/**
 * phoc_gesture_handles_sequence:
 * @self: a #PhocGesture
 * @sequence: (nullable): a #PhocEventSequence or %NULL
 *
 * Returns %TRUE if @self is currently handling events corresponding to
 * @sequence.
 *
 * Returns: %TRUE if @self is handling @sequence, %FALSE otherwise
 *
 * Since: 3.14
 **/
gboolean
phoc_gesture_handles_sequence (PhocGesture       *self,
                               PhocEventSequence *sequence)
{
  PhocGesturePrivate *priv;
  PointData *data;

  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);

  priv = phoc_gesture_get_instance_private (self);
  data = g_hash_table_lookup (priv->points, sequence);

  if (!data)
    return FALSE;

  if (data->state == PHOC_EVENT_SEQUENCE_DENIED)
    return FALSE;

  return TRUE;
}

/**
 * phoc_gesture_is_active:
 * @self: a #PhocGesture
 *
 * Returns %TRUE if the gesture is currently active.
 * A gesture is active meanwhile there are touch sequences
 * interacting with it.
 *
 * Returns: %TRUE if gesture is active
 *
 * Since: 3.14
 **/
gboolean
phoc_gesture_is_active (PhocGesture *self)
{
  g_return_val_if_fail (PHOC_IS_GESTURE (self), FALSE);

  return phoc_gesture_get_n_physical_points (self, TRUE) != 0;
}

/**
 * phoc_gesture_ungroup:
 * @self: a `PhocGesture`
 *
 * Separates @self into an isolated group.
 */
void
phoc_gesture_ungroup (PhocGesture *self)
{
  GList *link, *prev, *next;

  g_return_if_fail (PHOC_IS_GESTURE (self));

  link = phoc_gesture_get_group_link (self);
  prev = link->prev;
  next = link->next;

  /* Detach link from the group chain */
  if (prev)
    prev->next = next;
  if (next)
    next->prev = prev;

  link->next = link->prev = NULL;
}
