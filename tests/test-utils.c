/*
 * Copyright (C) 2022 Collabora Ltd
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Arnaud Ferraris <arnaud.ferraris@collabora.com>
 */

#include "utils.h"

/*
 * Test the scaling factor is properly calculated based on the
 * display properties of known devices.
 */
static void
test_phoc_utils_compute_scale (void)
{
  float scale;

  /* OnePlus 6: size 68 x 145 mm / mode 1080 x 2280 px */
  scale = phoc_utils_compute_scale (68, 145, 1080, 2280);
  g_assert_cmpfloat (scale, ==, 3.0);

  /* Librem 5: size 65 x 130 mm / mode 720 x 1440 px */
  scale = phoc_utils_compute_scale (65, 130, 720, 1440);
  g_assert_cmpfloat (scale, ==, 2.0);

  /* PineTab: size 135 x 217 mm / mode 800 x 1280 px */
  scale = phoc_utils_compute_scale (135, 217, 800, 1280);
  g_assert_cmpfloat (scale, ==, 1.0);

  /* Surface Pro 3: size 250 x 170 mm / mode 2160 x 1440 px */
  scale = phoc_utils_compute_scale (250, 170, 2160, 1440);
  g_assert_cmpfloat (scale, ==, 2.0);

  /* Nexdock 360: size 290 x 170 mm / mode 1920 x 1080 px */
  scale = phoc_utils_compute_scale (290, 170, 1920, 1080);
  g_assert_cmpfloat (scale, ==, 1.0);

  /* STM32MP157C-DK2: size 52 x 86 mm / mode 480 x 800 px */
  scale = phoc_utils_compute_scale (52, 86, 480, 800);
  g_assert_cmpfloat (scale, ==, 1.25);

  /* Aspect ratio instead of physical size */
  scale = phoc_utils_compute_scale (16, 10, 100, 100);
  g_assert_cmpfloat (scale, ==, 1.0);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phoc/utils/compute_scale", test_phoc_utils_compute_scale);

  return g_test_run ();
}
