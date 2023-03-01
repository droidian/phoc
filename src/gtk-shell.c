/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-gtk-shell"

#include "phoc-config.h"

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
 * Implement enough to raise windows for GTK based applications
 * and other bits needed by gtk when the protocol is bound.
 */
struct _PhocGtkShell {
  struct wl_global *global;
  GSList *resources;
  GSList *surfaces;
};

struct _PhocGtkSurface {
  struct wl_resource *resource;
  struct wlr_surface *wlr_surface;
  struct wlr_xdg_surface *xdg_surface;
  PhocGtkShell *gtk_shell;
  char *app_id;

  struct wl_listener wlr_surface_handle_destroy;
  struct wl_listener xdg_surface_handle_destroy;
  struct wl_listener xdg_surface_handle_configure;

  struct {
    struct wl_signal destroy;
  } events;
};

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
  PhocGtkSurface *gtk_surface = phoc_gtk_surface_from_resource (resource);
  PhocView *view;

  g_debug ("Setting app-id %s for surface %p (res %p)", application_id, gtk_surface->wlr_surface, resource);
  if (!gtk_surface->wlr_surface)
    return;

  g_free (gtk_surface->app_id);
  gtk_surface->app_id = g_strdup (application_id);

  view = phoc_view_from_wlr_surface (gtk_surface->wlr_surface);
  if (view)
    phoc_view_set_app_id (view, application_id);
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
  PhocGtkSurface *gtk_surface = phoc_gtk_surface_from_resource (resource);
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
  PhocGtkSurface *gtk_surface = phoc_gtk_surface_from_resource (resource);

  g_debug ("Destroying gtk_surface %p (res %p)", gtk_surface,
           gtk_surface->resource);
  if (gtk_surface->wlr_surface) {
    wl_list_remove(&gtk_surface->wlr_surface_handle_destroy.link);
    gtk_surface->wlr_surface = NULL;
  }

  if (gtk_surface->xdg_surface) {
    wl_list_remove(&gtk_surface->xdg_surface_handle_destroy.link);
    wl_list_remove (&gtk_surface->xdg_surface_handle_configure.link);
    gtk_surface->xdg_surface = NULL;
  }

  gtk_surface->gtk_shell->surfaces = g_slist_remove (gtk_surface->gtk_shell->surfaces,
                                                     gtk_surface);
  g_free (gtk_surface->app_id);
  g_free (gtk_surface);
}

static void
handle_wlr_surface_handle_destroy (struct wl_listener *listener,
                                   void               *data)
{
  PhocGtkSurface *gtk_surface =
    wl_container_of(listener, gtk_surface, wlr_surface_handle_destroy);

  /* Make sure we don't try to raise an already gone surface */
  gtk_surface->wlr_surface = NULL;
}


static void
handle_xdg_surface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocGtkSurface *gtk_surface = wl_container_of(listener, gtk_surface, xdg_surface_handle_destroy);

  /* Make sure we don't try to configure an already gone xdg surface */
  gtk_surface->xdg_surface = NULL;
}


static void
send_configure_edges (PhocGtkSurface *gtk_surface, PhocView *view)
{
  uint32_t *val;
  struct wl_array edge_states;

  wl_array_init (&edge_states);

  if (view_is_floating (view)) {
    val = wl_array_add (&edge_states, sizeof *val);
    *val = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_TOP;
    val = wl_array_add (&edge_states, sizeof *val);
    *val = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_RIGHT;
    val = wl_array_add (&edge_states, sizeof *val);
    *val = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_BOTTOM;
    val = wl_array_add (&edge_states, sizeof *val);
    *val = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_LEFT;
  } else if (view_is_tiled (view)) {
    PhocViewTileDirection dirs = phoc_view_get_tile_direction (view);

    if (dirs & PHOC_VIEW_TILE_LEFT) {
      val = wl_array_add (&edge_states, sizeof *val);
      *val = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_RIGHT;
    }
    if (dirs & PHOC_VIEW_TILE_RIGHT) {
      val = wl_array_add (&edge_states, sizeof *val);
      *val = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_LEFT;
    }
    /* TODO: top and bottom once implemented */
  }

  gtk_surface1_send_configure_edges (gtk_surface->resource, &edge_states);

  wl_array_release (&edge_states);
}


static void
send_configure (PhocGtkSurface *gtk_surface)
{
  struct wl_array states;
  PhocView *view;
  int version;

  if (gtk_surface->xdg_surface == NULL)
    return;

  view = phoc_view_from_wlr_surface (gtk_surface->wlr_surface);
  if (view == NULL)
    return;

  g_assert (PHOC_IS_VIEW (view));

  wl_array_init (&states);
  version = wl_resource_get_version (gtk_surface->resource);

  if (view_is_tiled (view)) {
    uint32_t *val;

    if (version < GTK_SURFACE1_CONFIGURE_EDGES_SINCE_VERSION) {
      val = wl_array_add (&states, sizeof *val);
      *val = GTK_SURFACE1_STATE_TILED;
    } else {
      PhocViewTileDirection dirs = phoc_view_get_tile_direction (view);

      if (version >= GTK_SURFACE1_STATE_TILED_LEFT_SINCE_VERSION && dirs & PHOC_VIEW_TILE_LEFT) {
        val = wl_array_add (&states, sizeof *val);
        *val = GTK_SURFACE1_STATE_TILED_LEFT;
      }

      if (version >= GTK_SURFACE1_STATE_TILED_RIGHT_SINCE_VERSION && dirs & PHOC_VIEW_TILE_RIGHT) {
        val = wl_array_add (&states, sizeof *val);
        *val = GTK_SURFACE1_STATE_TILED_RIGHT;
      }

      /* TODO: top and bottom once implemented */
      if (version >= GTK_SURFACE1_STATE_TILED_TOP_SINCE_VERSION) {
        val = wl_array_add (&states, sizeof *val);
        *val = GTK_SURFACE1_STATE_TILED_TOP;
      }

      if (version >= GTK_SURFACE1_STATE_TILED_BOTTOM_SINCE_VERSION) {
        val = wl_array_add (&states, sizeof *val);
        *val = GTK_SURFACE1_STATE_TILED_BOTTOM;
      }
    }
  }

  gtk_surface1_send_configure (gtk_surface->resource, &states);
  wl_array_release (&states);

  if (wl_resource_get_version (gtk_surface->resource) >= GTK_SURFACE1_CONFIGURE_EDGES_SINCE_VERSION)
    send_configure_edges (gtk_surface, view);
}


static void
handle_xdg_surface_handle_configure(struct wl_listener *listener,
                                    void               *data)
{
  PhocGtkSurface *gtk_surface = wl_container_of(listener, gtk_surface, xdg_surface_handle_configure);

  send_configure (gtk_surface);
}


static void
handle_get_gtk_surface(struct wl_client *client,
                       struct wl_resource *gtk_shell_resource,
                       uint32_t id,
                       struct wl_resource *surface_resource)
{
  struct wlr_surface *wlr_surface = wlr_surface_from_resource (surface_resource);
  PhocGtkSurface *gtk_surface;

  gtk_surface = g_new0 (PhocGtkSurface, 1);

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

  gtk_surface->wlr_surface_handle_destroy.notify = handle_wlr_surface_handle_destroy;
  wl_signal_add(&wlr_surface->events.destroy, &gtk_surface->wlr_surface_handle_destroy);

  if (wlr_surface_is_xdg_surface (wlr_surface)) {
    gtk_surface->xdg_surface = wlr_xdg_surface_from_wlr_surface (gtk_surface->wlr_surface);

    gtk_surface->xdg_surface_handle_destroy.notify = handle_xdg_surface_handle_destroy;
    wl_signal_add(&gtk_surface->xdg_surface->events.destroy, &gtk_surface->xdg_surface_handle_destroy);

    gtk_surface->xdg_surface_handle_configure.notify = handle_xdg_surface_handle_configure;
    wl_signal_add(&gtk_surface->xdg_surface->events.configure, &gtk_surface->xdg_surface_handle_configure);
  }

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
    wlr_xdg_activation_v1_add_token (server->desktop->xdg_activation_v1, startup_id);
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
  g_return_val_if_fail (self, NULL);

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
phoc_gtk_surface_from_resource (struct wl_resource *resource)
{
  g_assert(wl_resource_instance_of (resource, &gtk_surface1_interface,
                                    &gtk_surface1_impl));
  return wl_resource_get_user_data (resource);
}


const char *
phoc_gtk_surface_get_app_id (PhocGtkSurface *gtk_surface)
{
  return gtk_surface->app_id;
}
