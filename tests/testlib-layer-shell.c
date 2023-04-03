/*
 * Copyright (C) 2022 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib-layer-shell.h"

static void layer_surface_configure (void                         *data,
                                     struct zwlr_layer_surface_v1 *surface,
                                     uint32_t                      serial,
                                     uint32_t                      width,
                                     uint32_t                      height)
{
  PhocTestLayerSurface *ls = data;

  g_debug ("Configured %p serial %d", surface, serial);
  g_assert_cmpint (serial, >, 0);
  g_assert_nonnull (surface);
  g_assert_cmpint (width, >, 0);
  g_assert_cmpint (height, >, 0);
  zwlr_layer_surface_v1_ack_configure (surface, serial);

  ls->width = width;
  ls->height = height;

  ls->configured = TRUE;
}


static void layer_surface_closed (void                         *data,
                                  struct zwlr_layer_surface_v1 *surface)
{
  g_debug ("Destroyed %p", surface);
  zwlr_layer_surface_v1_destroy (surface);
}


static struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  .configure = layer_surface_configure,
  .closed = layer_surface_closed,
};


PhocTestLayerSurface *
phoc_test_layer_surface_new (PhocTestClientGlobals *globals,
                             guint32 width, guint32 height, guint32 color,
                             guint32 anchor, guint32 exclusive_zone)
{
  PhocTestLayerSurface *ls = g_malloc0 (sizeof(PhocTestLayerSurface));

  ls->wl_surface = wl_compositor_create_surface (globals->compositor);
  g_assert_nonnull (ls->wl_surface);

  ls->layer_surface = zwlr_layer_shell_v1_get_layer_surface (globals->layer_shell,
                                                             ls->wl_surface,
                                                             NULL,
                                                             ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                             "phoc-test");
  g_assert_nonnull (ls->wl_surface);
  zwlr_layer_surface_v1_set_size (ls->layer_surface, width, height);
  zwlr_layer_surface_v1_set_exclusive_zone (ls->layer_surface, exclusive_zone);
  zwlr_layer_surface_v1_add_listener (ls->layer_surface,
                                      &layer_surface_listener,
                                      ls);
  zwlr_layer_surface_v1_set_anchor (ls->layer_surface, anchor);
  wl_surface_commit (ls->wl_surface);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);
  g_assert_true (ls->configured);

  phoc_test_client_create_shm_buffer (globals, &ls->buffer, ls->width, ls->height,
                                      WL_SHM_FORMAT_XRGB8888);

  for (int i = 0; i < ls->width * ls->height * 4; i+=4)
    *(guint32*)(ls->buffer.shm_data + i) = color;

  wl_surface_attach (ls->wl_surface, ls->buffer.wl_buffer, 0, 0);
  wl_surface_damage (ls->wl_surface, 0, 0, ls->width, ls->height);
  wl_surface_commit (ls->wl_surface);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  return ls;
}

void
phoc_test_layer_surface_free (PhocTestLayerSurface *ls)
{
  zwlr_layer_surface_v1_destroy (ls->layer_surface);
  wl_surface_destroy (ls->wl_surface);
  phoc_test_buffer_free (&ls->buffer);
  g_free (ls);
}
