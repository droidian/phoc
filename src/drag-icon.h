/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include <wlr/types/wlr_data_device.h>

#include <glib.h>

#pragma once

G_BEGIN_DECLS

typedef struct _PhocSeat PhocSeat;
typedef struct _PhocDragIcon PhocDragIcon;

PhocDragIcon *phoc_drag_icon_create (PhocSeat *seat, struct wlr_drag_icon *icon);
gboolean      phoc_drag_icon_is_mapped (PhocDragIcon *self);
double        phoc_drag_icon_get_x (PhocDragIcon *self);
double        phoc_drag_icon_get_y (PhocDragIcon *self);
struct wlr_surface *
              phoc_drag_icon_get_wlr_surface (PhocDragIcon *self);
void          phoc_drag_icon_update_position (PhocDragIcon *self);

G_END_DECLS
