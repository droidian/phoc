/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0+
 */

#include "testlib.h"


static gboolean
test_client_xdg_decoration_server_side (PhocTestClientGlobals *globals, gpointer data)
{
  PhocTestXdgToplevelSurface *xs;
  guint32 color = 0xFF00FF00;
  struct zxdg_toplevel_decoration_v1 *toplevel_decoration_manager;

  xs = phoc_test_xdg_toplevel_new (globals, 0, 0, "server-side-decoration");
  g_assert_nonnull (xs);

  toplevel_decoration_manager = zxdg_decoration_manager_v1_get_toplevel_decoration(
    globals->decoration_manager, xs->xdg_toplevel);

  /* As per protocol we need to set decoration before attaching a buffer */
  zxdg_toplevel_decoration_v1_set_mode(toplevel_decoration_manager,
                                       ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  wl_display_dispatch (globals->display);

  phoc_test_xdg_update_buffer (globals, xs, color);

  phoc_assert_screenshot (globals, "test-xdg-decoration-server-side-1.png");

  zxdg_toplevel_decoration_v1_set_mode(toplevel_decoration_manager,
                                       ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
  wl_display_dispatch (globals->display);
  phoc_test_xdg_update_buffer (globals, xs, color);

  phoc_assert_screenshot (globals, "test-xdg-decoration-client-side-1.png");

  zxdg_toplevel_decoration_v1_destroy (toplevel_decoration_manager);
  phoc_test_xdg_toplevel_free (xs);

  phoc_assert_screenshot (globals, "empty.png");

  return TRUE;
}


static gboolean
test_client_xdg_decoration_server_prepare (PhocServer *server, gpointer data)
{
  PhocDesktop *desktop = server->desktop;
  gboolean maximize = GPOINTER_TO_INT (data);

  g_assert_nonnull (desktop);
  phoc_desktop_set_auto_maximize (desktop, maximize);
  return TRUE;
}


static void
test_xdg_decoration_server_side (void)
{
  PhocTestClientIface iface = {
    .server_prepare = test_client_xdg_decoration_server_prepare,
    .client_run     = test_client_xdg_decoration_server_side,
    .debug_flags    = PHOC_SERVER_DEBUG_FLAG_DISABLE_ANIMATIONS,
  };

  phoc_test_client_run (TEST_PHOC_CLIENT_TIMEOUT, &iface, GINT_TO_POINTER (FALSE));
}


gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  PHOC_TEST_ADD ("/phoc/xdg-decoration/server-side", test_xdg_decoration_server_side);

  return g_test_run();
}
