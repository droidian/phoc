/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view.h"

G_BEGIN_DECLS

/* Functions to be used by derived classes only */
void             view_set_title                      (PhocView *self, const char *title);
void             view_set_parent                     (PhocView *self, PhocView *parent);
void             phoc_view_setup                     (PhocView *self);
void             view_send_frame_done_if_not_visible (PhocView *self);
void             view_update_position                (PhocView *self, int x, int y);
void             view_update_size                    (PhocView *self, int width, int height);
void             phoc_view_set_initial_focus         (PhocView *self);
void             phoc_view_map                       (PhocView *self, struct wlr_surface *surface);
void             phoc_view_unmap                     (PhocView *self);
void             phoc_view_apply_damage              (PhocView *self);

G_END_DECLS
