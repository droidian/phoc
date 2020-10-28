/*
 * Copyright (C) 2019 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-server"

#include "config.h"
#include "server.h"

#include <errno.h>

/* FIXME */
#include <wlr/render/gles2.h>

G_DEFINE_TYPE(PhocServer, phoc_server, G_TYPE_OBJECT);

typedef struct {
  GSource source;
  struct wl_display *display;
} WaylandEventSource;

static gboolean
wayland_event_source_prepare (GSource *base,
                              int     *timeout)
{
  WaylandEventSource *source = (WaylandEventSource *)base;

  *timeout = -1;

  wl_display_flush_clients (source->display);

  return FALSE;
}

static gboolean
wayland_event_source_dispatch (GSource     *base,
                               GSourceFunc callback,
                               void        *data)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  struct wl_event_loop *loop = wl_display_get_event_loop (source->display);

  wl_event_loop_dispatch (loop, 0);

  return TRUE;
}

static GSourceFuncs wayland_event_source_funcs = {
  wayland_event_source_prepare,
  NULL,
  wayland_event_source_dispatch,
  NULL
};

static GSource *
wayland_event_source_new (struct wl_display *display)
{
  WaylandEventSource *source;
  struct wl_event_loop *loop = wl_display_get_event_loop (display);

  source = (WaylandEventSource *) g_source_new (&wayland_event_source_funcs,
                                                sizeof (WaylandEventSource));
  source->display = display;
  g_source_add_unix_fd (&source->source,
                        wl_event_loop_get_fd (loop),
                        G_IO_IN | G_IO_ERR);

  return &source->source;
}

static void
phoc_wayland_init (PhocServer *self)
{
  GSource *wayland_event_source;

  wayland_event_source = wayland_event_source_new (self->wl_display);
  self->wl_source = g_source_attach (wayland_event_source, NULL);
}


static void
on_session_exit (GPid pid, gint status, PhocServer *self)
{
  g_autoptr(GError) err = NULL;

  g_return_if_fail (PHOC_IS_SERVER (self));
  g_spawn_close_pid (pid);
  if (g_spawn_check_exit_status (status, &err)) {
    self->exit_status = 0;
  } else {
    if (err->domain ==  G_SPAWN_EXIT_ERROR)
      self->exit_status = err->code;
    else
      g_warning ("Session terminated: %s (%d)", err->message, self->exit_status);
  }
  if (!(self->debug_flags & PHOC_SERVER_DEBUG_FLAG_NO_QUIT))
    g_main_loop_quit (self->mainloop);
}


static void
on_child_setup (gpointer unused)
{
  sigset_t mask;

  /* phoc wants SIGUSR1 blocked due to wlroots/xwayland but we
     don't want to inherit that to childs */
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);
}


static gboolean
phoc_startup_session_in_idle(PhocServer *self)
{
  GPid pid;
  g_autoptr(GError) err = NULL;
  gchar *cmd[] = { "/bin/sh", "-c", self->session, NULL };

  if (g_spawn_async (NULL, cmd, NULL,
		      G_SPAWN_DO_NOT_REAP_CHILD,
		      on_child_setup, self, &pid, &err)) {
    g_child_watch_add (pid, (GChildWatchFunc)on_session_exit, self);
  } else {
    g_warning ("Failed to launch session: %s", err->message);
    g_main_loop_quit (self->mainloop);
  }
  return FALSE;
}

static void
phoc_startup_session (PhocServer *server)
{
  gint id;

  id = g_idle_add ((GSourceFunc) phoc_startup_session_in_idle, server);
  g_source_set_name_by_id (id, "[phoc] phoc_startup_session");
}

/* FIXME */
static gboolean
can_get_preferred_pixel_format (struct wlr_renderer *renderer) {
    if (wlr_egl_is_current(wlr_gles2_renderer_get_egl (renderer)))
        return TRUE;

    return FALSE;
}

static void
phoc_server_constructed (GObject *object)
{
  PhocServer *self = PHOC_SERVER (object);

  self->wl_display = wl_display_create();
  if (self->wl_display == NULL)
    g_error("Could not create wayland display");

  self->backend = wlr_backend_autocreate(self->wl_display, NULL);
  if (self->backend == NULL)
    g_error("Could not create backend");

  self->renderer = wlr_backend_get_renderer(self->backend);
  if (self->renderer == NULL)
    g_error("Could not create renderer");

  // FIXME: remove once we find something better
  if (can_get_preferred_pixel_format(self->renderer)) {
      self->preferred_pixel_format = self->renderer->impl->preferred_read_format(self->renderer);
  } else {
      self->preferred_pixel_format = WL_SHM_FORMAT_ARGB8888;
  }

  self->data_device_manager =
    wlr_data_device_manager_create(self->wl_display);
  wlr_renderer_init_wl_display(self->renderer, self->wl_display);

  G_OBJECT_CLASS (phoc_server_parent_class)->constructed (object);
}


static void
phoc_server_dispose (GObject *object)
{
  PhocServer *self = PHOC_SERVER (object);

  if (self->backend) {
    wl_display_destroy_clients (self->wl_display);
    wlr_backend_destroy(self->backend);
    self->backend = NULL;
  }

  G_OBJECT_CLASS (phoc_server_parent_class)->dispose (object);
}

static void
phoc_server_finalize (GObject *object)
{
  PhocServer *self = PHOC_SERVER (object);

  if (self->wl_source) {
    g_source_remove (self->wl_source);
    self->wl_source = 0;
  }
  g_clear_object (&self->desktop);
  g_clear_pointer (&self->session, g_free);

  if (self->inited) {
    g_unsetenv("WAYLAND_DISPLAY");
    g_unsetenv("_WAYLAND_DISPLAY");
    self->inited = FALSE;
  }

  wl_display_destroy (self->wl_display);
  G_OBJECT_CLASS (phoc_server_parent_class)->finalize (object);
}


static void
phoc_server_class_init (PhocServerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phoc_server_constructed;
  object_class->finalize = phoc_server_finalize;
  object_class->dispose = phoc_server_dispose;
}

static void
phoc_server_init (PhocServer *self)
{
}

PhocServer *
phoc_server_get_default (void)
{
  static PhocServer *instance;

  if (G_UNLIKELY (instance == NULL)) {
    g_debug("Creating server");
    instance = g_object_new (PHOC_TYPE_SERVER, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
  }

  return instance;
}

/**
 * phoc_server_setup:
 *
 * Perform wayland server intialization: parse command line and config,
 * create the wayland socket, setup env vars.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
phoc_server_setup (PhocServer *self, const char *config_path,
		   const char *session, GMainLoop *mainloop,
		   PhocServerDebugFlags debug_flags)
{
  g_assert (!self->inited);

  self->config = roots_config_create(config_path);
  if (!self->config) {
    g_warning("Failed to parse config");
    return FALSE;
  }

  self->mainloop = mainloop;
  self->exit_status = 1;
  self->desktop = phoc_desktop_new (self->config);
  self->input = input_create(self->config);
  self->session = g_strdup (session);
  self->mainloop = mainloop;
  self->debug_flags = debug_flags;

  const char *socket = wl_display_add_socket_auto(self->wl_display);
  if (!socket) {
    g_warning("Unable to open wayland socket: %s", strerror(errno));
    wlr_backend_destroy(self->backend);
    return FALSE;
  }

  g_print ("Running compositor on wayland display '%s'\n", socket);
  setenv("_WAYLAND_DISPLAY", socket, true);

  if (!wlr_backend_start(self->backend)) {
    g_warning("Failed to start backend");
    wlr_backend_destroy(self->backend);
    wl_display_destroy(self->wl_display);
    return FALSE;
  }

  setenv("WAYLAND_DISPLAY", socket, true);
#ifdef PHOC_XWAYLAND
  if (self->desktop->xwayland != NULL) {
    struct roots_seat *xwayland_seat =
      input_get_seat(self->input, ROOTS_CONFIG_DEFAULT_SEAT_NAME);
    wlr_xwayland_set_seat(self->desktop->xwayland, xwayland_seat->seat);
  }
#endif

  phoc_wayland_init (self);
  if (self->session)
    phoc_startup_session (self);

  self->inited = TRUE;
  return TRUE;
}

/**
 * phoc_server_get_exit_status:
 *
 * Return the session's exit status. This is only meaningful
 * if the session has ended.
 *
 * Returns: The session's exit status.
 */
gint
phoc_server_get_session_exit_status (PhocServer *self)
{
  return self->exit_status;
}
