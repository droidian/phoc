/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Inspired by AdwTimedAnimation which is
 * Copyright (C) 2021 Manuel Genovés <manuel.genoves@gmail.com>
 */
#define G_LOG_DOMAIN "phoc-timed-animation"

#include "phoc-config.h"

#include "animatable.h"
#include "property-easer.h"
#include "timed-animation.h"

enum {
  PROP_0,
  PROP_PROPERTY_EASER,
  PROP_ANIMATABLE,
  PROP_DURATION,
  PROP_DISPOSE_ON_DONE,
  PROP_STATE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  TICK,
  DONE,
  N_SIGNALS,
};
static guint signals[N_SIGNALS];

/**
 * PhocTimedAnimation:
 *
 * An animation that ends after the given period of time.
 *
 * [type@TimedAnimation] implements a timed animation using the given
 * [type@PropertyEaser] to animate properties of the [type@Animatable].
 */
struct _PhocTimedAnimation {
  GObject              parent;

  PhocAnimatable      *animatable;
  PhocPropertyEaser   *prop_easer;
  gint64               elapsed_ms;
  int                  duration;
  PhocAnimationState   state;
  guint                frame_callback_id;
  gboolean             dispose_on_done;
};

struct _PhocTimedAnimationClass {
  GObjectClass parent_class;
};

G_DEFINE_TYPE (PhocTimedAnimation, phoc_timed_animation, G_TYPE_OBJECT)


static void
set_animatable (PhocTimedAnimation *self, PhocAnimatable *animatable)
{
  g_assert (animatable == NULL || PHOC_IS_ANIMATABLE (animatable));

  if (self->animatable == animatable)
    return;

  g_set_weak_pointer (&self->animatable, animatable);
}


static void
update_properties (PhocTimedAnimation *self, guint t)
{
  double progress;

  g_assert (PHOC_IS_PROPERTY_EASER (self->prop_easer));

  if (self->duration == 0)
    return phoc_property_easer_set_progress (self->prop_easer, 0.0);

  progress = (double) t / self->duration;

  if (progress > 1.0)
    progress = 1.0;

  phoc_property_easer_set_progress (self->prop_easer, progress);
}


static void
set_property_easer (PhocTimedAnimation *self, PhocPropertyEaser *prop_easer)
{
  g_assert (PHOC_IS_TIMED_ANIMATION (self));
  g_assert (prop_easer == NULL || PHOC_IS_PROPERTY_EASER (prop_easer));

  if (self->prop_easer == prop_easer)
    return;

  g_set_object (&self->prop_easer, prop_easer);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PROPERTY_EASER]);
}


static void
set_dispose_on_done (PhocTimedAnimation *self, gboolean dispose_on_done)
{
  if (self->dispose_on_done == dispose_on_done)
    return;

  /* Take a ref so the code instantiating the object doesn't care about the life cycle */
  g_object_ref (self);

  self->dispose_on_done = dispose_on_done;
}


static void
stop_animation (PhocTimedAnimation *self)
{
  if (self->frame_callback_id) {
    phoc_animatable_remove_frame_callback (self->animatable, self->frame_callback_id);
    self->frame_callback_id = 0;
  }
}


static gboolean
on_frame_callback (PhocAnimatable *animatable,
                   guint64         last_frame,
                   gpointer        user_data)
{
  PhocTimedAnimation *self = PHOC_TIMED_ANIMATION (user_data);
  guint64 now = g_get_monotonic_time ();
  guint t = self->elapsed_ms + ((now - last_frame) / 1000);

  g_debug ("t: %d/%d", t, self->duration);
  if (self->elapsed_ms > self->duration) {
    self->frame_callback_id = 0;
    phoc_timed_animation_skip (self);
    return G_SOURCE_REMOVE;
  }

  update_properties (self, t);
  /* TODO: better emit changed progress? */
  g_signal_emit (self, signals[TICK], 0);

  self->elapsed_ms = t;
  return G_SOURCE_CONTINUE;
}


static void
play (PhocTimedAnimation *self)
{
  g_assert (PHOC_IS_ANIMATABLE (self->animatable));

  if (self->state == PHOC_TIMED_ANIMATION_PLAYING) {
    g_critical ("Trying to play animation %p, but it's already playing", self);
    return;
  }

  self->state = PHOC_TIMED_ANIMATION_PLAYING;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);

  self->elapsed_ms = 0;

  if (self->frame_callback_id)
    return;

  self->frame_callback_id = phoc_animatable_add_frame_callback (PHOC_ANIMATABLE (self->animatable),
                                                                on_frame_callback,
                                                                self,
                                                                NULL);
}


static void
phoc_timed_animation_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  PhocTimedAnimation *self = PHOC_TIMED_ANIMATION (object);

  switch (property_id) {
  case PROP_ANIMATABLE:
    set_animatable (self, g_value_get_object (value));
    break;
  case PROP_PROPERTY_EASER:
    set_property_easer (self, g_value_get_object (value));
    break;
  case PROP_DURATION:
    phoc_timed_animation_set_duration (self, g_value_get_int (value));
    break;
  case PROP_DISPOSE_ON_DONE:
    set_dispose_on_done (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_timed_animation_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PhocTimedAnimation *self = PHOC_TIMED_ANIMATION (object);

  switch (property_id) {
  case PROP_ANIMATABLE:
    g_value_set_object (value, phoc_timed_animation_get_animatable (self));
    break;
  case PROP_PROPERTY_EASER:
    g_value_set_object (value, self->prop_easer);
    break;
  case PROP_DURATION:
    g_value_set_int (value, self->duration);
    break;
  case PROP_DISPOSE_ON_DONE:
    g_value_set_boolean (value, self->dispose_on_done);
    break;
  case PROP_STATE:
    g_value_set_enum (value, phoc_timed_animation_get_state (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_animation_dispose (GObject *object)
{
  PhocTimedAnimation *self = PHOC_TIMED_ANIMATION (object);

  if (self->state == PHOC_TIMED_ANIMATION_PLAYING)
    phoc_timed_animation_skip (self);

  set_animatable (self, NULL);
  set_property_easer (self, NULL);

  G_OBJECT_CLASS (phoc_timed_animation_parent_class)->dispose (object);
}


static void
phoc_timed_animation_class_init (PhocTimedAnimationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_timed_animation_get_property;
  object_class->set_property = phoc_timed_animation_set_property;
  object_class->dispose = phoc_animation_dispose;

  /**
   * PhocTimedAnimation:animatable
   *
   * The animatable that drives the frame clock.
   */
  props[PROP_ANIMATABLE] =
    g_param_spec_object ("animatable",
                         "",
                         "",
                         PHOC_TYPE_ANIMATABLE,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * PhocTimedAnimation:property-easer:
   *
   * The [type@PhocPropertyEaser] that specifies the object and
   * properties to ease in the timed animation.
   */
  props[PROP_PROPERTY_EASER] =
    g_param_spec_object ("property-easer",
                         "",
                         "",
                         PHOC_TYPE_PROPERTY_EASER,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * PhocTimedAnimation:duration:
   *
   * The duration of the animation in milliseconds.
   */
  props[PROP_DURATION] =
    g_param_spec_int ("duration",
                      "",
                      "",
                      0,
                      G_MAXINT,
                      0,
                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhocTimedAnimation:dispose-on-done:
   *
   * Whether the animation should ref itself during construction and drop
   * that reference once it emitted the [signal@TimedAnimation::done]
   * signal.  This frees the animated object from tracking the
   * animations life cycle thus making it easy to create "fire and
   * forget" animations.
   */
  props[PROP_DISPOSE_ON_DONE] =
    g_param_spec_boolean ("dispose-on-done",
                          "",
                          "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PhocAnimation::state:
   *
   * The current state of the animation.
   */
  props[PROP_STATE] =
    g_param_spec_enum ("state",
                       "",
                       "",
                       PHOC_TYPE_ANIMATION_STATE,
                       PHOC_TIMED_ANIMATION_IDLE,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * PhocAnimation::tick:
   *
   * This signal is emitted on every tick of the frame clock that drives the animation.
   */
  signals[TICK] =
    g_signal_new ("tick",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
  /**
   * PhocAnimation::done:
   *
   * This signal is emitted when the animation has been completed, either on its
   * own or via calling [method@Phoc.Animation.skip].
   */
  signals[DONE] =
    g_signal_new ("done",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_timed_animation_init (PhocTimedAnimation *self)
{
}


PhocTimedAnimation *
phoc_timed_animation_new (void)
{
  return g_object_new (PHOC_TYPE_TIMED_ANIMATION, NULL);
}


/**
 * phoc_timed_animation_get_property_easer:
 * @self: a `PhocTimedAnimation`
 *
 * Returns: (transfer none): The property easer of this time animation.
 */
PhocPropertyEaser *
phoc_timed_animation_get_property_easer (PhocTimedAnimation *self)
{
  g_assert (PHOC_IS_TIMED_ANIMATION (self));

  return self->prop_easer;
}


void
phoc_timed_animation_set_duration (PhocTimedAnimation *self, int duration)
{
  g_assert (PHOC_IS_TIMED_ANIMATION (self));

  if (self->duration == duration)
    return;

  self->duration = duration;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DURATION]);
}


int
phoc_timed_animation_get_duration (PhocTimedAnimation *self)
{
  g_assert (PHOC_IS_TIMED_ANIMATION (self));

  return self->duration;
}


/**
 * phoc_timed_animation_get_animatable:
 * @self: a `PhocTimedAnimation`
 *
 * Returns: (transfer none): The animatable of this timed animation.
 */
PhocAnimatable *
phoc_timed_animation_get_animatable (PhocTimedAnimation *self)
{
  g_assert (PHOC_IS_TIMED_ANIMATION (self));

  return self->animatable;
}


/**
 * phoc_timed_animation_play:
 * @self: a `PhocTimedAnimation`
 *
 * Starts the animation for @self.
 *
 * If the animation is playing, or has been completed, restarts it from
 * the beginning. This allows to easily play an animation regardless of whether
 * it's already playing or not.
 *
 * Sets [property@Phoc.TimedAnimation:state] to `PHOC_TIMED_ANIMATION_PLAYING`.
 */
void
phoc_timed_animation_play (PhocTimedAnimation *self)
{
  g_assert (PHOC_IS_TIMED_ANIMATION (self));

  if (self->state != PHOC_TIMED_ANIMATION_IDLE) {
    self->state = PHOC_TIMED_ANIMATION_IDLE;
  }

  play (self);
}


/**
 * phoc_timed_animation_skip:
 * @self: a `PhocTimedAnimation`
 *
 * Skips the animation for @self.
 *
 * If the animation hasn't been started yet, is playing, or is paused, instantly
 * skips the animation to the end and causes [signal@Phoc.TimedAnimation::done] to be
 * emitted.
 *
 * Sets [property@Phoc.TimedAnimation:state] to `PHOC_TIMED_ANIMATION_FINISHED`.
 */
void
phoc_timed_animation_skip (PhocTimedAnimation *self)
{
  g_assert (PHOC_IS_TIMED_ANIMATION (self));

  if (self->state == PHOC_TIMED_ANIMATION_FINISHED)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  self->state = PHOC_TIMED_ANIMATION_FINISHED;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);

  stop_animation (self);

  update_properties (self, self->duration);
  self->elapsed_ms = 0;

  g_object_thaw_notify (G_OBJECT (self));

  g_signal_emit (self, signals[DONE], 0);
  if (self->dispose_on_done) {
    /* Only do this once */
    self->dispose_on_done = FALSE;
    g_object_unref (self);
  }
}


/**
 * phoc_timed_animation_get_state:
 * @self: a `PhocTimedAnimation`
 *
 * Gets the current state of @self.
 *
 * The state indicates whether @self is currently playing, finished or
 * hasn't been started yet.
 */
PhocAnimationState
phoc_timed_animation_get_state (PhocTimedAnimation *self)
{
  g_return_val_if_fail (PHOC_IS_TIMED_ANIMATION (self), PHOC_TIMED_ANIMATION_IDLE);

  return self->state;
}


/**
 * phoc_timed_animation_get_dispose_on_done:
 * @self: a `PhocTimedAnimation`
 *
 * Whether the animation tracks it's own reference.
 *
 * Returns: Whether [property@TimedAnimation:dispose-on-done] is set.
 */
gboolean
phoc_timed_animation_get_dispose_on_done (PhocTimedAnimation *self)
{
  g_return_val_if_fail (PHOC_IS_TIMED_ANIMATION (self), PHOC_TIMED_ANIMATION_IDLE);

  return self->dispose_on_done;
}


/**
 * phoc_timed_animation_reset:
 * @self: a `PhocTimedAnimation`
 *
 * Resets the animation for @self.
 *
 * Sets [property@Phoc.TimedAnimation:state] to `PHOC_TIMED_ANIMATION_IDLE`.
 */
void
phoc_timed_animation_reset (PhocTimedAnimation *self)
{
  g_return_if_fail (PHOC_IS_TIMED_ANIMATION (self));

  if (self->state == PHOC_TIMED_ANIMATION_IDLE)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  self->state = PHOC_TIMED_ANIMATION_IDLE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);

  stop_animation (self);

  update_properties (self, 0);
  self->elapsed_ms = 0;

  g_object_thaw_notify (G_OBJECT (self));
}
