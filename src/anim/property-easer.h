/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "easing.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_PROPERTY_EASER (phoc_property_easer_get_type ())

G_DECLARE_FINAL_TYPE (PhocPropertyEaser, phoc_property_easer, PHOC, PROPERTY_EASER, GObject)

PhocPropertyEaser    *phoc_property_easer_new              (GObject            *target);
void                  phoc_property_easer_set_progress     (PhocPropertyEaser  *self,
                                                            float               progress);
float                 phoc_property_easer_get_progress     (PhocPropertyEaser  *self);
void                  phoc_property_easer_set_easing       (PhocPropertyEaser  *self,
                                                            PhocEasing          easing);
PhocEasing            phoc_property_easer_get_easing       (PhocPropertyEaser  *self);
guint                 phoc_property_easer_set_props        (PhocPropertyEaser  *self,
                                                            const gchar        *first_property_name,
                                                            ...) G_GNUC_NULL_TERMINATED;

G_END_DECLS
