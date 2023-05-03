/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-event"

#include "phoc-config.h"

#include "event.h"
#include "input-device.h"

/**
 * PhocEventSequence:
 *
 * `PhocEventSequence` is an opaque type representing a sequence of
 * related touch events.
 */

typedef struct _PhocEventPrivate {
  PhocEvent base;

  gpointer unused;
} PhocEventPrivate;


G_DEFINE_BOXED_TYPE (PhocEvent, phoc_event,
                     phoc_event_copy,
                     phoc_event_free);

static PhocEventSequence *
phoc_event_sequence_copy (PhocEventSequence *sequence)
{
  /* Nothing to copy here */
  return sequence;
}

static void
phoc_event_sequence_free (PhocEventSequence *sequence)
{
  /* Nothing to free here */
}

G_DEFINE_BOXED_TYPE (PhocEventSequence, phoc_event_sequence,
                     phoc_event_sequence_copy,
                     phoc_event_sequence_free);

/**
 * phoc_event_new:
 * @type: The type of event.
 *
 * Creates a new #PhocEvent of the specified type.
 *
 * Return value: (transfer full): A newly allocated #PhocEvent.
 */
PhocEvent *
phoc_event_new (PhocEventType type, gpointer wlr_event, gsize size)
{
  PhocEvent *new_event;
  PhocEventPrivate *priv;

  g_assert (wlr_event == NULL || size >= sizeof (struct wlr_event_touch_cancel));

  priv = g_new0 (PhocEventPrivate, 1);

  new_event = (PhocEvent *) priv;
  new_event->type = type;

  if (wlr_event)
    memcpy (&new_event->button_press, wlr_event, size);

  return new_event;
}

/**
 * phoc_event_copy:
 * @event: A #PhocEvent.
 *
 * Copies @event.
 *
 * Return value: (transfer full): A newly allocated #PhocEvent
 */
PhocEvent *
phoc_event_copy (const PhocEvent *event)
{
  PhocEvent *new_event;

  g_return_val_if_fail (event != NULL, NULL);

  new_event = phoc_event_new (PHOC_EVENT_NOTHING, NULL, 0);
  *new_event = *event;

  switch (event->type) {
    /* Nothing todo here atm */
  default:
      break;
    }

  return new_event;
}

/**
 * phoc_event_free:
 * @event: A #PhocEvent.
 *
 * Frees all resources used by @event.
 */
void
phoc_event_free (PhocEvent *event)
{
  if (G_LIKELY (event != NULL)) {
    switch (event->type) {
      /* Nothing to do here atm */
    default:
      break;
    }

    g_free ((PhocEventPrivate *) event);
  }
}

/**
 * phoc_event_get_event_sequence:
 * @event: a `PhocEvent`
 *
 * Related touch events are connected in a sequence. Other
 * events typically don't have event sequence information.
 *
 * Returns: (transfer none): the event sequence that the event belongs to
 */
PhocEventSequence *
phoc_event_get_event_sequence (const PhocEvent *event)
{
  if (!event)
    return NULL;

  if (event->type == PHOC_EVENT_TOUCH_BEGIN ||
      event->type == PHOC_EVENT_TOUCH_UPDATE ||
      event->type == PHOC_EVENT_TOUCH_END ||
      event->type == PHOC_EVENT_TOUCH_CANCEL) {
    /* All wlr_event_touch_* have the touch_id at the same position */
    return GUINT_TO_POINTER (event->touch_up.touch_id);
  }

  return NULL;
}

/**
 * phoc_event_get_device:
 * @event: a #PhocEvent
 *
 * Retrieves the input device the event came from.
 *
 * Returns: (nullable) (transfer none): The input devie
 */
PhocInputDevice *
phoc_event_get_device (const PhocEvent *event)
{
  struct wlr_input_device *wlr_device;

  switch (event->type) {
  case PHOC_EVENT_TOUCH_BEGIN:
  case PHOC_EVENT_TOUCH_UPDATE:
  case PHOC_EVENT_TOUCH_END:
  case PHOC_EVENT_TOUCH_CANCEL:
  case PHOC_EVENT_BUTTON_PRESS:
  case PHOC_EVENT_BUTTON_RELEASE:
  case PHOC_EVENT_MOTION_NOTIFY:
  case PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN:
  case PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE:
  case PHOC_EVENT_TOUCHPAD_SWIPE_END:
  case PHOC_EVENT_TOUCHPAD_PINCH_BEGIN:
  case PHOC_EVENT_TOUCHPAD_PINCH_UPDATE:
  case PHOC_EVENT_TOUCHPAD_PINCH_END:
    wlr_device = event->touch_down.device;
    break;
  default:
    g_return_val_if_reached (NULL);
  }

  g_return_val_if_fail (PHOC_IS_INPUT_DEVICE (wlr_device->data), NULL);

  return PHOC_INPUT_DEVICE (wlr_device->data);
}

/**
 * phoc_touchpad_event_get_gesture_phase:
 * @event: (type PhocEvent): a touchpad event
 *
 * Extracts the touchpad gesture phase from a touchpad event.
 *
 * Returns: the gesture phase of @event
 **/
PhocTouchpadGesturePhase
phoc_event_get_touchpad_gesture_phase (const PhocEvent *event)
{
  PhocTouchpadGesturePhase phase;

  switch (event->type) {
  case PHOC_EVENT_TOUCHPAD_PINCH_BEGIN:
  case PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN:
    phase = PHOC_TOUCHPAD_GESTURE_PHASE_BEGIN;
    break;
  case PHOC_EVENT_TOUCHPAD_PINCH_UPDATE:
  case PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE:
    phase = PHOC_TOUCHPAD_GESTURE_PHASE_UPDATE;
    break;
  case PHOC_EVENT_TOUCHPAD_PINCH_END:
    phase = event->touchpad_pinch_end.cancelled ?
      PHOC_TOUCHPAD_GESTURE_PHASE_CANCEL : PHOC_TOUCHPAD_GESTURE_PHASE_END;
    break;
  case PHOC_EVENT_TOUCHPAD_SWIPE_END:
    phase = event->touchpad_swipe_end.cancelled ?
      PHOC_TOUCHPAD_GESTURE_PHASE_CANCEL : PHOC_TOUCHPAD_GESTURE_PHASE_END;
    break;
  default:
    g_return_val_if_reached (0);
  }

  return phase;
}


gboolean
phoc_event_is_touchpad_gesture (const PhocEvent *event)
{
  switch (event->type) {
  case PHOC_EVENT_TOUCHPAD_PINCH_BEGIN:
  case PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN:
  case PHOC_EVENT_TOUCHPAD_PINCH_UPDATE:
  case PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE:
  case PHOC_EVENT_TOUCHPAD_PINCH_END:
  case PHOC_EVENT_TOUCHPAD_SWIPE_END:
    return TRUE;
  default:
    return FALSE;
  }
}


/**
 * phoc_event_get_touchpad_gesture_n_fingers:
 * @event: a touchpad event
 *
 * Extracts the number of fingers from a touchpad event.
 *
 * Returns: the number of fingers for @event
 **/
guint
phoc_event_get_touchpad_gesture_n_fingers (const PhocEvent *event)
{
  switch (event->type) {
  case PHOC_EVENT_TOUCHPAD_PINCH_BEGIN:
    return event->touchpad_pinch_begin.fingers;
  case PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN:
    return event->touchpad_swipe_begin.fingers;
  case PHOC_EVENT_TOUCHPAD_PINCH_UPDATE:
    return event->touchpad_pinch_update.fingers;
  case PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE:
    return event->touchpad_swipe_update.fingers;
  default:
    return 0;
  }
}


/**
 * phoc_event_get_touchpad_gesture_deltas:
 * @event: a touchpad event
 * @dx: (out): return location for x
 * @dy: (out): return location for y
 *
 * Extracts delta information from a touchpad event.
 */
void
phoc_event_get_touchpad_gesture_deltas (const PhocEvent *event,
                                        double    *dx,
                                        double    *dy)
{
  g_return_if_fail (phoc_event_is_touchpad_gesture (event));

  switch (event->type) {
  case PHOC_EVENT_TOUCHPAD_PINCH_BEGIN:
  case PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN:
  case PHOC_EVENT_TOUCHPAD_PINCH_END:
  case PHOC_EVENT_TOUCHPAD_SWIPE_END:
    *dx = *dy = 0.0;
    break;
  case PHOC_EVENT_TOUCHPAD_PINCH_UPDATE:
    *dx = event->touchpad_pinch_update.dx;
    *dy = event->touchpad_pinch_update.dy;
    break;
  case PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE:
    *dx = event->touchpad_swipe_update.dx;
    *dy = event->touchpad_swipe_update.dy;
    break;
  default:
    g_return_if_reached ();
  }
}


/**
 * phoc_event_get_time:
 * @event: a `PhocEvent`
 *
 * Returns the timestamp of @event in milliseconds
 *
 * Returns: timestamp field from @event
 */
guint32
phoc_event_get_time (const PhocEvent *event)
{
  /* All structs have time in the same position */
  return event->button_press.time_msec;
}
