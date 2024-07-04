/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "xdg-surface.h"

#include <wlr/types/wlr_compositor.h>

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _PhocXdgToplevelDecoration PhocXdgToplevelDecoration;

void               phoc_handle_xdg_toplevel_decoration (struct wl_listener *listener, void *data);

G_END_DECLS
