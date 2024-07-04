/*
 * Copyright (C) 2019 Purism SPC
 *               2023-2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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

#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/xwayland.h>
#include <wlr/xwayland/shell.h>

#include <errno.h>

/* Maximum protocol versions we support */
#define PHOC_WL_DISPLAY_VERSION 6
#define PHOC_LINUX_DMABUF_VERSION 4

/**
 * PhocServer:
 *
 * The server singleton.
 *
 * Maintains the compositor's state.
 */
typedef struct _PhocServer {
  GObject              parent;

  gboolean             inited;

  PhocInput           *input;
  PhocConfig          *config;
  PhocServerFlags      flags;
  PhocServerDebugFlags debug_flags;

  PhocRenderer        *renderer;
  PhocDesktop         *desktop;

  gchar               *session_exec;
  gint                 exit_status;
  GMainLoop           *mainloop;

  GStrv                dt_compatibles;

  struct wl_display   *wl_display;
  guint                wl_source;

  struct wlr_compositor    *compositor;
  struct wlr_subcompositor *subcompositor;
  struct wlr_backend       *backend;
  struct wlr_session       *session;

  struct wlr_linux_dmabuf_v1     *linux_dmabuf_v1;
  struct wlr_data_device_manager *data_device_manager;
} PhocServer;

static void phoc_server_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocServer, phoc_server, G_TYPE_OBJECT,
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
  if (g_spawn_check_wait_status (status, &err)) {
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
phoc_startup_session_in_idle (PhocServer *self)
{
  GPid pid;
  g_auto (GStrv) argv;
  g_autoptr (GError) err = NULL;
  gboolean success;

  success = g_shell_parse_argv (self->session_exec, NULL, &argv, &err);
  if (!success) {
    g_critical ("Failed to parse session command: %s", err->message);
    g_main_loop_quit (self->mainloop);
  }

  if (g_spawn_async (NULL, argv, NULL,
                     G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                     on_child_setup, self, &pid, &err)) {
    g_child_watch_add (pid, (GChildWatchFunc)on_session_exit, self);
  } else {
    g_critical ("Failed to launch session: %s", err->message);
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
phoc_server_client_has_security_context (PhocServer *self, const struct wl_client *client)
{
  const struct wlr_security_context_v1_state *context;
  PhocDesktop *desktop = self->desktop;

  context = wlr_security_context_manager_v1_lookup_client (desktop->security_context_manager_v1,
                                                           (struct wl_client *)client);
  return context != NULL;
}


static bool
phoc_server_filter_globals (const struct wl_client *client,
                            const struct wl_global *global,
                            void                   *data)
{
  PhocServer *self = PHOC_SERVER (data);

#ifdef PHOC_XWAYLAND
  struct wlr_xwayland *xwayland = self->desktop->xwayland;
  if (xwayland && global == xwayland->shell_v1->global)
    return xwayland->server && client == xwayland->server->client;
#endif

  /* Clients with a security context can request privileged protocols */
  if (phoc_desktop_is_privileged_protocol (self->desktop, global) &&
      phoc_server_client_has_security_context (self, client)) {
    return false;
  }

  return true;
}


static gboolean
phoc_server_initable_init (GInitable    *initable,
                           GCancellable *cancellable,
                           GError      **error)
{
  PhocServer *self = PHOC_SERVER (initable);
  struct wlr_renderer *wlr_renderer;

  self->wl_display = wl_display_create ();
  if (self->wl_display == NULL) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "Could not create wayland display");
    return FALSE;
  }
  wl_display_set_global_filter (self->wl_display, phoc_server_filter_globals, self);

  self->backend = wlr_backend_autocreate (self->wl_display, &self->session);
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
  wlr_renderer_init_wl_shm (wlr_renderer, self->wl_display);

  if (wlr_renderer_get_dmabuf_texture_formats (wlr_renderer)) {
    wlr_drm_create (self->wl_display, wlr_renderer);
    self->linux_dmabuf_v1 = wlr_linux_dmabuf_v1_create_with_renderer (self->wl_display,
                                                                      PHOC_LINUX_DMABUF_VERSION,
                                                                      wlr_renderer);
  } else {
    g_message ("Linux dmabuf support unavailable");
  }

  self->data_device_manager = wlr_data_device_manager_create (self->wl_display);

  self->compositor = wlr_compositor_create (self->wl_display, PHOC_WL_DISPLAY_VERSION, wlr_renderer);
  self->subcompositor = wlr_subcompositor_create (self->wl_display);

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
    wlr_backend_destroy (self->backend);
    self->backend = NULL;
  }

  g_clear_object (&self->renderer);

  G_OBJECT_CLASS (phoc_server_parent_class)->dispose (object);
}

static void
phoc_server_finalize (GObject *object)
{
  PhocServer *self = PHOC_SERVER (object);

  g_clear_pointer (&self->dt_compatibles, g_strfreev);
  g_clear_handle_id (&self->wl_source, g_source_remove);
  g_clear_object (&self->input);
  g_clear_object (&self->desktop);
  g_clear_pointer (&self->session_exec, g_free);

  if (self->inited) {
    g_unsetenv("WAYLAND_DISPLAY");
    self->inited = FALSE;
  }

  g_clear_pointer (&self->config, phoc_config_destroy);

  g_clear_pointer (&self->wl_display, wl_display_destroy);

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
  g_autoptr (GError) err = NULL;

  self->dt_compatibles = gm_device_tree_get_compatibles (NULL, &err);
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
 * @exec: The executable to run
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
                   const char *exec, GMainLoop *mainloop,
                   PhocServerFlags flags,
                   PhocServerDebugFlags debug_flags)
{
  g_assert (!self->inited);

  self->config = config;
  self->flags = flags;
  self->debug_flags = debug_flags;
  self->mainloop = mainloop;
  self->exit_status = 1;
  self->desktop = phoc_desktop_new ();
  self->input = phoc_input_new ();
  self->session_exec = g_strdup (exec);
  self->mainloop = mainloop;

  const char *socket = wl_display_add_socket_auto (self->wl_display);
  if (!socket) {
    g_warning("Unable to open wayland socket: %s", strerror(errno));
    wlr_backend_destroy (self->backend);
    return FALSE;
  }

  g_print ("Running compositor on wayland display '%s'\n", socket);

  if (!wlr_backend_start (self->backend)) {
    g_warning("Failed to start backend");
    wlr_backend_destroy (self->backend);
    wl_display_destroy (self->wl_display);
    return FALSE;
  }

  g_setenv("WAYLAND_DISPLAY", socket, true);

  if (self->flags & PHOC_SERVER_FLAG_SHELL_MODE) {
    g_message ("Enabling shell mode");
    g_signal_connect_object (phoc_desktop_get_phosh_private (self->desktop),
                             "notify::shell-state",
                             G_CALLBACK (on_shell_state_changed),
                             self, G_CONNECT_SWAPPED);
    on_shell_state_changed (self, NULL, phoc_desktop_get_phosh_private (self->desktop));
  }

  phoc_wayland_init (self);
  if (self->session_exec)
    phoc_startup_session (self);

  self->inited = TRUE;
  return TRUE;
}

/**
 * phoc_server_get_exit_status:
 * @self: The server
 *
 * Return the session's exit status. This is only meaningful
 * if the session has ended.
 *
 * Returns: The session's exit status.
 */
gint
phoc_server_get_session_exit_status (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->exit_status;
}

/**
 * phoc_server_get_session_exec:
 * @self: The server
 *
 * Return the command that will be run to start the session
 *
 * Returns: The command run at startup
 */
const char *
phoc_server_get_session_exec (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->session_exec;
}

/**
 * phoc_server_get_renderer:
 * @self: The server
 *
 * Gets the renderer object
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
 * @self: The server
 *
 * Get's the desktop singleton
 *
 * Returns: (transfer none): The desktop
 */
PhocDesktop *
phoc_server_get_desktop (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->desktop;
}

/**
 * phoc_server_get_input:
 * @self: The server
 *
 * Get the device handling new input devices and seats.
 *
 * Returns:(transfer none): The input
 */
PhocInput *
phoc_server_get_input (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->input;
}

/**
 * phoc_server_get_config:
 * @self: The server
 *
 * Get the object that has the config file content.
 *
 * Returns:(transfer none): The config
 */
PhocConfig *
phoc_server_get_config (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->config;
}

/**
 * phoc_server_check_debug_flags:
 * @self: The server
 * @check: The flags to check
 *
 * Checks if the given debug flags are set in this server
 *
 * Returns: %TRUE if all of the given flags are set, otherwise %FALSE
 */
gboolean
phoc_server_check_debug_flags (PhocServer *self, PhocServerDebugFlags check)
{
  g_assert (PHOC_IS_SERVER (self));

  return !!(self->debug_flags & check);
}

/**
 * phoc_server_get_last_active_seat:
 * @self: The server
 *
 * Get's the last active seat.
 *
 * Returns: (transfer none): The last active seat.
 */
PhocSeat *
phoc_server_get_last_active_seat (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return phoc_input_get_last_active_seat (self->input);
}


const char * const *
phoc_server_get_compatibles (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return (const char * const *)self->dt_compatibles;
}


struct wl_display *
phoc_server_get_wl_display (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->wl_display;
}


struct wlr_backend *
phoc_server_get_backend (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->backend;
}


struct wlr_compositor *
phoc_server_get_compositor (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->compositor;
}


struct wlr_session *
phoc_server_get_session (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->session;
}


void
phoc_server_set_linux_dmabuf_surface_feedback (PhocServer *self,
                                               PhocView   *view,
                                               PhocOutput *output,
                                               bool        enable)
{
  g_assert (PHOC_IS_SERVER (self));

  if (!self->linux_dmabuf_v1 || !view->wlr_surface)
    return;

  g_assert ((enable && output && output->wlr_output) || (!enable && !output));

  if (enable) {
    struct wlr_linux_dmabuf_feedback_v1 feedback = { 0 };
    const struct wlr_linux_dmabuf_feedback_v1_init_options options = {
      .main_renderer = phoc_renderer_get_wlr_renderer (self->renderer),
      .scanout_primary_output = output->wlr_output,
    };

    if (!wlr_linux_dmabuf_feedback_v1_init_with_options (&feedback, &options))
      return;

    wlr_linux_dmabuf_v1_set_surface_feedback (self->linux_dmabuf_v1, view->wlr_surface, &feedback);
    wlr_linux_dmabuf_feedback_v1_finish (&feedback);
  } else {
    wlr_linux_dmabuf_v1_set_surface_feedback (self->linux_dmabuf_v1, view->wlr_surface, NULL);
  }
}
