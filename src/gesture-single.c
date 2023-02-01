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

#define G_LOG_DOMAIN "phoc-gesture-single"

#include "phoc-config.h"

#include "gesture.h"
#include "gesture-single.h"
#include "phoc-marshalers.h"

#include "linux/input-event-codes.h"

enum {
  PROP_0,
  PROP_BUTTON,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PhocGestureSingle:
 *
 * A single touch (or mouse) gesture.
 */
typedef struct _PhocGestureSinglePrivate {
  guint               button;

  PhocEventSequence  *current_sequence;
  guint               current_button;
} PhocGestureSinglePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhocGestureSingle, phoc_gesture_single, PHOC_TYPE_GESTURE)


static void
phoc_gesture_single_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PhocGestureSingle *self = PHOC_GESTURE_SINGLE (object);
  PhocGestureSinglePrivate *priv = phoc_gesture_single_get_instance_private (self);

  switch (property_id) {
  case PROP_BUTTON:
    priv->button = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_gesture_single_get_property (GObject    *object,
		      guint       property_id,
		      GValue     *value,
		      GParamSpec *pspec)
{
  PhocGestureSingle *self = PHOC_GESTURE_SINGLE (object);
  PhocGestureSinglePrivate *priv = phoc_gesture_single_get_instance_private (self);

  switch (property_id) {
  case PROP_BUTTON:
    g_value_set_uint (value, priv->button);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static gboolean
phoc_gesture_single_handle_event (PhocGesture *gesture, const PhocEvent *event, double lx, double ly)
{
  PhocEventSequence *sequence = NULL;
  PhocGestureSinglePrivate *priv;
  guint button = 0;
  gboolean retval;

  priv = phoc_gesture_single_get_instance_private (PHOC_GESTURE_SINGLE (gesture));

  switch (event->type) {
  case PHOC_EVENT_TOUCH_BEGIN:
    sequence = GINT_TO_POINTER (event->touch_down.touch_id);
    button = BTN_LEFT;
    break;
  case PHOC_EVENT_TOUCH_END:
    sequence = GINT_TO_POINTER (event->touch_up.touch_id);
    button = BTN_LEFT;
    break;
  case PHOC_EVENT_TOUCH_UPDATE:
    sequence = GINT_TO_POINTER (event->touch_motion.touch_id);
    button = BTN_LEFT;
    break;
  case PHOC_EVENT_BUTTON_PRESS:
    button = event->button_press.button;
    break;
  case PHOC_EVENT_BUTTON_RELEASE:
    button = event->button_release.button;
    break;
  case PHOC_EVENT_MOTION_NOTIFY:
    if (!phoc_gesture_handles_sequence (gesture, sequence))
      return FALSE;
    /* FIXME: We don't have button information in these events from
       wlr so just reuse the existing once since otherwise the gesture
       would have been canceled */
    button = priv->current_button;
    if (priv->current_button == 0)
      return FALSE;
    break;
  case PHOC_EVENT_TOUCH_CANCEL:
  case PHOC_EVENT_GRAB_BROKEN:
  case PHOC_EVENT_TOUCHPAD_SWIPE_BEGIN:
  case PHOC_EVENT_TOUCHPAD_SWIPE_UPDATE:
  case PHOC_EVENT_TOUCHPAD_SWIPE_END:
    return PHOC_GESTURE_CLASS (phoc_gesture_single_parent_class)->handle_event (gesture, event, lx, ly);
    break;
  default:
    return FALSE;
  }

  if (button == 0 || (priv->button != 0 && priv->button != button) ||
      (priv->current_button != 0 && priv->current_button != button)) {
    if (phoc_gesture_is_active (gesture)) {
      phoc_gesture_reset (gesture);
    }
    return FALSE;
  }

  if (event->type == PHOC_EVENT_BUTTON_PRESS || event->type == PHOC_EVENT_TOUCH_BEGIN ||
      event->type == PHOC_EVENT_MOTION_NOTIFY || event->type == PHOC_EVENT_TOUCH_UPDATE) {
    if (!phoc_gesture_is_active (gesture))
        priv->current_sequence = sequence;

    priv->current_button = button;
  }

  retval = PHOC_GESTURE_CLASS (phoc_gesture_single_parent_class)->handle_event (gesture, event, lx, ly);

  if (sequence == priv->current_sequence &&
      (event->type == PHOC_EVENT_BUTTON_RELEASE || event->type == PHOC_EVENT_TOUCH_END))
    priv->current_button = 0;
  else if (sequence == priv->current_sequence &&
           !phoc_gesture_handles_sequence (PHOC_GESTURE (gesture), sequence)) {
    if (button == priv->current_button && event->type == PHOC_EVENT_BUTTON_PRESS)
      priv->current_button = 0;
    else if (sequence == priv->current_sequence && event->type == PHOC_EVENT_TOUCH_BEGIN)
      priv->current_sequence = NULL;
  }

  return retval;
}


static void
phoc_gesture_single_cancel (PhocGesture      *gesture,
                           PhocEventSequence *sequence)
{
  PhocGestureSinglePrivate *priv;

  priv = phoc_gesture_single_get_instance_private (PHOC_GESTURE_SINGLE (gesture));

  if (sequence == priv->current_sequence)
    priv->current_button = 0;
}


static void
phoc_gesture_single_class_init (PhocGestureSingleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhocGestureClass *gesture_class = PHOC_GESTURE_CLASS (klass);

  object_class->get_property = phoc_gesture_single_get_property;
  object_class->set_property = phoc_gesture_single_set_property;

  gesture_class->handle_event = phoc_gesture_single_handle_event;
  gesture_class->cancel = phoc_gesture_single_cancel;

  /**
   * PhocGestureSingle:button:
   *
   * Mouse button number to listen to as input-event-code.
   */
  props[PROP_BUTTON] = g_param_spec_uint ("button", "", "",
                                          BTN_LEFT, BTN_TASK,
                                          BTN_LEFT,
                                          G_PARAM_READWRITE|G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_gesture_single_init (PhocGestureSingle *self)
{
  PhocGestureSinglePrivate *priv = phoc_gesture_single_get_instance_private (self);

  priv->button = BTN_LEFT;
}


PhocGestureSingle *
phoc_gesture_single_new (void)
{
  return PHOC_GESTURE_SINGLE (g_object_new (PHOC_TYPE_GESTURE_SINGLE, NULL));
}

/**
 * phoc_gesture_single_get_current_sequence:
 * @self: a `PhocGestureSingle`
 *
 * Returns the event sequence currently interacting with @self.
 *
 * This is only meaningful if [method@Phoc.Gesture.is_active]
 * returns %TRUE.
 *
 * Returns: (nullable): the current sequence
 */
PhocEventSequence *
phoc_gesture_single_get_current_sequence (PhocGestureSingle *self)
{
  PhocGestureSinglePrivate *priv;

  g_return_val_if_fail (PHOC_IS_GESTURE_SINGLE (self), NULL);

  priv = phoc_gesture_single_get_instance_private (self);

  return priv->current_sequence;
}
