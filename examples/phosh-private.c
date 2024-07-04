/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on wlroots layer-shell example which is BSD licensed.
 */

#define G_LOG_DOMAIN "phoc-example"

#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "phosh-private-client-protocol.h"

#include <glib.h>

static struct wl_display *display;
static struct phosh_private *phosh_private;

static void
handle_global (void *data, struct wl_registry *registry,
               uint32_t name, const char *interface, uint32_t version)
{
  if (strcmp (interface, phosh_private_interface.name) == 0) {
    phosh_private = wl_registry_bind (registry, name,
                                      &phosh_private_interface, 6);
  }
}

static void
handle_global_remove (void *data, struct wl_registry *registry,
                      uint32_t name)
{
  // who cares
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = handle_global_remove,
};

int
main (int argc, char **argv)
{
  struct wl_registry *registry;

  display = wl_display_connect (NULL);
  if (display == NULL) {
    g_critical ("Failed to create display");
    return 1;
  }

  registry = wl_display_get_registry (display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display);

  if (phosh_private == NULL) {
    g_critical ("phosh_private not available");
    return 1;
  }

  g_message ("Press CTRL-C to quit");
  phosh_private_set_shell_state (phosh_private, PHOSH_PRIVATE_SHELL_STATE_UP);
  wl_display_roundtrip (display);

  while (wl_display_dispatch (display)) {
    // This space intentionally left blank
  }

  return 0;
}
