/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "server.h"

static void
test_phoc_server_get_default (void)
{
  g_autoptr(PhocServer) server = phoc_server_get_default ();
  PhocServer *server2;

  g_assert_true (PHOC_IS_SERVER (server));

  server2 = phoc_server_get_default ();
  g_assert_true (server2 == server);
}

static void
test_phoc_server_setup (void)
{
  PhocConfig *config = phoc_config_new_from_file (TEST_PHOC_INI);
  g_autoptr(PhocServer) server = phoc_server_get_default ();

  g_assert_true (PHOC_IS_SERVER (server));
  g_assert_true (config);

  g_assert_true (phoc_server_setup(server, config, NULL, NULL,
                                   PHOC_SERVER_FLAG_NONE,
                                   PHOC_SERVER_DEBUG_FLAG_NONE));
}

static void
test_phoc_server_setup_args (void)
{
  PhocConfig *config = phoc_config_new_from_file (TEST_PHOC_INI);
  g_autoptr(PhocServer) server = phoc_server_get_default ();

  g_assert_true (PHOC_IS_SERVER (server));
  g_assert_true (config);

  g_assert_true (phoc_server_setup(server, config, "/bin/bash", NULL,
                                   PHOC_SERVER_FLAG_NONE,
                                   PHOC_SERVER_DEBUG_FLAG_NONE));

  g_assert_cmpstr (phoc_server_get_session_exec (server), ==, "/bin/bash");
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func("/phoc/server/get_default", test_phoc_server_get_default);
  g_test_add_func("/phoc/server/setup", test_phoc_server_setup);
  g_test_add_func("/phoc/server/setup-args", test_phoc_server_setup_args);

  return g_test_run();
}
