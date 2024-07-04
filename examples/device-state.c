/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-example"

#include "phoc-device-state-unstable-v1-client-protocol.h"

#include <glib.h>

static struct wl_display *display;
static struct zphoc_device_state_v1 *phoc_device_state;
struct zphoc_tablet_mode_switch_v1  *tablet_mode_switch;
struct zphoc_lid_switch_v1          *lid_switch;

static void
tablet_mode_switch_disabled (void *data,
                             struct zphoc_tablet_mode_switch_v1 *zphoc_tablet_mode_switch_v1)
{
  g_message ("Tablet mode disabled");
}

static void
tablet_mode_switch_enabled (void *data,
                            struct zphoc_tablet_mode_switch_v1 *zphoc_tablet_mode_switch_v1)
{
  g_message ("Tablet mode enabled");
}


const struct zphoc_tablet_mode_switch_v1_listener tablet_mode_switch_listener = {
  .disabled = tablet_mode_switch_disabled,
  .enabled = tablet_mode_switch_enabled,
};

static void
lid_switch_closed (void *data,
                   struct zphoc_lid_switch_v1 *zphoc_lid_switch_v1)
{
  g_message ("Lid closed");
}

static void
lid_switch_opened (void *data,
                   struct zphoc_lid_switch_v1 *zphoc_lid_switch_v1)
{
  g_message ("Lid opened");
}

const struct zphoc_lid_switch_v1_listener lid_switch_listener = {
  .closed = lid_switch_closed,
  .opened = lid_switch_opened,
};


static void
device_state_handle_capabilities (void *data,
                                  struct zphoc_device_state_v1 *zphoc_device_state_v1,
                                  uint32_t capabilities)
{
  g_message("Got capabilities: %d", capabilities);

  if (capabilities & ZPHOC_DEVICE_STATE_V1_CAPABILITY_TABLET_MODE_SWITCH) {
    tablet_mode_switch = zphoc_device_state_v1_get_tablet_mode_switch (phoc_device_state);
    zphoc_tablet_mode_switch_v1_add_listener (tablet_mode_switch, &tablet_mode_switch_listener, NULL);
  }
  if (capabilities & ZPHOC_DEVICE_STATE_V1_CAPABILITY_LID_SWITCH) {
    lid_switch = zphoc_device_state_v1_get_lid_switch (phoc_device_state);
    zphoc_lid_switch_v1_add_listener (lid_switch, &lid_switch_listener, NULL);
  }
}


const struct zphoc_device_state_v1_listener device_state_listener = {
  .capabilities = device_state_handle_capabilities,
};


static void
handle_global (void *data, struct wl_registry *registry,
               uint32_t name, const char *interface, uint32_t version)
{
  if (strcmp (interface, zphoc_device_state_v1_interface.name) == 0) {
    phoc_device_state = wl_registry_bind (registry, name,
                                          &zphoc_device_state_v1_interface, 2);
    zphoc_device_state_v1_add_listener (phoc_device_state, &device_state_listener, NULL);
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

  if (phoc_device_state == NULL) {
    g_critical ("device_state protocol not available");
    return 1;
  }

  g_message ("Press CTRL-C to quit");
  wl_display_roundtrip (display);

  while (wl_display_dispatch (display)) {
    // This space intentionally left blank
  }

  return 0;
}
