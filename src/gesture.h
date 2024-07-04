/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "event.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOC_TYPE_GESTURE (phoc_gesture_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhocGesture, phoc_gesture, PHOC, GESTURE, GObject)


/**
 * PhocGestureClass:
 * @parent_class: The parent class
 */
struct _PhocGestureClass
{
  GObjectClass parent_class;

  gboolean (*check)         (PhocGesture *self);
  gboolean (*handle_event)  (PhocGesture *self, const PhocEvent *event, double lx, double ly);
  void     (*reset)         (PhocGesture *self);
  /* Tells whether the event is filtered out, %TRUE makes
   * the event unseen by the handle_event vfunc.
   */
  gboolean (* filter_event) (PhocGesture    *self, const PhocEvent *event);

  /* vfuncs for signals */
  void     (*begin)        (PhocGesture *self, PhocEventSequence *seq);
  void     (*update)       (PhocGesture *self, PhocEventSequence *seq);
  void     (*end)          (PhocGesture *self, PhocEventSequence *seq);
  void     (*cancel)       (PhocGesture *self, PhocEventSequence *seq);
  void     (*sequence_state_changed)  (PhocGesture            *gesture,
                                       PhocEventSequence      *sequence,
                                       PhocEventSequenceState  state);
};


PhocGesture     *phoc_gesture_new                    (void);
gboolean         phoc_gesture_handle_event           (PhocGesture            *self,
                                                      const PhocEvent        *event,
                                                      double                  lx,
                                                      double                  ly);
gboolean         phoc_gesture_handles_sequence       (PhocGesture            *self,
                                                      PhocEventSequence      *sequence);
gboolean         phoc_gesture_is_active              (PhocGesture            *self);
void             phoc_gesture_reset                  (PhocGesture            *self);
PhocEventSequenceState
                 phoc_gesture_get_sequence_state     (PhocGesture            *self,
                                                      PhocEventSequence      *sequence);
gboolean         phoc_gesture_set_sequence_state     (PhocGesture            *self,
                                                      PhocEventSequence      *sequence,
                                                      PhocEventSequenceState  state);
gboolean         phoc_gesture_set_state              (PhocGesture            *self,
                                                      PhocEventSequenceState  state);
GList *          phoc_gesture_get_sequences          (PhocGesture            *self);
gboolean         phoc_gesture_is_recognized          (PhocGesture            *self);
void             phoc_gesture_group                  (PhocGesture            *group_gesture,
                                                      PhocGesture            *gesture);
void             phoc_gesture_ungroup                (PhocGesture            *self);
const PhocEvent *phoc_gesture_get_last_event         (PhocGesture            *self,
                                                      PhocEventSequence      *sequence);
gboolean         phoc_gesture_get_point              (PhocGesture            *self,
                                                      PhocEventSequence      *sequence,
                                                      double                 *lx,
                                                      double                 *ly);
PhocEventSequence *phoc_gesture_get_last_updated_sequence (PhocGesture       *self);
gboolean         phoc_gesture_get_last_update_time  (PhocGesture             *self,
                                                     PhocEventSequence       *sequence,
                                                     guint32                 *evtime);

G_END_DECLS
