/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "animatable.h"
#include "property-easer.h"
#include "phoc-anim-enums.h"
#include "phoc-anim-enum-types.h"
#include "utils.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_TIMED_ANIMATION (phoc_timed_animation_get_type ())

G_DECLARE_FINAL_TYPE (PhocTimedAnimation, phoc_timed_animation, PHOC, TIMED_ANIMATION, GObject)

PhocTimedAnimation   *phoc_timed_animation_new              (void);
PhocAnimatable       *phoc_timed_animation_get_animatable   (PhocTimedAnimation *self);
void                  phoc_timed_animation_set_property_easer (PhocTimedAnimation *self,
                                                             PhocPropertyEaser  *prop_easer);
PhocPropertyEaser    *phoc_timed_animation_get_property_easer (PhocTimedAnimation *self);
void                  phoc_timed_animation_set_duration     (PhocTimedAnimation *self, int duration);
int                   phoc_timed_animation_get_duration     (PhocTimedAnimation *self);
PhocAnimationState    phoc_timed_animation_get_state        (PhocTimedAnimation *self);
void                  phoc_timed_animation_play             (PhocTimedAnimation *self);
void                  phoc_timed_animation_skip             (PhocTimedAnimation *self);
void                  phoc_timed_animation_reset            (PhocTimedAnimation *self);

G_END_DECLS
