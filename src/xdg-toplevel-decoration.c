#define G_LOG_DOMAIN "phoc-xdg-toplevel-decoration"

#include "phoc-config.h"
#include "xdg-surface-private.h"
#include "xdg-toplevel-decoration.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "xdg-surface.h"
#include "desktop.h"
#include "server.h"
#include "utils.h"


typedef struct _PhocXdgToplevelDecoration {
  struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
  PhocXdgSurface *surface;
  struct wl_listener destroy;
  struct wl_listener request_mode;
  struct wl_listener surface_commit;
} PhocXdgToplevelDecoration;


static void
decoration_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocXdgToplevelDecoration *decoration = wl_container_of (listener, decoration, destroy);

  g_debug ("Destroy xdg toplevel decoration %p", decoration);

  if (decoration->surface) {
    phoc_xdg_surface_set_decoration (decoration->surface, NULL);
    phoc_view_set_decorated (PHOC_VIEW (decoration->surface), FALSE);
    g_signal_handlers_disconnect_by_data (decoration->surface, decoration);
  }
  wl_list_remove (&decoration->destroy.link);
  wl_list_remove (&decoration->request_mode.link);
  wl_list_remove (&decoration->surface_commit.link);

  free (decoration);
}

static void
decoration_handle_request_mode (struct wl_listener *listener, void *data)
{
  PhocXdgToplevelDecoration *decoration = wl_container_of (listener, decoration, request_mode);

  enum wlr_xdg_toplevel_decoration_v1_mode mode = decoration->wlr_decoration->requested_mode;

  if (mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) {
    mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
  }
  wlr_xdg_toplevel_decoration_v1_set_mode (decoration->wlr_decoration, mode);
}

static void
decoration_handle_surface_commit (struct wl_listener *listener, void *data)
{
  PhocXdgToplevelDecoration *decoration = wl_container_of (listener, decoration, surface_commit);

  bool decorated = decoration->wlr_decoration->current.mode ==
    WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
  phoc_view_set_decorated (PHOC_VIEW (decoration->surface), decorated);
}


static void
on_xdg_surface_destroy (PhocXdgSurface *surface, PhocXdgToplevelDecoration *decoration)
{
  g_assert (PHOC_IS_XDG_SURFACE (surface));

  decoration->surface = NULL;
}


void
phoc_handle_xdg_toplevel_decoration (struct wl_listener *listener, void *data)
{
  struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;
  PhocXdgSurface *xdg_surface = PHOC_XDG_SURFACE (wlr_decoration->toplevel->base->data);
  g_assert (xdg_surface != NULL);
  struct wlr_xdg_surface *wlr_xdg_surface = phoc_xdg_surface_get_wlr_xdg_surface (xdg_surface);
  PhocXdgToplevelDecoration *decoration = g_new0 (PhocXdgToplevelDecoration, 1);

  g_debug ("New xdg toplevel decoration %p", decoration);

  decoration->wlr_decoration = wlr_decoration;
  decoration->surface = xdg_surface;
  phoc_xdg_surface_set_decoration (xdg_surface, decoration);

  decoration->destroy.notify = decoration_handle_destroy;
  wl_signal_add (&wlr_decoration->events.destroy, &decoration->destroy);

  decoration->request_mode.notify = decoration_handle_request_mode;
  wl_signal_add (&wlr_decoration->events.request_mode, &decoration->request_mode);

  decoration->surface_commit.notify = decoration_handle_surface_commit;
  wl_signal_add (&wlr_xdg_surface->surface->events.commit, &decoration->surface_commit);

  g_signal_connect (xdg_surface, "surface-destroy", G_CALLBACK (on_xdg_surface_destroy), decoration);

  decoration_handle_request_mode (&decoration->request_mode, wlr_decoration);
}
