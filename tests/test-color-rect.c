/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "color-rect.h"
#include <glib-object.h>


static void
test_color_rect_new (void)
{
  PhocColorRect *rect = NULL;
  PhocBox box;
  PhocColor color;

  rect = g_object_new (PHOC_TYPE_COLOR_RECT,
                       "color", &(PhocColor){1.0, 2.0, 3.0, 4.0},
                       "box", &(PhocBox){10, 11, 100, 101},
                        NULL);
  box = phoc_color_rect_get_box (rect);
  g_assert_cmpint (box.x, ==, 10);
  g_assert_cmpint (box.y, ==, 11);
  g_assert_cmpint (box.width, ==, 100);
  g_assert_cmpint (box.height, ==, 101);

  color = phoc_color_rect_get_color (rect);
  g_assert_cmpfloat_with_epsilon (color.red, 1.0, FLT_EPSILON);
  g_assert_cmpfloat_with_epsilon (color.green, 2.0, FLT_EPSILON);
  g_assert_cmpfloat_with_epsilon (color.blue, 3.0, FLT_EPSILON);
  g_assert_cmpfloat_with_epsilon (color.alpha, 4.0, FLT_EPSILON);

  g_assert_false (phoc_bling_is_mapped (PHOC_BLING (rect)));

  g_assert_finalize_object (rect);
}


gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func("/phoc/color-rect/new", test_color_rect_new);

  return g_test_run();
}
