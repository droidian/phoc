/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "anim/timed-animation.h"


#define PHOC_TYPE_TEST_OBJ (phoc_test_obj_get_type ())
G_DECLARE_FINAL_TYPE (PhocTestObj, phoc_test_obj, PHOC, TEST_OBJ, GObject)

enum {
  PROP_0,
  PROP_I,
  PROP_F,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocTestObj {
  GObject               parent;

  int                   prop_i;
  float                 prop_f;
};
G_DEFINE_TYPE (PhocTestObj, phoc_test_obj, G_TYPE_OBJECT)


static void
phoc_test_obj_set_property (GObject      *object,
                     guint         property_id,
                     const GValue *value,
                     GParamSpec   *pspec)
{
  PhocTestObj *self = PHOC_TEST_OBJ (object);

  switch (property_id) {
  case PROP_I:
    self->prop_i = g_value_get_int (value);
    break;
  case PROP_F:
    self->prop_f = g_value_get_float (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_test_obj_get_property (GObject    *object,
                     guint       property_id,
                     GValue     *value,
                     GParamSpec *pspec)
{
  PhocTestObj *self = PHOC_TEST_OBJ (object);

  switch (property_id) {
  case PROP_I:
    g_value_set_int (value, self->prop_i);
    break;
  case PROP_F:
    g_value_set_float (value, self->prop_f);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_test_obj_class_init (PhocTestObjClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_test_obj_get_property;
  object_class->set_property = phoc_test_obj_set_property;

  props[PROP_I] =
    g_param_spec_int ("prop-i", "", "",
                      0,
                      1000,
                      0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_F] =
    g_param_spec_float ("prop-f", "", "",
                        -1000.0,
                        1000.0,
                        0.0,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_test_obj_init (PhocTestObj *self)
{
}


static PhocTestObj *
phoc_test_obj_new (void)
{
  return PHOC_TEST_OBJ (g_object_new (PHOC_TYPE_TEST_OBJ, NULL));
}


static void
test_phoc_timed_animation_simple (void)
{
  PhocTimedAnimation *anim = phoc_timed_animation_new ();

  g_assert_cmpint (phoc_timed_animation_get_duration (anim), ==, 0);
  g_assert_cmpint (phoc_timed_animation_get_state (anim), ==,
                   PHOC_TIMED_ANIMATION_IDLE);
  g_assert_false (phoc_timed_animation_get_dispose_on_done (anim));

  g_assert_finalize_object (anim);
}


static void
test_phoc_timed_animation_dispose_on_done (void)
{
  PhocTimedAnimation *anim = NULL;
  PhocTestObj *obj = phoc_test_obj_new ();
  PhocPropertyEaser *easer = phoc_property_easer_new (G_OBJECT (obj));

  anim = g_object_new (PHOC_TYPE_TIMED_ANIMATION,
                       "dispose-on-done", TRUE,
                       "property-easer", easer,
                       NULL);
  phoc_property_easer_set_props (easer, "prop-f", NULL);

  g_assert_cmpint (phoc_timed_animation_get_duration (anim), ==, 0);
  g_assert_cmpint (phoc_timed_animation_get_state (anim), ==,
                   PHOC_TIMED_ANIMATION_IDLE);
  g_assert_true (phoc_timed_animation_get_dispose_on_done (anim));

  phoc_timed_animation_skip (anim);

  /* anim holds a ref on easer and obj so dispose first */
  g_assert_finalize_object (anim);
  g_assert_finalize_object (easer);
  g_assert_finalize_object (obj);
}


gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func("/phoc/timed-animation/simple", test_phoc_timed_animation_simple);
  g_test_add_func("/phoc/timed-animation/dispose_on_done",
                  test_phoc_timed_animation_dispose_on_done);

  return g_test_run();
}
