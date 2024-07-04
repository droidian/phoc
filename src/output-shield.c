/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-output-shield"

#include "phoc-config.h"

#include "phoc-animation.h"
#include "server.h"
#include "output-shield.h"

#include "render-private.h"

#define PHOC_ANIM_DURATION_SHIELD_UP 250 /* ms */

enum {
  PROP_0,
  PROP_ALPHA,
  PROP_OUTPUT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PhocOutputShield:
 *
 * A shield that covers a whole `PhocOutput`. It can be raised (to cover
 * the whole screen) and lowered to show the screens content.
 *
 * TODO: Use PhocColorRect to simplify
 */
struct _PhocOutputShield {
  GObject             parent;

  float               alpha;
  PhocOutput         *output;
  PhocTimedAnimation *animation;
  gulong              render_end_id;
};

static void phoc_output_shield_animatable_interface_init (PhocAnimatableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocOutputShield, phoc_output_shield, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (PHOC_TYPE_ANIMATABLE,
                                                phoc_output_shield_animatable_interface_init))

static guint
phoc_output_shield_add_frame_callback (PhocAnimatable   *iface,
                                       PhocFrameCallback callback,
                                       gpointer          user_data,
                                       GDestroyNotify    notify)
{
  PhocOutputShield *self = PHOC_OUTPUT_SHIELD (iface);

  return phoc_output_add_frame_callback (self->output, iface, callback, user_data, notify);
}


static void
phoc_output_shield_remove_frame_callback (PhocAnimatable *iface, guint id)
{
  PhocOutputShield *self = PHOC_OUTPUT_SHIELD (iface);

  phoc_output_remove_frame_callback (self->output, id);
}


static void
set_output (PhocOutputShield *self, PhocOutput *output)
{
  g_assert (output == NULL || PHOC_IS_OUTPUT (output));

  if (self->output == output)
    return;

  g_set_weak_pointer (&self->output, output);
}


static void
set_alpha (PhocOutputShield *self, float alpha)
{
  g_assert (alpha >= 0.0 && alpha <= 1.0);

  self->alpha = alpha;

  /* Damage covers the whole output */
  phoc_output_damage_whole (self->output);
}


static void
phoc_output_shield_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhocOutputShield *self = PHOC_OUTPUT_SHIELD (object);

  switch (property_id) {
  case PROP_ALPHA:
    set_alpha (self, g_value_get_float (value));
    break;
  case PROP_OUTPUT:
    set_output (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_output_shield_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhocOutputShield *self = PHOC_OUTPUT_SHIELD (object);

  switch (property_id) {
  case PROP_ALPHA:
    g_value_set_float (value, self->alpha);
    break;
  case PROP_OUTPUT:
    g_value_set_object (value, self->output);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
stop_render (PhocOutputShield *self)
{
  PhocRenderer *renderer = phoc_server_get_renderer (phoc_server_get_default ());

  g_clear_signal_handler (&self->render_end_id, renderer);
}



static void
on_render (PhocOutputShield *self, PhocRenderContext *ctx)
{
  struct wlr_output *wlr_output;

  if (self->output == NULL || self->output != ctx->output)
    return;

  g_debug ("%s: alpha: %f", __func__, self->alpha);
  wlr_output = self->output->wlr_output;

  wlr_render_pass_add_rect (ctx->render_pass, &(struct wlr_render_rect_options){
      .box = { .width = wlr_output->width, .height = wlr_output->height },
      .color =  { .a = self->alpha },
    });
}


static void
start_render (PhocOutputShield *self)
{
  PhocRenderer *renderer = phoc_server_get_renderer (phoc_server_get_default ());

  if (self->render_end_id)
    return;

  self->render_end_id = g_signal_connect_swapped (renderer,
                                                  "render-end",
                                                  G_CALLBACK (on_render),
                                                  self);
}


static void
on_animation_done (PhocOutputShield *self)
{
  /* We can unhook from the render loop once the shield is lowered completely */
  stop_render (self);
}


static void
phoc_output_shield_constructed (GObject *object)
{
  PhocOutputShield *self = PHOC_OUTPUT_SHIELD (object);

  g_autoptr (PhocTimedAnimation) fade_anim = NULL;
  g_autoptr (PhocPropertyEaser) easer = NULL;

  G_OBJECT_CLASS (phoc_output_shield_parent_class)->constructed (object);

  easer = g_object_new (PHOC_TYPE_PROPERTY_EASER,
                        "target", self,
                        "easing", PHOC_EASING_EASE_IN_CUBIC,
                        NULL);
  phoc_property_easer_set_props (easer,
                                 "alpha", 1.0, 0.0,
                                 NULL);

  fade_anim = g_object_new (PHOC_TYPE_TIMED_ANIMATION,
                            "animatable", self,
                            "duration", PHOC_ANIM_DURATION_SHIELD_UP,
                            "property-easer", easer,
                            NULL);
  g_set_object (&self->animation, fade_anim);

  g_signal_connect_swapped (self->animation, "done",
                            G_CALLBACK (on_animation_done),
                            self);
}


static void
phoc_output_shield_finalize (GObject *object)
{
  PhocOutputShield *self = PHOC_OUTPUT_SHIELD (object);

  set_output (self, NULL);
  stop_render (self);

  G_OBJECT_CLASS (phoc_output_shield_parent_class)->finalize (object);
}


static void
phoc_output_shield_animatable_interface_init (PhocAnimatableInterface *iface)
{
  iface->add_frame_callback = phoc_output_shield_add_frame_callback;
  iface->remove_frame_callback = phoc_output_shield_remove_frame_callback;
}


static void
phoc_output_shield_class_init (PhocOutputShieldClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_output_shield_get_property;
  object_class->set_property = phoc_output_shield_set_property;
  object_class->constructed = phoc_output_shield_constructed;
  object_class->finalize = phoc_output_shield_finalize;

  /**
   * PhocOutputShield:alpha:
   *
   * The current transparency of this shield.
   */
  props[PROP_ALPHA] =
    g_param_spec_float ("alpha", "", "",
                        0,
                        1.0,
                        1.0,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * PhocOutputShield:output:
   *
   * The output covered by this shield.
   */
  props[PROP_OUTPUT] =
    g_param_spec_object ("output", "", "",
                         PHOC_TYPE_OUTPUT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_output_shield_init (PhocOutputShield *self)
{
}


PhocOutputShield *
phoc_output_shield_new (PhocOutput *output)
{
  return g_object_new (PHOC_TYPE_OUTPUT_SHIELD,
                       "output", output,
                       NULL);
}


/**
 * phoc_output_shield_raise:
 * @self: The shield
 *
 * Draw the shield to cover the whole output.
 */
void
phoc_output_shield_raise (PhocOutputShield *self)
{
  g_return_if_fail (PHOC_IS_OUTPUT_SHIELD (self));

  phoc_timed_animation_skip (self->animation);

  set_alpha (self, 1.0);
  phoc_output_damage_whole (self->output);
  start_render (self);
}


/**
 * phoc_output_shield_lower
 * @self: The shield
 *
 * Lower the shield exposing the output's content.
 */
void
phoc_output_shield_lower (PhocOutputShield *self)
{
  g_return_if_fail (PHOC_IS_OUTPUT_SHIELD (self));

  start_render (self);
  phoc_timed_animation_play (self->animation);
}
