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

#include <inttypes.h>
#include <math.h>
#include <wlr/util/box.h>
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
 * phoc_utils_rotated_bounds:
 *
 * Stores the smallest box that can contain provided box after rotating it
 * by specified rotation into *dest.
 */
void
phoc_utils_rotated_bounds (struct wlr_box *dest, const struct wlr_box *box, float rotation)
{
  if (rotation == 0) {
    *dest = *box;
    return;
  }

  double ox = box->x + (double) box->width / 2;
  double oy = box->y + (double) box->height / 2;

  double c = fabs (cos (rotation));
  double s = fabs (sin (rotation));

  double x1 = ox + (box->x - ox) * c + (box->y - oy) * s;
  double x2 = ox + (box->x + box->width - ox) * c + (box->y + box->height - oy) * s;

  double y1 = oy + (box->x - ox) * s + (box->y - oy) * c;
  double y2 = oy + (box->x + box->width - ox) * s + (box->y + box->height - oy) * c;

  dest->x = floor (fmin (x1, x2));
  dest->width = ceil (fmax (x1, x2) - fmin (x1, x2));
  dest->y = floor (fmin (y1, y2));
  dest->height = ceil (fmax (y1, y2) - fmin (y1, y2));
}

#define MIN_WIDTH       360.0
#define MIN_HEIGHT      540.0
#define MAX_DPI_TARGET  180.0
#define INCH_IN_MM      25.4

float
phoc_utils_compute_scale (int32_t phys_width, int32_t phys_height,
                          int32_t width, int32_t height)
{
  float dpi, long_side, short_side, max_scale, scale;

  if (width > height) {
    long_side = width;
    short_side = height;
  } else {
    long_side = height;
    short_side = width;
  }
  // Ensure scaled resolution won't be inferior to minimum values
  max_scale = fminf (long_side / MIN_HEIGHT, short_side / MIN_WIDTH);

  /*
   * Round the maximum scale to a sensible value:
   *   - never use a scaling factor < 1
   *   - round to the lower 0.25 step below 2
   *   - round to the lower 0.5 step between 2 and 3
   *   - round to the lower integer value over 3
   */
  if (max_scale < 1) {
    max_scale = 1;
  } else if (max_scale < 2) {
    max_scale = 0.25 * floorf (max_scale / 0.25);
  } else if (max_scale < 3) {
    max_scale = 0.5 * floorf (max_scale / 0.5);
  } else {
    max_scale = floorf (max_scale);
  }

  dpi = (float) height / (float) phys_height * INCH_IN_MM;
  scale = fminf (ceilf (dpi / MAX_DPI_TARGET), max_scale);

  g_debug ("Output DPI is %f for mode %" PRId32 "x%" PRId32", using scale %f",
           dpi, width, height, scale);

  return scale;
}
