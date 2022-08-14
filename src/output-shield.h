/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "output.h"
#include "render.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_OUTPUT_SHIELD (phoc_output_shield_get_type ())

G_DECLARE_FINAL_TYPE (PhocOutputShield, phoc_output_shield, PHOC, OUTPUT_SHIELD, GObject)

PhocOutputShield   *phoc_output_shield_new                       (PhocOutput *output);
void                phoc_output_shield_raise                     (PhocOutputShield *self);
void                phoc_output_shield_lower                     (PhocOutputShield *self);

G_END_DECLS
