/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on wlroots idle example which is BSD licensed.
 */

#include "ext-idle-notify-v1-client-protocol.h"

#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include <glib.h>

#include <pthread.h>
#include <unistd.h>


static struct ext_idle_notifier_v1 *idle_manager = NULL;
static struct wl_seat *seat = NULL;
static uint32_t timeout = 0, close_timeout = 0;
static int run = TRUE;

struct thread_args {
  struct wl_display                *display;
  struct ext_idle_notification_v1  *timer;
};

static void
handle_global (void *data, struct wl_registry *registry,
               uint32_t name, const char *interface, uint32_t version)
{
  g_debug ("Interface found: %s", interface);
  if (strcmp (interface, ext_idle_notifier_v1_interface.name) == 0)
    idle_manager = wl_registry_bind (registry, name, &ext_idle_notifier_v1_interface, 1);
  else if (strcmp (interface, "wl_seat") == 0)
    seat = wl_registry_bind (registry, name, &wl_seat_interface, 1);
}

static void
handle_global_remove (void *data, struct wl_registry *registry, uint32_t name)
{
  // TODO
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = handle_global_remove,
};

static void
handle_idled (void* data, struct ext_idle_notification_v1 *timer)
{
  g_message ("idle state");
}

static void
handle_resumed (void* data, struct ext_idle_notification_v1 *timer)
{
  g_message ("active state");
}

static const struct ext_idle_notification_v1_listener idle_notification_listener = {
  .idled = handle_idled,
  .resumed = handle_resumed,
};


static void *
close_program (void *data)
{
  sleep (close_timeout);
  struct thread_args *arg = data;

  run = 0;
  ext_idle_notification_v1_destroy (arg->timer);
  wl_display_roundtrip (arg->display);
  g_message ("close program");
  return NULL;
}


int
main (int argc, char *argv[])
{
  g_autoptr(GOptionContext) opt_context = NULL;
  g_autoptr (GError) err = NULL;
  const GOptionEntry options [] = {
    {"timeout", 't', 0, G_OPTION_ARG_INT, &timeout, "Idle timeout in seconds", NULL},
    {"close", 'c', 0, G_OPTION_ARG_INT, &close_timeout, "close program after x seconds", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
  };
  struct ext_idle_notification_v1 *timer;
  struct wl_registry *registry;
  struct wl_display *display;
  gboolean create_t;
  pthread_t t;
  struct thread_args arg;

  opt_context = g_option_context_new ("- ext-idle-notify-v1 example");
  g_option_context_add_main_entries (opt_context, options, NULL);
  if (!g_option_context_parse (opt_context, &argc, &argv, &err)) {
    g_critical ("Failed to aprse options: %s", err->message);
    return EXIT_FAILURE;
  }

  if (timeout == 0) {
    g_critical ("idle timeout 0 is invalid");
    return EXIT_FAILURE;
  }

  display = wl_display_connect (NULL);
  if (display == NULL) {
    g_critical ("failed to create display");
    return EXIT_FAILURE;
  }

  registry = wl_display_get_registry (display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display);
  wl_registry_destroy (registry);

  if (idle_manager == NULL) {
    g_critical ("display doesn't support idle protocol");
    return EXIT_FAILURE;
  }
  if (seat== NULL) {
    g_critical ("seat error");
    return EXIT_FAILURE;
  }

  timer = ext_idle_notifier_v1_get_idle_notification (idle_manager, timeout * 1000, seat);
  if (timer == NULL) {
    g_critical ("Could not create idle_timeout");
    return EXIT_FAILURE;
  }

  create_t = (close_timeout != 0);
  arg = (struct thread_args){
    .timer = timer,
    .display = display,
  };
  if (create_t) {
    if (pthread_create (&t, NULL, &close_program, (void *)&arg) != 0)
      return EXIT_FAILURE;
  }

  ext_idle_notification_v1_add_listener (timer, &idle_notification_listener, timer);
  g_message ("waiting");

  while (wl_display_dispatch (display) != -1 && run) {
    ;
  }

  if (create_t)
    pthread_join (t, NULL);

  wl_display_disconnect (display);
  return EXIT_SUCCESS;
}
