/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "phoc-anim-enums.h"
#include "phoc-anim-enum-types.h"

#include <glib-object.h>

G_BEGIN_DECLS

double phoc_easing_ease (PhocEasing self, double value);

G_END_DECLS
