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
 * PHOC_PRIV_CONTAINER_P:
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
double     phoc_ease_in_cubic               (double t);
double     phoc_ease_out_cubic              (double t);

G_END_DECLS
