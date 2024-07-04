/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "view.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_VIEW_DECO (phoc_view_deco_get_type ())

G_DECLARE_FINAL_TYPE (PhocViewDeco, phoc_view_deco, PHOC, VIEW_DECO, GObject)

PhocViewDeco           *phoc_view_deco_new                       (PhocView *view);
PhocViewDecoPart        phoc_view_deco_get_part                  (PhocViewDeco *self, double sx, double sy);


G_END_DECLS
