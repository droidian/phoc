/*
 * Copyright (C) 2022 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib-layer-shell.h"

#include <wayland-client-protocol.h>

#define HEIGHT 350
#define FOLDED  50

typedef struct _DragTestSimple {
  bool found_folded;
  bool found_margin;
} DragTestSimple;


static bool
drag_test_done (DragTestSimple *drag_test)
{
  return (drag_test->found_folded == true &&
          drag_test->found_margin == true);
}

static void
drag_test_simple_handle_drag_end (void                                    *data,
                                  struct zphoc_draggable_layer_surface_v1 *drag_surface,
                                  uint32_t                                 state)
{
  DragTestSimple *drag_test = data;

  g_debug ("State: %d", state);
  if (state == ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED)
    drag_test->found_folded = true;
}


static void
drag_test_simple_handle_dragged (void                                    *data,
                                 struct zphoc_draggable_layer_surface_v1 *drag_surface,
                                 int32_t                                  margin)
{
  DragTestSimple *drag_test = data;

  /* Just some random value mid way */
  if (ABS (margin) > HEIGHT / 2)
    drag_test->found_margin = true;
}


const struct zphoc_draggable_layer_surface_v1_listener drag_surface_listener = {
  .drag_end = drag_test_simple_handle_drag_end,
  .dragged = drag_test_simple_handle_dragged,
};


static gboolean
test_client_layer_shell_effects_drag_surface_simple (PhocTestClientGlobals *globals, gpointer data)
{
  PhocTestLayerSurface *ls_green;
  static struct zphoc_draggable_layer_surface_v1 *drag_surface;
  DragTestSimple drag_test = { 0 };

  ls_green = phoc_test_layer_surface_new (globals, 0, HEIGHT, 0xFF00FF00,
					  ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
                                          HEIGHT);
  g_assert_nonnull (ls_green);
  phoc_assert_screenshot (globals, "test-layer-shell-effects-1.png");

  drag_surface = zphoc_layer_shell_effects_v1_get_draggable_layer_surface (
    globals->layer_shell_effects, ls_green->layer_surface);
  g_assert_nonnull (drag_surface);
  zphoc_draggable_layer_surface_v1_add_listener (drag_surface, &drag_surface_listener, &drag_test);
  zphoc_draggable_layer_surface_v1_set_margins (drag_surface, -(HEIGHT - FOLDED), 0);
  zphoc_draggable_layer_surface_v1_set_threshold (drag_surface, wl_fixed_from_double (0.5));
  zphoc_draggable_layer_surface_v1_set_exclusive (drag_surface, FOLDED);
  zphoc_draggable_layer_surface_v1_set_state (drag_surface,
                                              ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED);
  wl_surface_commit (ls_green->wl_surface);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  while (wl_display_dispatch (globals->display) != -1 && !drag_test_done (&drag_test)) {
    /* Main loop */
  }
  phoc_assert_screenshot (globals, "test-layer-shell-effects-2.png");

  phoc_test_layer_surface_free (ls_green);
  phoc_assert_screenshot (globals, "empty.png");

  return TRUE;
}


static void
test_layer_shell_effects_drag_surface_simple (void)
{
  PhocTestClientIface iface = { .client_run =  test_client_layer_shell_effects_drag_surface_simple };

  phoc_test_client_run (3, &iface, NULL);
}


gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func("/phoc/layer-shell-effects/drag-surface/simple",
                  test_layer_shell_effects_drag_surface_simple);

  return g_test_run();
}
