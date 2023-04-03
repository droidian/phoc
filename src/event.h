/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_pointer.h>

#include <glib-object.h>

#pragma once

G_BEGIN_DECLS

#define PHOC_TYPE_EVENT          (phoc_event_get_type ())
#define PHOC_TYPE_EVENT_SEQUENCE (phoc_event_sequence_get_type ())

/**
 * PhocEventType:
 * @PHOC_EVENT_NOTHING: No event
 * @PHOC_EVENT_TOUCH_BEGIN: A new touch event sequence has started
 * @PHOC_EVENT_TOUCH_UPDATE: A touch event sequence has been updated
 * @PHOC_EVENT_TOUCH_END: A touch event sequence has finished
 * @PHOC_EVENT_TOUCH_CANCEL: A touch event sequence has been canceled
 * @PHOC_EVENT_TOUCHPAD_PINCH: A pinch gesture event
 * @PHOC_EVENT_TOUCHPAD_SWIPE: A swipe gesture event
 *
 * Types of events.
 */
typedef enum
{
  PHOC_EVENT_NOTHING = 0,
  PHOC_EVENT_BUTTON_PRESS,
  PHOC_EVENT_BUTTON_RELEASE,
  PHOC_EVENT_MOTION_NOTIFY,
  PHOC_EVENT_TOUCH_BEGIN,
  PHOC_EVENT_TOUCH_UPDATE,
  PHOC_EVENT_TOUCH_END,
  PHOC_EVENT_TOUCH_CANCEL,
  PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN,
  PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE,
  PHOC_EVENT_TOUCHPAD_SWIPE_END,
  PHOC_EVENT_TOUCHPAD_PINCH_BEGIN,
  PHOC_EVENT_TOUCHPAD_PINCH_UPDATE,
  PHOC_EVENT_TOUCHPAD_PINCH_END,
/* TODO needed when leaving surface */
  PHOC_EVENT_GRAB_BROKEN,

  PHOC_EVENT_EVENT_LAST
} PhocEventType;


/**
 * PhocEventFlags:
 * @PHOC_EVENT_NONE: No flag set
 *
 * Flags for the #PhocEvent
 */
typedef enum
{
  PHOC_EVENT_NONE              = 0,
} PhocEventFlags;

/**
 * PhocTouchpadGesturePhase:
 * @PHOC_TOUCHPAD_GESTURE_PHASE_BEGIN: The gesture has begun.
 * @PHOC_TOUCHPAD_GESTURE_PHASE_UPDATE: The gesture has been updated.
 * @PHOC_TOUCHPAD_GESTURE_PHASE_END: The gesture was finished, changes
 *   should be permanently applied.
 * @PHOC_TOUCHPAD_GESTURE_PHASE_CANCEL: The gesture was cancelled, all
 *   changes should be undone.
 *
 * The phase of a touchpad gesture event. All gestures are guaranteed to
 * begin with an event of type %PHOC_TOUCHPAD_GESTURE_PHASE_BEGIN,
 * followed by a number of %PHOC_TOUCHPAD_GESTURE_PHASE_UPDATE (possibly 0).
 *
 * A finished gesture may have 2 possible outcomes, an event with phase
 * %PHOC_TOUCHPAD_GESTURE_PHASE_END will be emitted when the gesture is
 * considered successful, this should be used as the hint to perform any
 * permanent changes.

 * Cancelled gestures may be so for a variety of reasons, due to hardware,
 * or due to the gesture recognition layers hinting the gesture did not
 * finish resolutely (eg. a 3rd finger being added during a pinch gesture).
 * In these cases, the last event with report the phase
 * %PHOC_TOUCHPAD_GESTURE_PHASE_CANCEL, this should be used as a hint
 * to undo any visible/permanent changes that were done throughout the
 * progress of the gesture.
 */
typedef enum
{
  PHOC_TOUCHPAD_GESTURE_PHASE_BEGIN,
  PHOC_TOUCHPAD_GESTURE_PHASE_UPDATE,
  PHOC_TOUCHPAD_GESTURE_PHASE_END,
  PHOC_TOUCHPAD_GESTURE_PHASE_CANCEL
} PhocTouchpadGesturePhase;


/**
 * PhocEventSequenceState:
 * @PHOC_EVENT_SEQUENCE_NONE: The sequence is handled, but not grabbed.
 * @PHOC_EVENT_SEQUENCE_CLAIMED: The sequence is handled and grabbed.
 * @PHOC_EVENT_SEQUENCE_DENIED: The sequence is denied.
 *
 * Describes the state of a #PhocEventSequence in a #PhocGesture.
 */
typedef enum
{
  PHOC_EVENT_SEQUENCE_NONE,
  PHOC_EVENT_SEQUENCE_CLAIMED,
  PHOC_EVENT_SEQUENCE_DENIED
} PhocEventSequenceState;

typedef struct _PhocEventSequence            PhocEventSequence;
typedef struct _PhocAnyEvent                 PhocAnyEvent;
typedef struct _PhocEvent                    PhocEvent;

/**
 * PhocEvent:
 *
 * Input events.
 */
struct  _PhocEvent {
  PhocEventType          type;

  union {
    struct wlr_event_pointer_button             button_press;
    struct wlr_event_pointer_button             button_release;
    struct wlr_event_pointer_motion_absolute    motion_notify;
    struct wlr_event_touch_down                 touch_down;
    struct wlr_event_touch_up                   touch_up;
    struct wlr_event_touch_motion               touch_motion;
    struct wlr_event_touch_cancel               touch_cancel;
    struct wlr_event_pointer_swipe_begin        touchpad_swipe_begin;
    struct wlr_event_pointer_swipe_update       touchpad_swipe_update;
    struct wlr_event_pointer_swipe_end          touchpad_swipe_end;
    struct wlr_event_pointer_pinch_begin        touchpad_pinch_begin;
    struct wlr_event_pointer_pinch_update       touchpad_pinch_update;
    struct wlr_event_pointer_pinch_end          touchpad_pinch_end;
  };
};

GType                       phoc_event_get_type                      (void) G_GNUC_CONST;
GType                       phoc_event_sequence_get_type             (void) G_GNUC_CONST;
PhocEvent                  *phoc_event_new                           (PhocEventType    type,
                                                                      const gpointer   wlr_event,
                                                                      gsize            size);
PhocEvent                  *phoc_event_copy                          (const PhocEvent *event);
void                        phoc_event_free                          (PhocEvent       *event);
PhocEventSequence          *phoc_event_get_event_sequence            (const PhocEvent *event);
/* TODO: #include "input-device.h" tirggers header fallout again */
typedef struct _PhocInputDevice PhocInputDevice;
PhocInputDevice           *phoc_event_get_device                     (const PhocEvent *event);
PhocTouchpadGesturePhase   phoc_event_get_touchpad_gesture_phase     (const PhocEvent *event);
gboolean                   phoc_event_is_touchpad_gesture            (const PhocEvent *event);
void                       phoc_event_get_touchpad_gesture_deltas    (const PhocEvent *event,
                                                                      double          *dx,
                                                                      double          *dy);
guint                      phoc_event_get_touchpad_gesture_n_fingers (const PhocEvent *event);
guint32                    phoc_event_get_time                       (const PhocEvent *event);


G_DEFINE_AUTOPTR_CLEANUP_FUNC (PhocEvent, phoc_event_free)

G_END_DECLS
