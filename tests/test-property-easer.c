/*
 * Copyright (C) 2022 Purism SPC
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phoc-animation.h"
#include <glib-object.h>

#define PHOC_TYPE_TEST_OBJ (phoc_test_obj_get_type ())
G_DECLARE_FINAL_TYPE (PhocTestObj, phoc_test_obj, PHOC, TEST_OBJ, GObject)

enum {
  PROP_0,
  PROP_I,
  PROP_F,
  PROP_U,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocTestObj {
  GObject               parent;

  int                   prop_i;
  float                 prop_f;
  guint                 prop_u;
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
  case PROP_U:
    self->prop_u = g_value_get_uint (value);
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
  case PROP_U:
    g_value_set_uint (value, self->prop_u);
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

  props[PROP_U] =
    g_param_spec_uint ("prop-u", "", "",
                       0,
                       1000,
                       0,
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
test_phoc_property_easer_props_va_list (void)
{
  g_autoptr (PhocTestObj) obj = phoc_test_obj_new ();
  g_autoptr (PhocTestObj) cmp_obj = NULL;
  PhocEasing cmp_easing;
  float cmp_f;
  int cmp_i;
  guint cmp_u;
  g_autoptr (PhocPropertyEaser) easer = NULL;

  easer = g_object_new (PHOC_TYPE_PROPERTY_EASER,
                        "target", obj,
                        "easing", PHOC_EASING_EASE_IN_CUBIC,
                        NULL);

  g_object_get (easer, "target", &cmp_obj, "easing", &cmp_easing, NULL);
  g_assert_true (PHOC_IS_TEST_OBJ (cmp_obj));
  g_assert_true (cmp_obj == obj);
  g_assert_cmpint (cmp_easing, ==, PHOC_EASING_EASE_IN_CUBIC);

  phoc_property_easer_set_props (easer,
                                 "prop-i", 0, 10,
                                 "prop-f", -100.0, 100.0,
                                 "prop-u", 0, 101,
                                 NULL);

  g_object_set (easer, "progress", 1.0, NULL);
  g_object_get (obj, "prop-i", &cmp_i, "prop-f", &cmp_f, "prop-u", &cmp_u, NULL);

  g_assert_cmpint (cmp_i, ==, 10);
  g_assert_cmpfloat_with_epsilon (cmp_f, 100.0, FLT_EPSILON);
  g_assert_cmpint (cmp_u, ==, 101);
}


static void
test_phoc_property_easer_props_variant (void)
{
  g_autoptr (PhocTestObj) obj = phoc_test_obj_new ();
  float cmp_f;
  int cmp_i;
  guint cmp_u;
  g_autoptr (PhocPropertyEaser) easer = NULL;
  g_autoptr (GVariantBuilder) builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);

  g_variant_builder_add (builder, "(sdd)", "prop-f", -100.0, 100.0);
  g_variant_builder_add (builder, "(sdd)", "prop-i", 0.0, 10.0);
  g_variant_builder_add (builder, "(sdd)", "prop-u", 0.0, 100.0);

  easer = g_object_new (PHOC_TYPE_PROPERTY_EASER,
                        "target", obj,
                        "properties", g_variant_builder_end (builder),
                        "easing", PHOC_EASING_EASE_IN_CUBIC,
                        NULL);
  g_object_get (obj, "prop-i", &cmp_i, "prop-f", &cmp_f, "prop-u", &cmp_u, NULL);
  g_assert_cmpint (cmp_i, ==, 0);
  g_assert_cmpfloat_with_epsilon (cmp_f, -100.0, FLT_EPSILON);
  g_assert_cmpint (cmp_u, ==, 0);

  g_object_set (easer, "progress", 1.0, NULL);
  g_object_get (obj, "prop-i", &cmp_i, "prop-f", &cmp_f, "prop-u", &cmp_u, NULL);

  g_assert_cmpint (cmp_i, ==, 10);
  g_assert_cmpfloat_with_epsilon (cmp_f, 100.0, FLT_EPSILON);
  g_assert_cmpint (cmp_u, ==, 100);
}


gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func("/phoc/propety-easer/va-list", test_phoc_property_easer_props_va_list);
  g_test_add_func("/phoc/propety-easer/variant", test_phoc_property_easer_props_variant);

  return g_test_run();
}
