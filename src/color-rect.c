/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-color-box"

#include "phoc-config.h"
#include "color-rect.h"
#include "server.h"
#include "desktop.h"
#include "output.h"
#include "utils.h"

#include "render-private.h"

#include <glib.h>

/**
 * PhocColorRect:
 *
 * A colored rectangle to be drawn by the compositor.
 *
 * When created the rectangle is initially unmapped. For it to be drawn it needs
 * to be mapped and attached to the render tree by e.g. adding it as a [type@Bling]
 * to a [type@View].
 */

enum {
  PROP_0,
  PROP_X,
  PROP_Y,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_BOX,
  PROP_COLOR,
  PROP_ALPHA,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocColorRect {
  GObject        parent;

  gboolean       mapped;
  PhocBox        box;
  PhocColor      color;
};

static void bling_interface_init (PhocBlingInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocColorRect, phoc_color_rect, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (PHOC_TYPE_BLING, bling_interface_init))


static void
phoc_color_rect_damage_box (PhocColorRect *self)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocOutput *output;

  if (!self->mapped)
    return;

  wl_list_for_each (output, &desktop->outputs, link) {
    struct wlr_box damage_box = self->box;
    bool intersects = wlr_output_layout_intersects (desktop->layout, output->wlr_output, &self->box);
    if (!intersects)
      continue;

    damage_box.x -= output->lx;
    damage_box.y -= output->ly;
    phoc_utils_scale_box (&damage_box, output->wlr_output->scale);

    if (wlr_damage_ring_add_box (&output->damage_ring, &damage_box))
      wlr_output_schedule_frame (output->wlr_output);
  }
}


static void
phoc_color_rect_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PhocColorRect *self = PHOC_COLOR_RECT (object);

  switch (property_id) {
  case PROP_X:
    /* Damage the old box's area */
    phoc_color_rect_damage_box (self);
    self->box.x = g_value_get_int (value);
    /* Damage the new box's area */
    phoc_color_rect_damage_box (self);
    break;
  case PROP_Y:
    phoc_color_rect_damage_box (self);
    self->box.y = g_value_get_int (value);
    phoc_color_rect_damage_box (self);
    break;
  case PROP_WIDTH:
    phoc_color_rect_damage_box (self);
    self->box.width = g_value_get_uint (value);
    phoc_color_rect_damage_box (self);
    break;
  case PROP_HEIGHT:
    phoc_color_rect_damage_box (self);
    self->box.height = g_value_get_uint (value);
    phoc_color_rect_damage_box (self);
    break;
  case PROP_BOX:
    phoc_color_rect_damage_box (self);
    self->box = *(PhocBox*)g_value_get_boxed (value);
    phoc_color_rect_damage_box (self);
    break;
  case PROP_COLOR:
    phoc_color_rect_set_color (self, g_value_get_boxed (value));
    break;
  case PROP_ALPHA:
    phoc_color_rect_set_alpha (self, g_value_get_float (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_color_rect_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PhocColorRect *self = PHOC_COLOR_RECT (object);

  switch (property_id) {
  case PROP_X:
    g_value_set_int (value, self->box.x);
    break;
  case PROP_Y:
    g_value_set_int (value, self->box.y);
    break;
  case PROP_WIDTH:
    g_value_set_uint (value, self->box.width);
    break;
  case PROP_HEIGHT:
    g_value_set_uint (value, self->box.height);
    break;
  case PROP_BOX:
    g_value_set_boxed (value, &self->box);
    break;
  case PROP_COLOR:
    g_value_set_boxed (value, &self->color);
    break;
  case PROP_ALPHA:
    g_value_set_float (value, phoc_color_rect_get_alpha (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_color_rect_dispose (GObject *object)
{
  PhocColorRect *self = PHOC_COLOR_RECT(object);

  phoc_bling_unmap (PHOC_BLING (self));

  G_OBJECT_CLASS (phoc_color_rect_parent_class)->dispose (object);
}

static void
bling_render (PhocBling *bling, PhocRenderContext *ctx)
{
  PhocColorRect *self = PHOC_COLOR_RECT (bling);
  pixman_region32_t damage;

  if (!self->mapped)
    return;

  struct wlr_box box = self->box;
  box.x -= ctx->output->lx;
  box.y -= ctx->output->ly;
  phoc_utils_scale_box (&box, ctx->output->wlr_output->scale);

  if (!phoc_utils_is_damaged (&box, ctx->damage, NULL, &damage)) {
    pixman_region32_fini (&damage);
    return;
  }

  phoc_output_transform_damage (ctx->output, &damage);
  phoc_output_transform_box (ctx->output, &box);

  wlr_render_pass_add_rect (ctx->render_pass, &(struct wlr_render_rect_options){
      .box = box,
      .color = {
        .r = self->color.red * self->color.alpha,
        .g = self->color.green * self->color.alpha,
        .b = self->color.blue * self->color.alpha,
        .a = self->color.alpha,
      },
      .clip = &damage,
    });
  pixman_region32_fini (&damage);
}


static PhocBox
bling_get_box (PhocBling *bling)
{
  PhocColorRect *self = PHOC_COLOR_RECT (bling);

  return phoc_color_rect_get_box (self);
}


static void
bling_map (PhocBling *bling)
{
  PhocColorRect *self = PHOC_COLOR_RECT (bling);

  self->mapped = TRUE;
  phoc_color_rect_damage_box (self);
}


static void
bling_unmap (PhocBling *bling)
{
  PhocColorRect *self = PHOC_COLOR_RECT (bling);

  phoc_color_rect_damage_box (self);
  self->mapped = FALSE;
}


static gboolean
bling_is_mapped (PhocBling *bling)
{
  PhocColorRect *self = PHOC_COLOR_RECT (bling);

  return self->mapped;
}


static void
bling_interface_init (PhocBlingInterface *iface)
{
  iface->get_box = bling_get_box;
  iface->render = bling_render;
  iface->map = bling_map;
  iface->unmap = bling_unmap;
  iface->is_mapped = bling_is_mapped;
}



static void
phoc_color_rect_class_init (PhocColorRectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_color_rect_get_property;
  object_class->set_property = phoc_color_rect_set_property;
  object_class->dispose = phoc_color_rect_dispose;

  props[PROP_X] =
    g_param_spec_int ("x", "", "",
                      -G_MAXINT, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_Y] =
    g_param_spec_int ("y", "", "",
                      -G_MAXINT, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_WIDTH] =
    g_param_spec_uint ("width", "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_HEIGHT] =
    g_param_spec_uint ("height", "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhocColorRect:box:
   *
   * The rectangle's box in layout coordinates
   */
  props[PROP_BOX] =
    g_param_spec_boxed ("box", "", "",
                        PHOC_TYPE_BOX,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhocColorRect:color:
   *
   * The rectangle's color
   */
  props[PROP_COLOR] =
    g_param_spec_boxed ("color", "", "",
                        PHOC_TYPE_COLOR,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhocColorRect:alpha:
   *
   * The rectangle's alpha channel
   */
  props[PROP_ALPHA] =
    g_param_spec_float ("alpha", "", "",
                        0.0, 1.0, 0.0,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_color_rect_init (PhocColorRect *self)
{
}


PhocColorRect *
phoc_color_rect_new (PhocBox *box, PhocColor *color)
{
  return g_object_new (PHOC_TYPE_COLOR_RECT,
                       "box", box,
                       "color", color,
                       NULL);
}


/**
 * phoc_color_rect_get_box:
 * @self: The color rectangle
 *
 * Get the rectangles current coordinates and size as box.
 *
 * Returns: The current rectangle's position and size
 */
PhocBox
phoc_color_rect_get_box (PhocColorRect *self)
{
  g_assert (PHOC_IS_COLOR_RECT (self));

  return self->box;
}

/**
 * phoc_color_rect_set_color:
 * @self: The color rectangle
 * @color: The color
 *
 * Set the rectangle's color
 */
void
phoc_color_rect_set_color (PhocColorRect *self, PhocColor *color)
{
  float alpha;

  g_assert (PHOC_IS_COLOR_RECT (self));

  if (phoc_color_is_equal (&self->color, color))
    return;

  alpha = self->color.alpha;
  self->color = *color;
  phoc_color_rect_damage_box (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COLOR]);
  if (!G_APPROX_VALUE (self->color.alpha, alpha, FLT_EPSILON))
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALPHA]);
}

/**
 * phoc_color_rect_get_color:
 * @self: The color rectangle
 *
 * Get the rectangle's color
 *
 * Returns: the color
 */
PhocColor
phoc_color_rect_get_color (PhocColorRect *self)
{
  g_assert (PHOC_IS_COLOR_RECT (self));

  return self->color;
}


void
phoc_color_rect_set_alpha (PhocColorRect *self, float alpha)
{
  g_assert (PHOC_IS_COLOR_RECT (self));

  if (G_APPROX_VALUE (self->color.alpha, alpha, FLT_EPSILON))
    return;

  self->color.alpha = alpha;
  phoc_color_rect_damage_box (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALPHA]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COLOR]);
}


float
phoc_color_rect_get_alpha (PhocColorRect *self)
{
  g_assert (PHOC_IS_COLOR_RECT (self));

  return self->color.alpha;
}
