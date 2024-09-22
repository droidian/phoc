/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "layer-shell.h"

G_BEGIN_DECLS

PhocLayerPopup      *phoc_layer_popup_create (struct wlr_xdg_popup *wlr_popup);
void                 phoc_layer_popup_unconstrain (PhocLayerPopup *popup);

PhocLayerSubsurface *phoc_layer_subsurface_create (struct wlr_subsurface *wlr_subsurface);
void                 phoc_layer_subsurface_destroy (PhocLayerSubsurface *subsurface);

void                 phoc_layer_shell_update_cursors (PhocLayerSurface *layer_surface,
                                                      GSList           *seats);

G_END_DECLS
