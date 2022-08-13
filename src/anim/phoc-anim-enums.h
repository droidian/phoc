/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * PhocAnimationState:
 * @PHOC_TIMED_ANIMATION_IDLE: The animation hasn't started yet.
 * @PHOC_TIMED_ANIMATION_PLAYING: The animation is currently playing.
 * @PHOC_TIMED_ANIMATION_FINISHED: The animation has finished.
 *
 * Describes the possible states of an [type@PhocTimedAnimation].
 *
 * The state can be controlled with [method@PhocTimedAnimation.play]
 * and [method@PhocTimedAnimation.skip].
 */
typedef enum {
  PHOC_TIMED_ANIMATION_IDLE = 0,
  PHOC_TIMED_ANIMATION_PLAYING,
  PHOC_TIMED_ANIMATION_FINISHED,
} PhocAnimationState;

/**
 * PhocEasing:
 * @PHOC_EASING_NONE: No easing, linear tweening.
 * @PHOC_EASING_EASE_IN_QUAD: Quadratic tweening.
 * @PHOC_EASING_EASE_OUT_QUAD: Quadratic tweening, inverse of `PHOC_EASING_EASE_IN_QUAD`.
 * @PHOC_EASING_EASE_IN_OUT_QUAD: Quadratic tweening, combining `PHOC_EASING_EASE_IN_QUAD` and
 *   `PHOC_EASING_EASE_OUT_QUAD`.
 * @PHOC_EASING_EASE_IN_CUBIC: Cubic tweening.
 * @PHOC_EASING_EASE_OUT_CUBIC: Cubic tweening, inverse of `PHOC_EASING_EASE_IN_CUBIC`.
 * @PHOC_EASING_EASE_IN_OUT_CUBIC: Cubic tweening, combining `PHOC_EASING_EASE_IN_CUBIC` and
 *   `PHOC_EASING_EASE_OUT_CUBIC`.
 * @PHOC_EASING_EASE_IN_QUART: Quartic tweening.
 * @PHOC_EASING_EASE_OUT_QUART: Quartic tweening, inverse of `PHOC_EASING_EASE_IN_QUART`.
 * @PHOC_EASING_EASE_IN_OUT_QUART: Quartic tweening, combining `PHOC_EASING_EASE_IN_QUART` and
 *   `PHOC_EASING_EASE_OUT_QUART`.
 * @PHOC_EASING_EASE_IN_QUINT: Quintic tweening.
 * @PHOC_EASING_EASE_OUT_QUINT: Quintic tweening, inverse of `PHOC_EASING_EASE_IN_QUINT`.
 * @PHOC_EASING_EASE_IN_OUT_QUINT: Quintic tweening, combining `PHOC_EASING_EASE_IN_QUINT` and
 *   `PHOC_EASING_EASE_OUT_QUINT`.
 * @PHOC_EASING_EASE_IN_SINE: Sine wave tweening.
 * @PHOC_EASING_EASE_OUT_SINE: Sine wave tweening, inverse of `PHOC_EASING_EASE_IN_SINE`.
 * @PHOC_EASING_EASE_IN_OUT_SINE: Sine wave tweening, combining `PHOC_EASING_EASE_IN_SINE` and
 *   `PHOC_EASING_EASE_OUT_SINE`.
 * @PHOC_EASING_EASE_IN_EXPO: Exponential tweening.
 * @PHOC_EASING_EASE_OUT_EXPO: Exponential tweening, inverse of `PHOC_EASING_EASE_IN_EXPO`.
 * @PHOC_EASING_EASE_IN_OUT_EXPO: Exponential tweening, combining `PHOC_EASING_EASE_IN_EXPO` and
 *   `PHOC_EASING_EASE_OUT_EXPO`.
 * @PHOC_EASING_EASE_IN_CIRC: Circular tweening.
 * @PHOC_EASING_EASE_OUT_CIRC: Circular tweening, inverse of `PHOC_EASING_EASE_IN_CIRC`.
 * @PHOC_EASING_EASE_IN_OUT_CIRC: Circular tweening, combining `PHOC_EASING_EASE_IN_CIRC` and
 *   `PHOC_EASING_EASE_OUT_CIRC`.
 * @PHOC_EASING_EASE_IN_ELASTIC: Elastic tweening, with offshoot on start.
 * @PHOC_EASING_EASE_OUT_ELASTIC: Elastic tweening, with offshoot on end, inverse of
 *   `PHOC_EASING_EASE_IN_ELASTIC`.
 * @PHOC_EASING_EASE_IN_OUT_ELASTIC: Elastic tweening, with offshoot on both ends,
 *   combining `PHOC_EASING_EASE_IN_ELASTIC` and `PHOC_EASING_EASE_OUT_ELASTIC`.
 * @PHOC_EASING_EASE_IN_BACK: Overshooting cubic tweening, with backtracking on start.
 * @PHOC_EASING_EASE_OUT_BACK: Overshooting cubic tweening, with backtracking on end,
 *   inverse of `PHOC_EASING_EASE_IN_BACK`.
 * @PHOC_EASING_EASE_IN_OUT_BACK: Overshooting cubic tweening, with backtracking on both
 *   ends, combining `PHOC_EASING_EASE_IN_BACK` and `PHOC_EASING_EASE_OUT_BACK`.
 * @PHOC_EASING_EASE_IN_BOUNCE: Exponentially decaying parabolic (bounce) tweening,
 *   on start.
 * @PHOC_EASING_EASE_OUT_BOUNCE: Exponentially decaying parabolic (bounce) tweening,
 *   with bounce on end, inverse of `PHOC_EASING_EASE_IN_BOUNCE`.
 * @PHOC_EASING_EASE_IN_OUT_BOUNCE: Exponentially decaying parabolic (bounce) tweening,
 *   with bounce on both ends, combining `PHOC_EASING_EASE_IN_BOUNCE` and
 *   `PHOC_EASING_EASE_OUT_BOUNCE`.
 *
 * Describes the available easing functions for use with
 * [class@Phoc.TimedAnimation].
 *
 * New values may be added to this enumeration over time.
 */
typedef enum {
  PHOC_EASING_NONE,
  PHOC_EASING_EASE_IN_QUAD,
  PHOC_EASING_EASE_OUT_QUAD,
  PHOC_EASING_EASE_IN_OUT_QUAD,
  PHOC_EASING_EASE_IN_CUBIC,
  PHOC_EASING_EASE_OUT_CUBIC,
  PHOC_EASING_EASE_IN_OUT_CUBIC,
  PHOC_EASING_EASE_IN_QUART,
  PHOC_EASING_EASE_OUT_QUART,
  PHOC_EASING_EASE_IN_OUT_QUART,
  PHOC_EASING_EASE_IN_QUINT,
  PHOC_EASING_EASE_OUT_QUINT,
  PHOC_EASING_EASE_IN_OUT_QUINT,
  PHOC_EASING_EASE_IN_SINE,
  PHOC_EASING_EASE_OUT_SINE,
  PHOC_EASING_EASE_IN_OUT_SINE,
  PHOC_EASING_EASE_IN_EXPO,
  PHOC_EASING_EASE_OUT_EXPO,
  PHOC_EASING_EASE_IN_OUT_EXPO,
  PHOC_EASING_EASE_IN_CIRC,
  PHOC_EASING_EASE_OUT_CIRC,
  PHOC_EASING_EASE_IN_OUT_CIRC,
  PHOC_EASING_EASE_IN_ELASTIC,
  PHOC_EASING_EASE_OUT_ELASTIC,
  PHOC_EASING_EASE_IN_OUT_ELASTIC,
  PHOC_EASING_EASE_IN_BACK,
  PHOC_EASING_EASE_OUT_BACK,
  PHOC_EASING_EASE_IN_OUT_BACK,
  PHOC_EASING_EASE_IN_BOUNCE,
  PHOC_EASING_EASE_OUT_BOUNCE,
  PHOC_EASING_EASE_IN_OUT_BOUNCE,
} PhocEasing;

G_END_DECLS
