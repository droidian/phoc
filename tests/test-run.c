/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib.h"
#include "server.h"

static gboolean
on_timer_expired (gpointer unused)
{
  /* Compositor did not quit in time */
  g_assert_not_reached ();
  return FALSE;
}

static void
test_phoc_run_session_success (void)
{
  PhocConfig *config = phoc_config_new_from_file (TEST_PHOC_INI);
  g_autoptr(PhocServer) server = phoc_server_get_default ();
  g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);

  g_assert_true (PHOC_IS_SERVER (server));
  g_assert_true (config);

  g_assert_true (phoc_server_setup(server, config, "/bin/true", loop,
                                   PHOC_SERVER_FLAG_NONE,
                                   PHOC_SERVER_DEBUG_FLAG_NONE));
  g_timeout_add_seconds (TEST_PHOC_CLIENT_TIMEOUT, on_timer_expired, NULL);
  g_main_loop_run (loop);
  g_assert_cmpint (phoc_server_get_session_exit_status (server), ==, 0);
}

static void
test_phoc_run_session_failure (void)
{
  PhocConfig *config = phoc_config_new_from_file (TEST_PHOC_INI);
  g_autoptr(PhocServer) server = phoc_server_get_default ();
  g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);

  g_assert_true (PHOC_IS_SERVER (server));
  g_assert_true (config);

  g_assert_true (phoc_server_setup(server, config, "/bin/false", loop,
                                   PHOC_SERVER_FLAG_NONE,
                                   PHOC_SERVER_DEBUG_FLAG_NONE));
  g_timeout_add_seconds (TEST_PHOC_CLIENT_TIMEOUT, on_timer_expired, NULL);
  g_main_loop_run (loop);
  g_assert_cmpint (phoc_server_get_session_exit_status (server), ==, 1);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  PHOC_TEST_ADD ("/phoc/run/session_success", test_phoc_run_session_success);
  PHOC_TEST_ADD ("/phoc/run/session_failure", test_phoc_run_session_failure);

  return g_test_run();
}
