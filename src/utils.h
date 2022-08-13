#pragma once

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

void phoc_utils_fix_transform (enum wl_output_transform *transform);
void phoc_utils_rotate_child_position (double *sx, double *sy, double sw, double sh,
                                       double pw, double ph, float rotation);
void phoc_utils_rotated_bounds (struct wlr_box *dest, const struct wlr_box *box, float rotation);
float      phoc_utils_compute_scale         (int32_t phys_width, int32_t phys_height,
                                             int32_t width, int32_t height);

G_END_DECLS
