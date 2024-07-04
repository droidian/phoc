/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view-child-private.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_SUBSURFACE (phoc_subsurface_get_type ())

G_DECLARE_FINAL_TYPE (PhocSubsurface, phoc_subsurface, PHOC, SUBSURFACE, PhocViewChild)

PhocSubsurface *phoc_subsurface_new (PhocView *view, struct wlr_subsurface *wlr_subsurface);

G_END_DECLS
