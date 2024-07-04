/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view.h"
#include "view-child-private.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_XDG_POPUP (phoc_xdg_popup_get_type ())

G_DECLARE_FINAL_TYPE (PhocXdgPopup, phoc_xdg_popup, PHOC, XDG_POPUP, PhocViewChild)

PhocXdgPopup      *phoc_xdg_popup_new              (PhocView             *view,
                                                    struct wlr_xdg_popup *wlr_popup);

G_END_DECLS
