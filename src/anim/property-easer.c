/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-property-easer"

#include "config.h"

#include "easing.h"
#include "property-easer.h"


enum {
  PROP_0,
  PROP_TARGET,
  PROP_PROGRESS,
  PROP_PROPERTIES,
  PROP_EASING,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


typedef struct _PhocEaseProp {
  GParamSpec *pspec;
  float start;
  float end;
} PhocEaseProp;


/**
 * PhocPropertyEaser:
 *
 * Eases properties of a given object.
 *
 * Based on the given easing function the `PhocPropertyEaser` will
 * ease the specified `float` properties of the `target` object based
 * on the progress set via `progress` across the unit interval `[0.0,
 * 1.0]`.
 *
 * If an object has properties `a` and `b` that should be eased in the intervals `[-1.0,1.0]`
 * and `[10.0, 100.0]` respectively the invocation would look like:
 *
 * |[<!-- language="C" -->
 * PhocPropertyEaser easer = phoc_property_easer_new (G_OBJECT (object));
 * phoc_property_easer_set_props (easer, "a", -1.0, 1.0, "b", 0.0, 100.0, NULL);
 * ]|
 *
 * which sets up the properties to their initial values. Changing `progress` then
 * updates the properties according to the given easing function. E.g. with `PHOC_EASING_NONE`
 * (plain linear interpolation)
 *
 * |[<!-- language="C" -->
 * phoc_property_easer_set_progress (easer, 0.5);
 * ]|
 *
 * would set `object.a == 0.0` and `object.b == 50.0`.
 *
 * The eased properties must be of type `float` or `int`.
 */
struct _PhocPropertyEaser {
  GObject         parent;

  float           progress;
  GObject        *target;
  PhocEasing      easing;

  GHashTable     *ease_props;
};
G_DEFINE_TYPE (PhocPropertyEaser, phoc_property_easer, G_TYPE_OBJECT)


/**
 * phoc_lerp:
 * @a: the start
 * @b: the end
 * @t: the interpolation rate
 *
 * Computes the linear interpolation between @a and @b for @t.
 *
 * Returns: the computed value
 *
 * Since: 1.0
 */
static double
phoc_lerp (double a, double b, double t)
{
  /* a + (b - a) * t */
  return a * (1.0 - t) + b * t;
}


static void
phoc_ease_prop_free (PhocEaseProp *ease_prop)
{
  g_free (ease_prop);
}


static PhocEaseProp *
phoc_ease_prop_new (GParamSpec *pspec, float start, float end)
{
  PhocEaseProp *ease_prop = g_new0 (PhocEaseProp, 1);

  g_assert (G_IS_PARAM_SPEC (pspec));

  ease_prop->pspec = pspec;
  ease_prop->start = start;
  ease_prop->end = end;

  return ease_prop;
}


static void
set_target (PhocPropertyEaser *self, GObject *target)
{
  g_assert (target == NULL || G_IS_OBJECT (target));

  if (self->target == target)
    return;

  g_set_weak_pointer (&self->target, target);

  g_assert (self->target == NULL || G_IS_OBJECT (self->target));
}


#define PROPS_FORMAT "(&sdd)"


static guint
set_props_variant (PhocPropertyEaser *self, GVariant *variant)
{
  const gchar *name;
  guint n_params = 0;
  GVariantIter iter;
  double start, end;

  if (variant == NULL)
    return 0;

  g_return_val_if_fail (self->target, 0);

  g_variant_iter_init (&iter, variant);
  while (g_variant_iter_next (&iter, PROPS_FORMAT, &name, &start, &end, NULL)) {
    GParamSpec *pspec;
    PhocEaseProp *ease_prop;

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self->target), name);

    if (pspec == NULL) {
      g_warning ("'%s' is not a valid property", name);
      continue;
    }

    ease_prop = phoc_ease_prop_new (pspec, start, end);
    g_hash_table_insert (self->ease_props, g_strdup (name), ease_prop);

    n_params++;
  }

  phoc_property_easer_set_progress (self, 0.0);

  return n_params;
}



static void
phoc_property_easer_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PhocPropertyEaser *self = PHOC_PROPERTY_EASER (object);

  switch (property_id) {
  case PROP_TARGET:
    set_target (self, g_value_get_object (value));
    break;
  case PROP_PROGRESS:
    phoc_property_easer_set_progress (self, g_value_get_float (value));
    break;
  case PROP_PROPERTIES:
    set_props_variant (self, g_value_get_variant (value));
    break;
  case PROP_EASING:
    phoc_property_easer_set_easing (self, g_value_get_enum (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_property_easer_get_property (GObject    *object,
                     guint       property_id,
                     GValue     *value,
                     GParamSpec *pspec)
{
  PhocPropertyEaser *self = PHOC_PROPERTY_EASER (object);

  switch (property_id) {
  case PROP_TARGET:
    g_value_set_object (value, self->target);
    break;
  case PROP_PROGRESS:
    g_value_set_float (value, phoc_property_easer_get_progress (self));
    break;
  case PROP_EASING:
    g_value_set_enum (value, phoc_property_easer_get_easing (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_property_easer_dispose (GObject *object)
{
  PhocPropertyEaser *self = PHOC_PROPERTY_EASER(object);

  set_target (self, NULL);
  g_clear_pointer (&self->ease_props, g_hash_table_destroy);

  G_OBJECT_CLASS (phoc_property_easer_parent_class)->dispose (object);
}


static void
phoc_property_easer_class_init (PhocPropertyEaserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_property_easer_get_property;
  object_class->set_property = phoc_property_easer_set_property;
  object_class->dispose = phoc_property_easer_dispose;

  /**
   * PhocPropertyEaser:target:
   *
   * The object of which the properties will be eased.
   */
  props[PROP_TARGET] =
    g_param_spec_object ("target", "", "",
                         G_TYPE_OBJECT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhocPropertyEaser:properties:
   *
   * the properties to ease. Needs to be set past `target`.
   */
  props[PROP_PROPERTIES] =
    g_param_spec_variant ("properties", "", "",
                          G_VARIANT_TYPE_ARRAY,
                          NULL,
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PhocPropertyEaser:progress:
   *
   * The progress of the animation in milliseconds.
   */
  props[PROP_PROGRESS] =
    g_param_spec_float ("progress", "", "",
                        0,
                        1.0,
                        0,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhocPropertyEaser:easing:
   *
   * The easing function to use.
   */
  props[PROP_EASING] =
    g_param_spec_enum ("easing", "", "",
                       PHOC_TYPE_EASING,
                       PHOC_EASING_NONE,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_property_easer_init (PhocPropertyEaser *self)
{
  self->ease_props = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)phoc_ease_prop_free);
}


PhocPropertyEaser *
phoc_property_easer_new (GObject *target)
{
  return PHOC_PROPERTY_EASER (g_object_new (PHOC_TYPE_PROPERTY_EASER,
                                            "target", target,
                                            NULL));
}

/**
 * phoc_property_easer_set_progress:
 * @self: The property easer
 * @progress: The current progress
 *
 * Sets the current progress and updates the target objects properties according
 * to the set easing function.
 */
void
phoc_property_easer_set_progress (PhocPropertyEaser *self, float progress)
{
  GHashTableIter iter;
  PhocEaseProp *ease_prop;

  /* target disposed, nothing to do */
  if (self->target == NULL)
    return;

  g_return_if_fail (g_hash_table_size (self->ease_props));
  g_return_if_fail (PHOC_IS_PROPERTY_EASER (self));
  g_return_if_fail (progress >= 0.0 && progress <= 1.0);

  self->progress = progress;

  g_object_freeze_notify (self->target);
  g_hash_table_iter_init (&iter, self->ease_props);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &ease_prop)) {
    float t = phoc_easing_ease (self->easing, progress);
    g_auto (GValue) val = G_VALUE_INIT;
    g_auto (GValue) target = G_VALUE_INIT;

    g_value_init (&val, G_TYPE_FLOAT);
    g_value_init (&target, G_PARAM_SPEC_VALUE_TYPE (ease_prop->pspec));

    g_value_set_float (&val, phoc_lerp (ease_prop->start, ease_prop->end, t));
    g_value_transform (&val, &target);

    g_object_set_property (self->target,
                           ease_prop->pspec->name,
                           &target);
  }
  g_object_thaw_notify (self->target);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PROGRESS]);
}


float
phoc_property_easer_get_progress (PhocPropertyEaser *self)
{
  g_return_val_if_fail (PHOC_IS_PROPERTY_EASER (self), 0.0);

  return self->progress;
}


void
phoc_property_easer_set_easing (PhocPropertyEaser *self, PhocEasing easing)
{
  g_return_if_fail (PHOC_IS_PROPERTY_EASER (self));

  if (self->easing == easing)
    return;

  self->easing = easing;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_EASING]);
}


PhocEasing
phoc_property_easer_get_easing (PhocPropertyEaser *self)
{
  g_return_val_if_fail (PHOC_IS_PROPERTY_EASER (self), PHOC_EASING_NONE);

  return self->easing;
}


static guint
phoc_property_easer_set_props_valist (PhocPropertyEaser *self,
                                      const gchar       *first_property_name,
                                      va_list            var_args)
{
  const gchar *name;
  guint n_params = 0;

  name = first_property_name;
  do {
    GParamSpec *pspec;
    PhocEaseProp *ease_prop;
    float start, end;

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self->target), name);

    if (pspec == NULL) {
      g_warning ("'%s' is not a valid property", name);
      continue;
    }

    if (pspec->value_type == G_TYPE_FLOAT) {
      start = va_arg (var_args, double);
      end = va_arg (var_args, double);
    } else if (pspec->value_type == G_TYPE_INT) {
      start = va_arg (var_args, int);
      end = va_arg (var_args, int);
    } else {
      g_warning ("'%s' is not a float or int property", name);
      continue;
    }

    ease_prop = phoc_ease_prop_new (pspec, start, end);
    g_hash_table_insert (self->ease_props, g_strdup (name), ease_prop);

    n_params++;
  } while ((name = va_arg (var_args, const gchar *)));

  phoc_property_easer_set_progress (self, 0.0);

  return n_params;
}

/**
 * phoc_property_easer_set_props_valist:
 * @self: The property easer
 * @first_property_name: the name of the first property to ease
 * @...: the start and end values of the first property, followed optionally by more
 *  name/start/end triplets, followed by %NULL
 *
 * Set the properties to ease.
 */
guint
phoc_property_easer_set_props (PhocPropertyEaser *self,
                               const gchar       *first_property_name,
                               ...)
{
  guint n;
  va_list var_args;

  g_return_val_if_fail (PHOC_IS_PROPERTY_EASER (self), 0);

  va_start (var_args, first_property_name);
  n = phoc_property_easer_set_props_valist (self, first_property_name, var_args);
  va_end (var_args);
  return n;
}
