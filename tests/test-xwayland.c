/*
 * Copyright (C) Guido Günther <agx@sigxcpu.org>
 * SPDX-License-Identifier: GPL-3.0+
 */

#include <xcb/xcb.h>
#include <glib-unix.h>
#include <glib/gstdio.h>

#include "testlib.h"


typedef struct  {
  xcb_connection_t      *conn;
  PhocTestClientGlobals *globals;
  GMainLoop             *loop;

  gboolean               mapped;
  gboolean               unmapped;
} PhocXcbTestClientData;


static gboolean
on_xcb_fd (int fd, GIOCondition condition, gpointer user_data)
{
  PhocXcbTestClientData *cdata = user_data;
  xcb_generic_event_t *event;

  if (condition & G_IO_HUP)
    return G_SOURCE_REMOVE;

  if (condition & G_IO_NVAL)
    return G_SOURCE_REMOVE;

  g_assert_cmpint (condition, ==, G_IO_IN);

  event = xcb_poll_for_event (cdata->conn);
  if (event == NULL)
    return G_SOURCE_CONTINUE;

  if ((event->response_type & 0x7f) == XCB_MAP_NOTIFY) {
    g_debug ("Xcb Window mapped, taking screenshot");
    phoc_assert_screenshot (cdata->globals, "test-xwayland-simple-1.png");
    cdata->mapped = TRUE;
    g_main_loop_quit (cdata->loop);
  } else if ((event->response_type & 0x7f) == XCB_UNMAP_NOTIFY) {
    g_debug ("Xcb Window unmapped, taking screenshot");
    phoc_assert_screenshot (cdata->globals, "empty.png");
    cdata->unmapped = TRUE;
    g_main_loop_quit (cdata->loop);
  }

  free (event);
  return G_SOURCE_CONTINUE;
}


static gboolean
test_client_xwayland_normal (PhocTestClientGlobals *globals, gpointer data)
{
  g_autoptr (GMainContext) client_context = g_main_context_new ();
  g_autoptr (GMainLoop) loop = g_main_loop_new (client_context, FALSE);
  g_autoptr (GSource) source = NULL;
  PhocXcbTestClientData cdata = {
    .conn = xcb_connect (NULL, NULL),
    .globals = globals,
    .loop = loop,
  };
  const xcb_setup_t      *setup  = xcb_get_setup (cdata.conn);
  xcb_screen_iterator_t   iter   = xcb_setup_roots_iterator (setup);
  xcb_screen_t           *screen = iter.data;
  xcb_window_t            window = xcb_generate_id (cdata.conn);
  int                     xcb_fd = -1;
  uint32_t values[2] = {
    screen->white_pixel,
    XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
  };

  /* Make sure we poll the xcb connection in this thread */
  g_main_context_push_thread_default (client_context);
  cdata.loop = loop;
  xcb_fd = xcb_get_file_descriptor (cdata.conn);
  g_assert (xcb_fd >= 0);
  source = g_unix_fd_source_new (xcb_fd, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL);
  g_source_set_callback (source, (GSourceFunc) on_xcb_fd, &cdata, NULL);
  g_source_attach (source, client_context);

  /* Create and map the window */
  xcb_create_window (cdata.conn,                            /* Connection          */
                     XCB_COPY_FROM_PARENT,                  /* depth               */
                     window,                                /* window Id           */
                     screen->root,                          /* parent window       */
                     0, 0,                                  /* x, y                */
                     150, 150,                              /* width, height       */
                     5,                                     /* border_width        */
                     XCB_WINDOW_CLASS_INPUT_OUTPUT,         /* class               */
                     screen->root_visual,                   /* visual              */
                     XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, /* needs to match values */
                     &values);
  xcb_map_window (cdata.conn, window);
  xcb_flush (cdata.conn);

  /* Run the main loop in this thread until we see a mapped event */
  g_main_loop_run (cdata.loop);
  g_assert_true (cdata.mapped);

  xcb_unmap_window (cdata.conn, window);
  xcb_flush (cdata.conn);
  /* Run the main loop in this thread until we see a mapped event */
  g_main_loop_run (cdata.loop);
  g_assert_true (cdata.unmapped);

  xcb_disconnect (cdata.conn);

  phoc_assert_screenshot (globals, "empty.png");

  return TRUE;
}

static gboolean
test_client_xwayland_server_prepare (PhocServer *server, gpointer data)
{
  PhocDesktop *desktop = server->desktop;
  gboolean maximize = GPOINTER_TO_INT (data);

  g_assert_nonnull (g_getenv ("DISPLAY"));
  g_assert_nonnull (desktop);
  phoc_desktop_set_auto_maximize (desktop, maximize);
  return TRUE;
}

static void
test_xwayland_normal (void)
{
  PhocConfig *config = phoc_config_new_from_data (
    "[core]\n"
    "xwayland=true\n"
    "[output:HEADLESS-1]\n"
    "mode=1024x768\n"
    );
  PhocTestClientIface iface = {
    .config         = config,
    .server_prepare = test_client_xwayland_server_prepare,
    .client_run     = test_client_xwayland_normal,
    .debug_flags    = PHOC_SERVER_DEBUG_FLAG_DISABLE_ANIMATIONS,
  };

  phoc_test_client_run (3, &iface, GINT_TO_POINTER (FALSE));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  PHOC_TEST_ADD ("/phoc/xwayland/simple", test_xwayland_normal);
  return g_test_run ();
}
