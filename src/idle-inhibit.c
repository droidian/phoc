/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "phoc-idle-inhibit"

#include "phoc-config.h"

#include "idle-inhibit.h"
#include "server.h"

#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>

#include <gio/gio.h>
#include <glib/gi18n.h>

#define SCREENSAVER_BUS_NAME  "org.freedesktop.ScreenSaver"

/**
 * PhocIdleInhibit:
 *
 * Forward idle inhibit to gnome-session
 */
struct _PhocIdleInhibit {
  struct wlr_idle_inhibit_manager_v1 *wlr_idle_inhibit;
  struct wlr_idle                    *wlr_idle;

  struct wl_listener                  new_idle_inhibitor_v1;

  GSList                             *inhibitors_v1;

  GDBusProxy                         *screensaver_proxy;
  GCancellable                       *cancellable;
};

typedef struct _PhocIdleInhibitorV1 {
  PhocIdleInhibit              *idle_inhibit;
  PhocView                     *view;
  guint                         cookie;

  struct wlr_idle_inhibitor_v1 *wlr_inhibitor;

  struct wl_listener            inhibitor_v1_destroy;
} PhocIdleInhibitorV1;


static void
on_screensaver_inhibit_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
  PhocIdleInhibitorV1 *inhibitor = user_data;
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GError) err = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &err);
  if (ret == NULL) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to inhibit " SCREENSAVER_BUS_NAME ": %s", err->message);
    return;
  }

  g_variant_get (ret, "(u)", &inhibitor->cookie);
  g_debug ("Inhibit " SCREENSAVER_BUS_NAME " (%p), cookie = %u", inhibitor, inhibitor->cookie);
}


static void
screensaver_idle_inhibit (PhocIdleInhibit *self, PhocIdleInhibitorV1 *inhibitor, PhocView *view)
{
  const char *app_id = NULL;

  if (!self->screensaver_proxy)
    return;

  if (view)
    app_id = phoc_view_get_app_id (view);

  if (app_id == NULL)
    app_id = PHOC_APP_ID;

  g_dbus_proxy_call (self->screensaver_proxy,
                     "Inhibit",
                     g_variant_new ("(ss)", app_id, _("Inhibiting idle session")),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     on_screensaver_inhibit_finish,
                     inhibitor);
}


static void
on_screensaver_idle_uninhibit_finish (GObject *source, GAsyncResult *res, gpointer data)
{
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GError) err = NULL;
  guint cookie = GPOINTER_TO_UINT (data);

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &err);
  if (ret == NULL) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to " SCREENSAVER_BUS_NAME " uninhibit: %s", err->message);
    return;
  }

  g_debug ("Uninhibit " SCREENSAVER_BUS_NAME " cookie = %u", cookie);
}



static void
screensaver_idle_uninhibit (PhocIdleInhibit *self, PhocIdleInhibitorV1 *inhibitor)
{
  if (self->screensaver_proxy == NULL)
    return;

  if (inhibitor->cookie == 0)
    return;

  g_dbus_proxy_call (self->screensaver_proxy,
                     "UnInhibit",
                     g_variant_new ("(u)", inhibitor->cookie),
                     G_DBUS_CALL_FLAGS_NO_AUTO_START,
                     -1,
                     self->cancellable,
                     on_screensaver_idle_uninhibit_finish,
                     GUINT_TO_POINTER (inhibitor->cookie));
  inhibitor->cookie = 0;
}


static void
phoc_idle_inhibit_destroy_inhibitor_v1 (PhocIdleInhibit *self, PhocIdleInhibitorV1 *inhibitor)
{
  screensaver_idle_uninhibit (self, inhibitor);

  /* We go away but view sticks around so disconnect signals */
  if (inhibitor->view)
    g_signal_handlers_disconnect_by_data (inhibitor->view, inhibitor);

  self->inhibitors_v1 = g_slist_remove (self->inhibitors_v1, inhibitor);
  wl_list_remove (&inhibitor->inhibitor_v1_destroy.link);

  g_free (inhibitor);
}


static void
handle_inhibitor_v1_destroy (struct wl_listener *listener, void *data)
{
  PhocIdleInhibitorV1 *inhibitor = wl_container_of (listener, inhibitor, inhibitor_v1_destroy);

  g_debug ("Idle inhibitor v1 (%p) destroyed", inhibitor);
  phoc_idle_inhibit_destroy_inhibitor_v1 (inhibitor->idle_inhibit, inhibitor);
}


static void
on_view_mapped_changed (PhocIdleInhibitorV1 *inhibitor, GParamSpec *pspec, PhocView *view)
{
  g_assert (PHOC_IS_VIEW (view));

  if (phoc_view_is_mapped (view))
    screensaver_idle_inhibit (inhibitor->idle_inhibit, inhibitor, view);
  else
    screensaver_idle_uninhibit (inhibitor->idle_inhibit, inhibitor);
}


static void
on_surface_destroy (PhocIdleInhibitorV1 *inhibitor, PhocView *view)
{
  g_assert (PHOC_IS_VIEW (view));

  screensaver_idle_uninhibit (inhibitor->idle_inhibit, inhibitor);

  inhibitor->view = NULL;
}


static void
handle_idle_inhibitor_v1 (struct wl_listener *listener, void *data)
{
  struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
  PhocIdleInhibit *self = wl_container_of (listener, self, new_idle_inhibitor_v1);
  PhocIdleInhibitorV1 *inhibitor = g_new0 (PhocIdleInhibitorV1, 1);

  g_debug ("New idle inhibitor v1 (%p)", inhibitor);

  inhibitor->idle_inhibit = self;
  inhibitor->wlr_inhibitor = wlr_inhibitor;

  self->inhibitors_v1 = g_slist_prepend (self->inhibitors_v1, inhibitor);

  inhibitor->inhibitor_v1_destroy.notify = handle_inhibitor_v1_destroy;
  wl_signal_add (&wlr_inhibitor->events.destroy, &inhibitor->inhibitor_v1_destroy);

  inhibitor->view = phoc_view_from_wlr_surface (wlr_inhibitor->surface);
  if (inhibitor->view) {
    g_signal_connect_swapped (inhibitor->view,
                              "notify::mapped",
                              G_CALLBACK (on_view_mapped_changed),
                              inhibitor);
    on_view_mapped_changed (inhibitor, NULL, inhibitor->view);

    g_signal_connect_swapped (inhibitor->view,
                              "surface-destroy",
                              G_CALLBACK (on_surface_destroy),
                              inhibitor);
  } else {
    /* Inhibit when we can't find a matching view */
    screensaver_idle_inhibit (self, inhibitor, NULL);
  }
}


static void
on_proxy_new_for_bus_finish (GObject *object, GAsyncResult *res, gpointer data)
{
  PhocIdleInhibit *self = data;
  g_autoptr (GError) err = NULL;
  PhocServer *server = phoc_server_get_default ();
  GDBusProxy *screensaver_proxy;

  screensaver_proxy = g_dbus_proxy_new_for_bus_finish (res, &err);
  if (screensaver_proxy == NULL) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to get screensaver session proxy: %s", err->message);
    return;
  }

  g_info ("Found " SCREENSAVER_BUS_NAME " interface");

  self->screensaver_proxy = screensaver_proxy;
  /* We connected to DBus so let's expose zwp_idle_inhibit_manager_v1 */
  self->wlr_idle_inhibit = wlr_idle_inhibit_v1_create (server->wl_display);
  if (!self->wlr_idle_inhibit) {
    g_clear_object (&self->screensaver_proxy);
    return;
  }

  self->new_idle_inhibitor_v1.notify = handle_idle_inhibitor_v1;
  wl_signal_add (&self->wlr_idle_inhibit->events.new_inhibitor, &self->new_idle_inhibitor_v1);
}


PhocIdleInhibit *
phoc_idle_inhibit_create (struct wlr_idle   *idle)
{
  PhocIdleInhibit *self = g_new0 (PhocIdleInhibit, 1);

  g_info ("Initializing idle inhibit interface");
  self->wlr_idle = idle;
  wl_list_init (&self->new_idle_inhibitor_v1.link);

  self->cancellable = g_cancellable_new ();
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            SCREENSAVER_BUS_NAME,
                            "/org/freedesktop/ScreenSaver",
                            "org.freedesktop.ScreenSaver",
                            self->cancellable,
                            on_proxy_new_for_bus_finish,
                            self);

  return self;
}


void
phoc_idle_inhibit_destroy (PhocIdleInhibit *self)
{
  wl_list_remove (&self->new_idle_inhibitor_v1.link);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  for (GSList *l = self->inhibitors_v1; l; l = l->next)
    phoc_idle_inhibit_destroy_inhibitor_v1 (self, l->data);
  g_slist_free (self->inhibitors_v1);

  g_clear_object (&self->screensaver_proxy);

  g_free (self);
}
