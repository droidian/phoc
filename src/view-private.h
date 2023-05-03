/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view.h"

G_BEGIN_DECLS

/* Functions to be used by derived classes only */
void             view_set_title                      (PhocView *view, const char *title);
void             view_set_parent                     (PhocView *view, PhocView *parent);
void             view_setup                          (PhocView *view);
void             view_send_frame_done_if_not_visible (PhocView *view);
void             view_update_position                (PhocView *view, int x, int y);
void             view_update_size                    (PhocView *view, int width, int height);
void             view_initial_focus                  (PhocView *view);
void             phoc_view_map                       (PhocView *view, struct wlr_surface *surface);
void             view_unmap                          (PhocView *view);
void             phoc_view_apply_damage              (PhocView *view);

G_END_DECLS

