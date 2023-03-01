/*
 * Copyright (C) 2022 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "output.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_CUTOUTS_OVERLAY (phoc_cutouts_overlay_get_type ())

G_DECLARE_FINAL_TYPE (PhocCutoutsOverlay, phoc_cutouts_overlay, PHOC, CUTOUTS_OVERLAY, GObject)

PhocCutoutsOverlay *phoc_cutouts_overlay_new                 (const char * const *compatibles);
struct wlr_texture *phoc_cutouts_overlay_get_cutouts_texture (PhocCutoutsOverlay *self,
                                                              PhocOutput         *output);

G_END_DECLS
