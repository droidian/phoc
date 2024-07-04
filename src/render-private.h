/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "render.h"

#include <wlr/types/wlr_output.h>

G_BEGIN_DECLS

struct wlr_renderer  *phoc_renderer_get_wlr_renderer  (PhocRenderer *self);
struct wlr_allocator *phoc_renderer_get_wlr_allocator (PhocRenderer *self);

G_END_DECLS
