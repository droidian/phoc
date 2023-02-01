/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-animatable"

#include "phoc-config.h"

#include "animatable.h"

/**
 * PhocAnimatable:
 *
 * Something that can be animated
 */
G_DEFINE_INTERFACE (PhocAnimatable, phoc_animatable, G_TYPE_OBJECT)

void
phoc_animatable_default_init (PhocAnimatableInterface *klass)
{
}

/**
 * phoc_animatable_add_frame_callback:
 * @self: the animatable
 * @callback: the frame callback to add
 * @user_data: User data to pass to the callback
 * @notify: How to free the user data
 *
 * Adds a callback to be called before each frame. Until the frame
 * callback is removed, it will be at the frame rate of the
 * output. The frame callback does not automatically imply any
 * repaint. You need to damage the areas you want repainted.
 *
 * Returns: An id for the connection of this callback. Suitable to pass to
 *  phoc_animatable_remove_frame_callback() or %0 if the callback can't
 *  be attached for some reason.
 */
guint
phoc_animatable_add_frame_callback (PhocAnimatable    *self,
                                    PhocFrameCallback  callback,
                                    gpointer          user_data,
                                    GDestroyNotify    notify)
{
  PhocAnimatableInterface *iface;

  g_assert (PHOC_IS_ANIMATABLE (self));

  iface = PHOC_ANIMATABLE_GET_IFACE (self);
  g_assert (iface->add_frame_callback != NULL);

  return iface->add_frame_callback (self, callback, user_data, notify);
}

void
phoc_animatable_remove_frame_callback (PhocAnimatable    *self,
                                       guint              id)
{
  PhocAnimatableInterface *iface;

  g_assert (PHOC_IS_ANIMATABLE (self));

  iface = PHOC_ANIMATABLE_GET_IFACE (self);
  g_assert (iface->remove_frame_callback != NULL);

  return iface->remove_frame_callback (self, id);
}
