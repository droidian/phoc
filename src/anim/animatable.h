/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_ANIMATABLE (phoc_animatable_get_type ())
G_DECLARE_INTERFACE (PhocAnimatable, phoc_animatable, PHOC, ANIMATABLE, GObject)

/**
 * PhocFrameCallback:
 * @self: The paintable
 * @last_frame: Time of the last frame in us
 * @user_data: User data passed when registering the callback
 *
 * Callback type for adding a function to update animations. See
 * phoc_animatable_add_frame_callback().
 *
 * Returns: G_SOURCE_CONTINUE if the frame callback should continue to
 *  or G_SOURCE_REMOVE if the frame callback should be removed.
 */
typedef gboolean (*PhocFrameCallback) (PhocAnimatable *self,
                                       guint64         last_frame,
                                       gpointer        user_data);

struct _PhocAnimatableInterface
{
  GTypeInterface parent_iface;

  guint    (*add_frame_callback)    (PhocAnimatable      *self,
                                     PhocFrameCallback    callback,
                                     gpointer             user_data,
                                     GDestroyNotify       notify);
  void     (*remove_frame_callback) (PhocAnimatable      *self,
                                     guint                id);
};

guint    phoc_animatable_add_frame_callback    (PhocAnimatable    *self,
                                                PhocFrameCallback  callback,
                                                gpointer           user_data,
                                                GDestroyNotify     notify);
void     phoc_animatable_remove_frame_callback (PhocAnimatable    *self,
                                                guint              id);

G_END_DECLS
