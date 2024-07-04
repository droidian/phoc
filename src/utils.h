/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "output.h"

#include <glib.h>
#include <wlr/types/wlr_output_layout.h>

G_BEGIN_DECLS

/**
 * PHOC_PRIV_CONTAINER_P:
 * t: the name of the type in camel case
 * p: The pointer to the private part of an instance
 *
 * Returns an untyped pointer to the instance containing the instance
 * private data @p.
 */
#define PHOC_PRIV_CONTAINER_P(t, p) ((guint8*)p - (t##_private_offset))

/**
 * PHOC_PRIV_CONTAINER:
 * c: cast to the type @t
 * t: the name of the type in camel case
 * p: The pointer to the private part of an instance
 *
 * Returns a pointer to the instance containing the instance private
 * data @p.
 */
#define PHOC_PRIV_CONTAINER(c, t, p)  (c)(PHOC_PRIV_CONTAINER_P(t,p))

void       phoc_utils_fix_transform         (enum wl_output_transform *transform);
float      phoc_utils_compute_scale         (int32_t phys_width, int32_t phys_height,
                                             int32_t width, int32_t height);
void       phoc_utils_scale_box             (struct wlr_box *box, float scale);
gboolean   phoc_utils_is_damaged            (const struct wlr_box    *box,
                                             const pixman_region32_t *damage,
                                             const struct wlr_box    *clip_box,
                                             pixman_region32_t       *out_damage);

void       phoc_utils_wlr_surface_update_scales (struct wlr_surface *surface);
void       phoc_utils_wlr_surface_enter_output  (struct wlr_surface *wlr_surface,
                                                 struct wlr_output  *wlr_output);
void       phoc_utils_wlr_surface_leave_output  (struct wlr_surface *wlr_surface,
                                                 struct wlr_output  *wlr_output);


G_END_DECLS
