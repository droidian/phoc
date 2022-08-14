/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Copied from libadwaita which is
 *
 * Copyright (C) 2021 Manuel Genovés <manuel.genoves@gmail.com>
 * Copyright (C) 2021 Purism SPC
 */

#include "easing.h"

#include <math.h>

static double
phoc_linear (double t, double d)
{
  return t;
}

static inline double
phoc_ease_in_quad (double t, double d)
{
  double p = t / d;

  return p * p;
}

static inline double
phoc_ease_out_quad (double t, double d)
{
  double p = t / d;

  return -1.0 * p * (p - 2);
}

static inline double
phoc_ease_in_out_quad (double t, double d)
{
  double p = t / (d / 2);

  if (p < 1)
    return 0.5 * p * p;

  p -= 1;

  return -0.5 * (p * (p - 2) - 1);
}

static inline double
phoc_ease_in_cubic (double t, double d)
{
  double p = t / d;

  return p * p * p;
}

static inline double
phoc_ease_out_cubic (double t, double d)
{
  double p = t / d - 1;

  return p * p * p + 1;
}

static inline double
phoc_ease_in_out_cubic (double t, double d)
{
  double p = t / (d / 2);

  if (p < 1)
    return 0.5 * p * p * p;

  p -= 2;

  return 0.5 * (p * p * p + 2);
}

static inline double
phoc_ease_in_quart (double t, double d)
{
  double p = t / d;

  return p * p * p * p;
}

static inline double
phoc_ease_out_quart (double t, double d)
{
  double p = t / d - 1;

  return -1.0 * (p * p * p * p - 1);
}

static inline double
phoc_ease_in_out_quart (double t, double d)
{
  double p = t / (d / 2);

  if (p < 1)
    return 0.5 * p * p * p * p;

  p -= 2;

  return -0.5 * (p * p * p * p - 2);
}

static inline double
phoc_ease_in_quint (double t, double d)
{
  double p = t / d;

  return p * p * p * p * p;
}

static inline double
phoc_ease_out_quint (double t, double d)
{
  double p = t / d - 1;

  return p * p * p * p * p + 1;
}

static inline double
phoc_ease_in_out_quint (double t, double d)
{
  double p = t / (d / 2);

  if (p < 1)
    return 0.5 * p * p * p * p * p;

  p -= 2;

  return 0.5 * (p * p * p * p * p + 2);
}

static inline double
phoc_ease_in_sine (double t, double d)
{
  return -1.0 * cos (t / d * G_PI_2) + 1.0;
}

static inline double
phoc_ease_out_sine (double t, double d)
{
  return sin (t / d * G_PI_2);
}

static inline double
phoc_ease_in_out_sine (double t, double d)
{
  return -0.5 * (cos (G_PI * t / d) - 1);
}

static inline double
phoc_ease_in_expo (double t, double d)
{
  return (t == 0) ? 0.0 : pow (2, 10 * (t / d - 1));
}

static inline  double
phoc_ease_out_expo (double t, double d)
{
  return (t == d) ? 1.0 : -pow (2, -10 * t / d) + 1;
}

static inline double
phoc_ease_in_out_expo (double t, double d)
{
  double p;

  if (t == 0)
    return 0.0;

  if (t == d)
    return 1.0;

  p = t / (d / 2);

  if (p < 1)
    return 0.5 * pow (2, 10 * (p - 1));

  p -= 1;

  return 0.5 * (-pow (2, -10 * p) + 2);
}

static inline double
phoc_ease_in_circ (double t, double d)
{
  double p = t / d;

  return -1.0 * (sqrt (1 - p * p) - 1);
}

static inline double
phoc_ease_out_circ (double t, double d)
{
  double p = t / d - 1;

  return sqrt (1 - p * p);
}

static inline double
phoc_ease_in_out_circ (double t, double d)
{
  double p = t / (d / 2);

  if (p < 1)
    return -0.5 * (sqrt (1 - p * p) - 1);

  p -= 2;

  return 0.5 * (sqrt (1 - p * p) + 1);
}

static inline double
phoc_ease_in_elastic (double t, double d)
{
  double p = d * .3;
  double s = p / 4;
  double q = t / d;

  if (q == 1)
    return 1.0;

  q -= 1;

  return -(pow (2, 10 * q) * sin ((q * d - s) * (2 * G_PI) / p));
}

static inline double
phoc_ease_out_elastic (double t, double d)
{
  double p = d * .3;
  double s = p / 4;
  double q = t / d;

  if (q == 1)
    return 1.0;

  return pow (2, -10 * q) * sin ((q * d - s) * (2 * G_PI) / p) + 1.0;
}

static inline double
phoc_ease_in_out_elastic (double t, double d)
{
  double p = d * (.3 * 1.5);
  double s = p / 4;
  double q = t / (d / 2);

  if (q == 2)
    return 1.0;

  if (q < 1) {
    q -= 1;

    return -.5 * (pow (2, 10 * q) * sin ((q * d - s) * (2 * G_PI) / p));
  } else {
    q -= 1;

    return pow (2, -10 * q)
      * sin ((q * d - s) * (2 * G_PI) / p)
      * .5 + 1.0;
  }
}

static inline double
phoc_ease_in_back (double t, double d)
{
  double p = t / d;

  return p * p * ((1.70158 + 1) * p - 1.70158);
}

static inline double
phoc_ease_out_back (double t, double d)
{
  double p = t / d - 1;

  return p * p * ((1.70158 + 1) * p + 1.70158) + 1;
}

static inline double
phoc_ease_in_out_back (double t, double d)
{
  double p = t / (d / 2);
  double s = 1.70158 * 1.525;

  if (p < 1)
    return 0.5 * (p * p * ((s + 1) * p - s));

  p -= 2;

  return 0.5 * (p * p * ((s + 1) * p + s) + 2);
}

static inline double
phoc_ease_out_bounce (double t, double d)
{
  double p = t / d;

  if (p < (1 / 2.75)) {
    return 7.5625 * p * p;
  } else if (p < (2 / 2.75)) {
    p -= (1.5 / 2.75);

    return 7.5625 * p * p + .75;
  } else if (p < (2.5 / 2.75)) {
    p -= (2.25 / 2.75);

    return 7.5625 * p * p + .9375;
  } else {
    p -= (2.625 / 2.75);

    return 7.5625 * p * p + .984375;
  }
}

static inline double
phoc_ease_in_bounce (double t, double d)
{
  return 1.0 - phoc_ease_out_bounce (d - t, d);
}

static inline double
phoc_ease_in_out_bounce (double t, double d)
{
  if (t < d / 2)
    return phoc_ease_in_bounce (t * 2, d) * 0.5;
  else
    return phoc_ease_out_bounce (t * 2 - d, d) * 0.5 + 1.0 * 0.5;
}


/**
 * phoc_easing_ease:
 * @self: a `PhocEasing`
 * @value: a value to ease
 *
 * Computes easing with @easing for @value.
 *
 * @value muste be in the [0, 1] range.
 *
 * Returns: the easing for @value
 */
double
phoc_easing_ease (PhocEasing self,
                  double     value)
{
  switch (self) {
  case PHOC_EASING_NONE:
    return phoc_linear (value, 1);
  case PHOC_EASING_EASE_IN_QUAD:
    return phoc_ease_in_quad (value, 1);
  case PHOC_EASING_EASE_OUT_QUAD:
    return phoc_ease_out_quad (value, 1);
  case PHOC_EASING_EASE_IN_OUT_QUAD:
    return phoc_ease_in_out_quad (value, 1);
  case PHOC_EASING_EASE_IN_CUBIC:
    return phoc_ease_in_cubic (value, 1);
  case PHOC_EASING_EASE_OUT_CUBIC:
    return phoc_ease_out_cubic (value, 1);
  case PHOC_EASING_EASE_IN_OUT_CUBIC:
    return phoc_ease_in_out_cubic (value, 1);
  case PHOC_EASING_EASE_IN_QUART:
    return phoc_ease_in_quart (value, 1);
  case PHOC_EASING_EASE_OUT_QUART:
    return phoc_ease_out_quart (value, 1);
  case PHOC_EASING_EASE_IN_OUT_QUART:
    return phoc_ease_in_out_quart (value, 1);
  case PHOC_EASING_EASE_IN_QUINT:
    return phoc_ease_in_quint (value, 1);
  case PHOC_EASING_EASE_OUT_QUINT:
    return phoc_ease_out_quint (value, 1);
  case PHOC_EASING_EASE_IN_OUT_QUINT:
    return phoc_ease_in_out_quint (value, 1);
  case PHOC_EASING_EASE_IN_SINE:
    return phoc_ease_in_sine (value, 1);
  case PHOC_EASING_EASE_OUT_SINE:
    return phoc_ease_out_sine (value, 1);
  case PHOC_EASING_EASE_IN_OUT_SINE:
    return phoc_ease_in_out_sine (value, 1);
  case PHOC_EASING_EASE_IN_EXPO:
    return phoc_ease_in_expo (value, 1);
  case PHOC_EASING_EASE_OUT_EXPO:
    return phoc_ease_out_expo (value, 1);
  case PHOC_EASING_EASE_IN_OUT_EXPO:
    return phoc_ease_in_out_expo (value, 1);
  case PHOC_EASING_EASE_IN_CIRC:
    return phoc_ease_in_circ (value, 1);
  case PHOC_EASING_EASE_OUT_CIRC:
    return phoc_ease_out_circ (value, 1);
  case PHOC_EASING_EASE_IN_OUT_CIRC:
    return phoc_ease_in_out_circ (value, 1);
  case PHOC_EASING_EASE_IN_ELASTIC:
    return phoc_ease_in_elastic (value, 1);
  case PHOC_EASING_EASE_OUT_ELASTIC:
    return phoc_ease_out_elastic (value, 1);
  case PHOC_EASING_EASE_IN_OUT_ELASTIC:
    return phoc_ease_in_out_elastic (value, 1);
  case PHOC_EASING_EASE_IN_BACK:
    return phoc_ease_in_back (value, 1);
  case PHOC_EASING_EASE_OUT_BACK:
    return phoc_ease_out_back (value, 1);
  case PHOC_EASING_EASE_IN_OUT_BACK:
    return phoc_ease_in_out_back (value, 1);
  case PHOC_EASING_EASE_IN_BOUNCE:
    return phoc_ease_in_bounce (value, 1);
  case PHOC_EASING_EASE_OUT_BOUNCE:
    return phoc_ease_out_bounce (value, 1);
  case PHOC_EASING_EASE_IN_OUT_BOUNCE:
    return phoc_ease_in_out_bounce (value, 1);
  default:
    g_assert_not_reached ();
  }
}
