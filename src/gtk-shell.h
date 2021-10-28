/*
 * Copyright (C) 2019 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

G_BEGIN_DECLS

typedef struct _PhocGtkShell {
  struct wl_global *global;
  GSList *resources;
  GSList *surfaces;

} PhocGtkShell;

typedef struct PhocGtkSurface {
  struct wl_resource *resource;
  struct wlr_surface *wlr_surface;
  PhocGtkShell *gtk_shell;
  char *app_id;

  struct wl_listener wlr_surface_handle_destroy;

  struct {
    struct wl_signal destroy;
  } events;
} PhocGtkSurface;

PhocGtkShell *phoc_gtk_shell_create(PhocDesktop *desktop,
				    struct wl_display *display);
void phoc_gtk_shell_destroy(PhocGtkShell *gtk_shell);
PhocGtkSurface *phoc_gtk_shell_get_gtk_surface_from_wlr_surface (PhocGtkShell *self, struct wlr_surface *wlr_surface);
PhocGtkShell *phoc_gtk_shell_from_resource(struct wl_resource *resource);
PhocGtkSurface *gtk_surface_from_resource(struct wl_resource *resource);

G_END_DECLS
