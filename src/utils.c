/*
 * Copyright (C) 2020,2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: Arnaud Ferraris <arnaud.ferraris@collabora.com>
 *          Clayton Craft <clayton@craftyguy.net>
 *          Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-utils"

#include <wlr/version.h>
#include "utils.h"

void
phoc_utils_fix_transform (enum wl_output_transform *transform)
{
  /*
   * Starting from version 0.11.0, wlroots rotates counter-clockwise, while
   * it was rotating clockwise previously.
   * In order to maintain the same behavior, we need to modify the transform
   * before applying it
   */
  switch (*transform) {
  case WL_OUTPUT_TRANSFORM_90:
    *transform = WL_OUTPUT_TRANSFORM_270;
    break;
  case WL_OUTPUT_TRANSFORM_270:
    *transform = WL_OUTPUT_TRANSFORM_90;
    break;
  case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    *transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
    break;
  case WL_OUTPUT_TRANSFORM_FLIPPED_270:
    *transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
    break;
  default:
    /* Nothing to be done */
    break;
  }
}

/**
 * phoc_utils_rotate_child_position:
 *
 * Rotate a child's position relative to a parent. The parent size is (pw, ph),
 * the child position is (*sx, *sy) and its size is (sw, sh).
 */
void
phoc_utils_rotate_child_position (double *sx, double *sy, double sw, double sh,
                                  double pw, double ph, float rotation)
{
  if (rotation == 0.0) {
    return;
  }

  // Coordinates relative to the center of the subsurface
  double cx = *sx - pw/2 + sw/2,
         cy = *sy - ph/2 + sh/2;
  // Rotated coordinates
  double rx = cos (rotation)*cx - sin (rotation)*cy,
         ry = cos (rotation)*cy + sin (rotation)*cx;

  *sx = rx + pw/2 - sw/2;
  *sy = ry + ph/2 - sh/2;
}


/**
 * phoc_ease_in_cubic:
 * @t: The term
 *
 * Ease in using cubic interpolation.
 */
double
phoc_ease_in_cubic (double t)
{
  double p = t;
  return p * p * p;
}


/**
 * phoc_ease_out_cubic:
 * @t: The term
 *
 * Ease out using cubic interpolation
 */
double
phoc_ease_out_cubic (double t)
{
  double p = t - 1;
  return p * p * p + 1;
}
