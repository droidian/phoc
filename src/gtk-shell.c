/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-gtk-shell"

#include "config.h"

#include "server.h"
#include "cursor.h"
#include "desktop.h"
#include "input.h"
#include "phosh-private.h"
#include "gtk-shell.h"

#include <gtk-shell-protocol.h>
#include <wlr/types/wlr_xdg_activation_v1.h>

/**
 * PhocGtkShell:
 *
 * A minimal implementeation of gtk_shell1 protocol
 *
 * Implement just enough to raise windows for GTK based applications
 * until there's an agreed on upstream protocol.
 */

static void
handle_set_dbus_properties(struct wl_client *client,
                           struct wl_resource *resource,
                           const char *application_id,
                           const char *app_menu_path,
                           const char *menubar_path,
                           const char *window_object_path,
                           const char *application_object_path,
                           const char *unique_bus_name)
{
  PhocGtkSurface *gtk_surface = gtk_surface_from_resource (resource);
  PhocView *view;

  g_debug ("Setting app-id %s for surface %p (res %p)", application_id, gtk_surface->wlr_surface, resource);
  if (!gtk_surface->wlr_surface)
    return;

  g_free (gtk_surface->app_id);
  gtk_surface->app_id = g_strdup (application_id);

  view = phoc_view_from_wlr_surface (gtk_surface->wlr_surface);
  if (view)
    view_set_app_id (view, application_id);
}

static void
handle_set_modal(struct wl_client *client,
                 struct wl_resource *resource)
{
  g_debug ("%s not implemented", __func__);
}

static void
handle_unset_modal(struct wl_client *client,
                    struct wl_resource *resource)
{
  g_debug ("%s not implemented", __func__);
}

static void
handle_present(struct wl_client *client,
               struct wl_resource *resource,
               uint32_t time)
{
  g_debug ("%s not implemented", __func__);
}

static void
handle_request_focus(struct wl_client *client,
                     struct wl_resource *resource,
                     const char *startup_id)
{
  PhocGtkSurface *gtk_surface =
    gtk_surface_from_resource (resource);
  PhocServer *server = phoc_server_get_default ();
  PhocInput *input = server->input;
  PhocSeat *seat = phoc_input_get_last_active_seat (input);
  PhocView *view;

  g_debug ("Requesting focus for surface %p (res %p)", gtk_surface->wlr_surface, resource);
  if (!gtk_surface->wlr_surface)
    return;

  view = phoc_view_from_wlr_surface (gtk_surface->wlr_surface);
  if (view)
    phoc_seat_set_focus(seat, view);
}

static const struct gtk_surface1_interface gtk_surface1_impl = {
  handle_set_dbus_properties,
  handle_set_modal,
  handle_unset_modal,
  handle_present,
  handle_request_focus,
};

static void
gtk_surface_handle_resource_destroy(struct wl_resource *resource)
{
  PhocGtkSurface *gtk_surface =
    gtk_surface_from_resource(resource);

  g_debug ("Destroying gtk_surface %p (res %p)", gtk_surface,
           gtk_surface->resource);
  if (gtk_surface->wlr_surface) {
    wl_list_remove(&gtk_surface->wlr_surface_handle_destroy.link);
    gtk_surface->wlr_surface = NULL;
  }
  gtk_surface->gtk_shell->surfaces = g_slist_remove (gtk_surface->gtk_shell->surfaces,
                                                     gtk_surface);
  g_free (gtk_surface->app_id);
  g_free (gtk_surface);
}

static void handle_wlr_surface_handle_destroy(struct wl_listener *listener,
                                              void *data)
{
  PhocGtkSurface *gtk_surface =
    wl_container_of(listener, gtk_surface, wlr_surface_handle_destroy);

  /* Make sure we don't try to raise an already gone surface */
  gtk_surface->wlr_surface = NULL;
}

static void
handle_get_gtk_surface(struct wl_client *client,
                       struct wl_resource *gtk_shell_resource,
                       uint32_t id,
                       struct wl_resource *surface_resource)
{
  struct wlr_surface *wlr_surface =
    wlr_surface_from_resource (surface_resource);
  PhocGtkSurface *gtk_surface;

  gtk_surface = g_new0 (PhocGtkSurface, 1);
  if (gtk_surface == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  int version = wl_resource_get_version(gtk_shell_resource);
  gtk_surface->gtk_shell = phoc_gtk_shell_from_resource (gtk_shell_resource);
  gtk_surface->resource = wl_resource_create(client,
                                             &gtk_surface1_interface, version, id);
  if (gtk_surface->resource == NULL) {
    g_free (gtk_surface);
    wl_client_post_no_memory(client);
    return;
  }

  g_debug ("New gtk_surface_surface %p (res %p)", gtk_surface,
           gtk_surface->resource);
  wl_resource_set_implementation(gtk_surface->resource,
                                 &gtk_surface1_impl,
                                 gtk_surface,
                                 gtk_surface_handle_resource_destroy);

  gtk_surface->wlr_surface = wlr_surface;
  gtk_surface->wlr_surface_handle_destroy.notify =
    handle_wlr_surface_handle_destroy;

  wl_signal_add(&wlr_surface->events.destroy,
                &gtk_surface->wlr_surface_handle_destroy);

  /* Send empty configure */
  struct wl_array states;
  wl_array_init (&states);
  gtk_surface1_send_configure (gtk_surface->resource, &states);
  wl_array_release (&states);

  wl_signal_init(&gtk_surface->events.destroy);

  gtk_surface->gtk_shell->surfaces = g_slist_prepend (gtk_surface->gtk_shell->surfaces,
                                                      gtk_surface);
}

static void
handle_set_startup_id(struct wl_client *client,
                      struct wl_resource *resource,
                      const char *startup_id)
{
  PhocServer *server = phoc_server_get_default ();
  g_debug ("%s: %s", __func__, startup_id);

  if (startup_id) {
    phoc_phosh_private_notify_startup_id (server->desktop->phosh,
                                          startup_id,
                                          PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_GTK_SHELL);
  }
}

static void
handle_system_bell(struct wl_client *client,
                   struct wl_resource *resource,
                   struct wl_resource *surface)
{
  g_debug ("%s not implemented", __func__);
}

static void
handle_notify_launch(struct wl_client *client,
                     struct wl_resource *resource,
                     const char *startup_id)
{
  PhocServer *server = phoc_server_get_default ();

  g_debug ("%s: %s", __func__, startup_id);
  if (startup_id) {
#ifdef PHOC_HAVE_WLR_XDG_ACTIVATION_V1_ADD_TOKEN
    wlr_xdg_activation_v1_add_token (server->desktop->xdg_activation_v1, startup_id);
#endif
    phoc_phosh_private_notify_launch (server->desktop->phosh,
                                      startup_id,
                                      PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_GTK_SHELL);
  }
}

static void
gtk_shell1_handle_resource_destroy(struct wl_resource *resource)
{
  PhocGtkShell *gtk_shell = phoc_gtk_shell_from_resource (resource);

  g_debug ("Destroying gtk_shell %p (res %p)", gtk_shell, resource);
  gtk_shell->resources = g_slist_remove (gtk_shell->resources,
                                         resource);
}

static const struct gtk_shell1_interface gtk_shell1_impl = {
  handle_get_gtk_surface,
  handle_set_startup_id,
  handle_system_bell,
  handle_notify_launch,
};

static void
gtk_shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
  PhocGtkShell *gtk_shell = data;
  struct wl_resource *resource  = wl_resource_create(client, &gtk_shell1_interface,
                                                     version, id);
  wl_resource_set_implementation(resource,
                                 &gtk_shell1_impl,
                                 gtk_shell,
                                 gtk_shell1_handle_resource_destroy);

  gtk_shell->resources = g_slist_prepend (gtk_shell->resources,
                                          resource);

  gtk_shell1_send_capabilities (resource, 0);
  return;
}

PhocGtkShell*
phoc_gtk_shell_create(PhocDesktop *desktop, struct wl_display *display)
{
  PhocGtkShell *gtk_shell = g_new0 (PhocGtkShell, 1);
  if (!gtk_shell)
    return NULL;

  g_info ("Initializing gtk-shell interface");
  gtk_shell->global = wl_global_create(display, &gtk_shell1_interface, 3, gtk_shell, gtk_shell_bind);

  if (!gtk_shell->global)
    return NULL;

  return gtk_shell;
}

void
phoc_gtk_shell_destroy (PhocGtkShell *gtk_shell)
{
  g_clear_pointer (&gtk_shell->resources, g_slist_free);
  g_clear_pointer (&gtk_shell->surfaces, g_slist_free);
  wl_global_destroy(gtk_shell->global);
  g_free (gtk_shell);
}

PhocGtkSurface *
phoc_gtk_shell_get_gtk_surface_from_wlr_surface (PhocGtkShell *self, struct wlr_surface *wlr_surface)
{
  GSList *item = self->surfaces;
  while (item) {
    PhocGtkSurface *surface = item->data;
    if (surface->wlr_surface == wlr_surface)
      return surface;
    item = item->next;
  }
  return NULL;
}

PhocGtkShell *
phoc_gtk_shell_from_resource (struct wl_resource *resource)
{
  g_assert(wl_resource_instance_of(resource, &gtk_shell1_interface,
                                   &gtk_shell1_impl));
  return wl_resource_get_user_data(resource);
}

PhocGtkSurface *
gtk_surface_from_resource (struct wl_resource *resource)
{
  g_assert(wl_resource_instance_of (resource, &gtk_surface1_interface,
                                    &gtk_surface1_impl));
  return wl_resource_get_user_data (resource);
}
