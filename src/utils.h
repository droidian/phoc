#pragma once

#include <wlr/types/wlr_output_layout.h>

void phoc_utils_fix_transform (enum wl_output_transform *transform);
void phoc_utils_rotate_child_position (double *sx, double *sy, double sw, double sh,
                                       double pw, double ph, float rotation);
