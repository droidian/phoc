/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop.h"

#include <glib-object.h>

#pragma once

G_BEGIN_DECLS

typedef struct _PhocIdleInhibit PhocIdleInhibit;

PhocIdleInhibit *phoc_idle_inhibit_create  (struct wlr_idle   *idle);
void             phoc_idle_inhibit_destroy (PhocIdleInhibit *self);

G_END_DECLS
