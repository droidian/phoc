/*
 * Copyright (C) 2019 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-server"

#include "phoc-config.h"
#include "render.h"
#include "render-private.h"
#include "utils.h"
#include "seat.h"
#include "server.h"

#define GMOBILE_USE_UNSTABLE_API
#include <gmobile.h>
#include <wlr/xwayland.h>

#include <errno.h>

typedef struct _PhocServerPrivate {
  GStrv dt_compatibles;
} PhocServerPrivate;

static void phoc_server_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocServer, phoc_server, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (PhocServer)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, phoc_server_initable_iface_init));

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
  .prepare = wayland_event_source_prepare,
  .dispatch = wayland_event_source_dispatch
};

static GSource *
wayland_event_source_new (struct wl_display *display)
{
  WaylandEventSource *source;
  struct wl_event_loop *loop = wl_display_get_event_loop (display);

  source = (WaylandEventSource *) g_source_new (&wayland_event_source_funcs,
                                                sizeof (WaylandEventSource));
  g_source_set_name (&source->source, "[phoc] wayland source");
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
#if GLIB_CHECK_VERSION (2, 70, 0)
  if (g_spawn_check_wait_status (status, &err)) {
#else
  if (g_spawn_check_exit_status (status, &err)) {
#endif
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
     don't want to inherit that to children */
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


static void
on_shell_state_changed (PhocServer *self, GParamSpec *pspec, PhocPhoshPrivate *phosh)
{
  PhocPhoshPrivateShellState state;
  PhocOutput *output;

  g_assert (PHOC_IS_SERVER (self));
  g_assert (PHOC_IS_PHOSH_PRIVATE (phosh));

  state = phoc_phosh_private_get_shell_state (phosh);
  g_debug ("Shell state changed: %d", state);

  switch (state) {
  case PHOC_PHOSH_PRIVATE_SHELL_STATE_UP:
    /* Shell is up, lower shields */
    wl_list_for_each (output, &self->desktop->outputs, link)
      phoc_output_lower_shield (output);
    break;
  case PHOC_PHOSH_PRIVATE_SHELL_STATE_UNKNOWN:
  default:
    /* Shell is gone, raise shields */
    /* TODO: prevent input without a shell attached */
    wl_list_for_each (output, &self->desktop->outputs, link)
      phoc_output_raise_shield (output);
  }
}


static gboolean
phoc_server_initable_init (GInitable    *initable,
                           GCancellable *cancellable,
                           GError      **error)
{
  PhocServer *self = PHOC_SERVER (initable);
  struct wlr_renderer *wlr_renderer;

  self->wl_display = wl_display_create();
  if (self->wl_display == NULL) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "Could not create wayland display");
    return FALSE;
  }

  self->backend = wlr_backend_autocreate(self->wl_display);
  if (self->backend == NULL) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "Could not create backend");
    return FALSE;
  }

  self->renderer = phoc_renderer_new (self->backend, error);
  if (self->renderer == NULL) {
    return FALSE;
  }
  wlr_renderer = phoc_renderer_get_wlr_renderer (self->renderer);

  self->data_device_manager = wlr_data_device_manager_create(self->wl_display);
  wlr_renderer_init_wl_display(wlr_renderer, self->wl_display);

  self->compositor = wlr_compositor_create(self->wl_display,
                                           wlr_renderer);

  return TRUE;
}


static void
phoc_server_initable_iface_init (GInitableIface *iface)
{
  iface->init = phoc_server_initable_init;
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

  g_clear_object (&self->renderer);

  G_OBJECT_CLASS (phoc_server_parent_class)->dispose (object);
}

static void
phoc_server_finalize (GObject *object)
{
  PhocServer *self = PHOC_SERVER (object);
  PhocServerPrivate *priv = phoc_server_get_instance_private (self);

  g_clear_pointer (&priv->dt_compatibles, g_strfreev);
  g_clear_handle_id (&self->wl_source, g_source_remove);
  g_clear_object (&self->input);
  g_clear_object (&self->desktop);
  g_clear_pointer (&self->session, g_free);

  if (self->inited) {
    g_unsetenv("WAYLAND_DISPLAY");
    self->inited = FALSE;
  }

  g_clear_pointer (&self->config, phoc_config_destroy);

  wl_display_destroy (self->wl_display);
  G_OBJECT_CLASS (phoc_server_parent_class)->finalize (object);
}


static void
phoc_server_class_init (PhocServerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phoc_server_finalize;
  object_class->dispose = phoc_server_dispose;
}

static void
phoc_server_init (PhocServer *self)
{
  PhocServerPrivate *priv = phoc_server_get_instance_private(self);
  g_autoptr (GError) err = NULL;

  priv->dt_compatibles = gm_device_tree_get_compatibles (NULL, &err);
}

/**
 * phoc_server_get_default:
 *
 * Get the server singleton.
 *
 * Returns: (transfer none): The server singleton
 */
PhocServer *
phoc_server_get_default (void)
{
  static PhocServer *instance;

  if (G_UNLIKELY (instance == NULL)) {
    g_autoptr (GError) err = NULL;
    g_debug("Creating server");
    instance = g_initable_new (PHOC_TYPE_SERVER, NULL, &err, NULL);
    if (instance == NULL) {
      g_critical ("Failed to create server: %s", err->message);
      return NULL;
    }

    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
  }

  return instance;
}

/**
 * phoc_server_setup:
 * @self: The server
 * @config:(transfer full): The configuration
 * @session: The session name
 * @mainloop:(transfer none): The mainloop
 * @flags: The flags to use for spawning the server
 * @debug_flags: The debug flags to use
 *
 * Perform wayland server initialization: parse command line and config,
 * create the wayland socket, setup env vars.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
phoc_server_setup (PhocServer *self, PhocConfig *config,
                   const char *session, GMainLoop *mainloop,
                   PhocServerFlags flags,
                   PhocServerDebugFlags debug_flags)
{
  g_assert (!self->inited);

  self->config = config;
  self->flags = flags;
  self->debug_flags = debug_flags;
  self->mainloop = mainloop;
  self->exit_status = 1;
  self->desktop = phoc_desktop_new (self->config);
  self->input = phoc_input_new ();
  self->session = g_strdup (session);
  self->mainloop = mainloop;

  const char *socket = wl_display_add_socket_auto(self->wl_display);
  if (!socket) {
    g_warning("Unable to open wayland socket: %s", strerror(errno));
    wlr_backend_destroy(self->backend);
    return FALSE;
  }

  g_print ("Running compositor on wayland display '%s'\n", socket);

  if (!wlr_backend_start(self->backend)) {
    g_warning("Failed to start backend");
    wlr_backend_destroy(self->backend);
    wl_display_destroy(self->wl_display);
    return FALSE;
  }

  g_setenv("WAYLAND_DISPLAY", socket, true);
#ifdef PHOC_XWAYLAND
  if (self->desktop->xwayland != NULL) {
    PhocSeat *xwayland_seat = phoc_input_get_seat(self->input, PHOC_CONFIG_DEFAULT_SEAT_NAME);
    wlr_xwayland_set_seat(self->desktop->xwayland, xwayland_seat->seat);
  }
#endif

  if (self->flags & PHOC_SERVER_FLAG_SHELL_MODE) {
    g_message ("Enabling shell mode");
    g_signal_connect_object (self->desktop->phosh,
                             "notify::shell-state",
                             G_CALLBACK (on_shell_state_changed),
                             self, G_CONNECT_SWAPPED);
    on_shell_state_changed (self, NULL, self->desktop->phosh);
  }

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

/**
 * phoc_server_get_renderer:
 *
 * Returns: (transfer none): The renderer
 */
PhocRenderer *
phoc_server_get_renderer (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->renderer;
}

/**
 * phoc_server_get_desktop:
 *
 * Returns: (transfer none): The desktop
 */
PhocDesktop *
phoc_server_get_desktop (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->desktop;
}


const char * const *
phoc_server_get_compatibles (PhocServer *self)
{
  PhocServerPrivate *priv;

  g_assert (PHOC_IS_SERVER (self));
  priv = phoc_server_get_instance_private (self);

  return (const char * const *)priv->dt_compatibles;
}
