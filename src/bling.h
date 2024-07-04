/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "phoc-types.h"
#include "render.h"

#include <glib-object.h>

#pragma once

G_BEGIN_DECLS

#define PHOC_TYPE_BLING (phoc_bling_get_type())

G_DECLARE_INTERFACE (PhocBling, phoc_bling, PHOC, BLING, GObject)

struct _PhocBlingInterface
{
  GTypeInterface parent_iface;

  /**
   * PhocBlingInterface::get_box:
   * @self: A bilng
   *
   * Get a minimal box in layout coordindates that contains the bling
   *
   * Returns: The box
   */
  PhocBox       (*get_box)    (PhocBling *self);
  /**
   * PhocBlingInterface::render:
   * @self: A bling
   *
   * Render the bling. Scissoring is handled by the renderer prior to invoking
   * this function.
   */
  void          (*render)     (PhocBling *self, PhocRenderContext *ctx);
  /**
   * PhocBlingInterface::map:
   * @self: A bling
   *
   * Map the bling so it can be rendered.
   */
  void          (*map)        (PhocBling *self);
  /**
   * PhocBlingInterface::unmap:
   * @self: A bling
   *
   * Umap the bling so it's not rendered anymore
   */
  void          (*unmap)      (PhocBling *self);
  /**
   * PhocBlingInterface::is_mapped:
   * @self: A bling
   *
   * Check whether the bling is mapped.
   *
   * Returns: %TRUE if the bling is mapped, otherwise %FALSE.
   */
   gboolean      (*is_mapped)  (PhocBling *self);
};

void                    phoc_bling_render                        (PhocBling    *self,
                                                                  PhocRenderContext *ctx);
PhocBox                 phoc_bling_get_box                       (PhocBling    *self);
void                    phoc_bling_map                           (PhocBling    *self);
void                    phoc_bling_unmap                         (PhocBling    *self);
gboolean                phoc_bling_is_mapped                     (PhocBling    *self);

G_END_DECLS
