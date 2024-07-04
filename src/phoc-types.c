/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phoc-types.h"

/**
 * PhocBox:
 * @x: The x position of the box's top left corner
 * @y: The y position of the box's top left corner
 * @width: The width of the box
 * @height: The height of the box
 *
 * #PhocBox is used to represent a rectangular region. It just wraps
 * `wlr_box` so we can use it in the type system.
 */
G_DEFINE_BOXED_TYPE (PhocBox, phoc_box, phoc_box_copy, phoc_box_free)

PhocBox *
phoc_box_copy (const PhocBox *box)
{
  return g_memdup2 (box, sizeof (PhocBox));
}

void
phoc_box_free (PhocBox *box)
{
  g_free (box);
}


/**
 * PhocColor:
 * @red: The intensity of the red channel from 0.0 to 1.0 inclusive
 * @green: The intensity of the green channel from 0.0 to 1.0 inclusive
 * @blue: The intensity of the blue channel from 0.0 to 1.0 inclusive
 * @alpha: The opacity of the color from 0.0 for completely translucent to
 *   1.0 for opaque
 *
 * #PhocColor is used to represent a (possibly translucent) color.
 */
G_DEFINE_BOXED_TYPE (PhocColor, phoc_color, phoc_color_copy, phoc_color_free)

PhocColor *
phoc_color_copy (const PhocColor *color)
{
  return g_memdup2 (color, sizeof (PhocColor));
}


void
phoc_color_free (PhocColor *rgba)
{
  g_free (rgba);
}

/**
 * phoc_color_is_equal:
 * @c1: A color
 * @c2: Another color
 *
 * Compare two colors for equality
 *
 * Returns: %TRUE if both colors are equal, otherwise %FALSE
 */
gboolean
phoc_color_is_equal (PhocColor *c1, PhocColor *c2)
{
  g_assert (c1);
  g_assert (c2);

  return !memcmp (c1, c2, sizeof (PhocColor));
}
