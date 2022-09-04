/*
 * Copyright (C) 2021 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-xdg-activation-v1"

#include "config.h"
#include "view.h"
#include "phosh-private.h"
#include "server.h"
#include "xdg-activation-v1.h"

#include <wlr/types/wlr_xdg_activation_v1.h>


void
phoc_xdg_activation_v1_handle_request_activate (struct wl_listener *listener,
                                                void               *data)
{
  PhocServer *server = phoc_server_get_default ();
  const struct wlr_xdg_activation_v1_request_activate_event *event = data;
  const struct wlr_xdg_activation_token_v1 *token = event->token;
  struct wlr_xdg_surface *xdg_surface;
  PhocView *view;

  if (!token) {
    g_warning ("No activation token");
    return;
  }

  g_debug ("%s: %s", __func__, token->token);
  if (!wlr_surface_is_xdg_surface (event->surface)) {
    return;
  }

  xdg_surface = wlr_xdg_surface_from_wlr_surface (event->surface);
  g_assert (xdg_surface);
  view = xdg_surface->data;

  if (view == NULL)
    return;

  if (phoc_view_is_mapped (view)) {
     g_debug ("Activating view %p via token '%s'", view, token->token);
      phoc_view_activate (view, true);
      phoc_phosh_private_notify_startup_id (server->desktop->phosh,
                                            token->token,
                                            PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_XDG_ACTIVATION);
  } else {
    g_debug ("Setting view %p via token '%s' as pending activation", view, token->token);
    phoc_view_set_activation_token (view, token->token);
  }
}
