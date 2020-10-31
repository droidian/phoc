/*
 * Copyright (C) 2019 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_switch.h>
#include <gmodule.h>
#include "keybindings.h"

#define PHOSH_PRIVATE_XDG_SWITCHER_SINCE_VERSION 2

struct phosh_private {
  struct wl_resource* resource;
  struct wl_global *global;
  GList *keyboard_events;
  guint last_action_id;

  PhocDesktop *desktop;
  struct {
    struct wl_listener layer_shell_new_surface;
    struct wl_listener panel_surface_destroy;
  } listeners;
  struct wlr_layer_surface_v1 *panel;
};

struct phosh_private_screencopy_frame {
  struct wl_resource *resource, *toplevel;
  struct phosh_private *phosh;
  struct wl_listener view_destroy;

  enum wl_shm_format format;
  uint32_t width;
  uint32_t height;
  uint32_t stride;

  struct wl_shm_buffer *buffer;
  struct roots_view *view;
};

struct phosh_private* phosh_create(PhocDesktop *desktop,
				   struct wl_display *display);
void phosh_destroy(struct phosh_private *shell);
struct phosh_private *phosh_private_from_resource(struct wl_resource *resource);
struct phosh_private_xdg_switcher *phosh_private_xdg_switcher_from_resource(struct wl_resource *resource);
struct phosh_private_screencopy_frame *phosh_private_screencopy_frame_from_resource(struct wl_resource *resource);

bool phosh_forward_keysym (PhocKeyCombo *combo, uint32_t timestamp);
bool phosh_forward_switch_event (struct wlr_event_switch_toggle *event);
