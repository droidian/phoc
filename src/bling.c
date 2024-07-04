/* Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-bling"

#include "bling.h"

/**
 * PhocBling:
 *
 * A #PhocBling is additional render bling such as rectangles or other
 * elements to be drawn by the compositor.
 *
 * Blings are currently meant to be attached to [type@View] but can be
 * extended to other objects.
 */

G_DEFINE_INTERFACE (PhocBling, phoc_bling, G_TYPE_OBJECT)


static void
phoc_bling_default_init (PhocBlingInterface *iface)
{
}


void
phoc_bling_render (PhocBling *self, PhocRenderContext *ctx)
{
  PhocBlingInterface *iface;

  g_assert (PHOC_IS_BLING (self));

  iface = PHOC_BLING_GET_IFACE (self);
  g_assert (iface->render);

  iface->render (self, ctx);
}


PhocBox
phoc_bling_get_box (PhocBling *self)
{
  PhocBlingInterface *iface;

  g_assert (PHOC_IS_BLING (self));

  iface = PHOC_BLING_GET_IFACE (self);
  g_assert (iface->get_box);

  return iface->get_box (self);
}


void
phoc_bling_map (PhocBling *self)
{
  PhocBlingInterface *iface;

  g_assert (PHOC_IS_BLING (self));

  iface = PHOC_BLING_GET_IFACE (self);
  g_assert (iface->map);

  iface->map (self);
}


void
phoc_bling_unmap (PhocBling *self)
{
  PhocBlingInterface *iface;

  g_assert (PHOC_IS_BLING (self));

  iface = PHOC_BLING_GET_IFACE (self);
  g_assert (iface->unmap);

  iface->unmap (self);
}


gboolean
phoc_bling_is_mapped (PhocBling *self)
{
  PhocBlingInterface *iface;

  g_assert (PHOC_IS_BLING (self));

  iface = PHOC_BLING_GET_IFACE (self);
  g_assert (iface->is_mapped);

  return iface->is_mapped (self);
}
