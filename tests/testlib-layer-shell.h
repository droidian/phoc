/*
 * Copyright (C) 2022 Purism SPC
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib.h"

#pragma once

G_BEGIN_DECLS

typedef struct _PhocTestLayerSurface
{
  struct wl_surface *wl_surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  PhocTestBuffer buffer;
  guint32 width, height;
  gboolean configured;
} PhocTestLayerSurface;

PhocTestLayerSurface *phoc_test_layer_surface_new   (PhocTestClientGlobals *globals,
                                                     guint32 width,
                                                     guint32 height,
                                                     guint32 color,
                                                     guint32 anchor,
                                                     guint32 exclusive_zone);
void                   phoc_test_layer_surface_free (PhocTestLayerSurface *layer_surface);

G_END_DECLS
 
