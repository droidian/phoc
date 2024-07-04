/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-view-deco"

#include "phoc-config.h"

#include "bling.h"
#include "desktop.h"
#include "output.h"
#include "server.h"
#include "view-deco.h"
#include "utils.h"

#include "render-private.h"

#define PHOC_DECO_BORDER_WIDTH      4
#define PHOC_DECO_TITLEBAR_HEIGHT  12
#define PHOC_DECO_COLOR            ((struct wlr_render_color){ 0.2, 0.2, 0.2, 1.0 })

/**
 * PhocViewDeco:
 *
 * The decoration for views using server side decorations
 */

enum {
  PROP_0,
  PROP_VIEW,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocViewDeco {
  GObject         parent;

  PhocView       *view;
  guint           border_width;
  guint           titlebar_height;
  gboolean        mapped;
};

static void bling_interface_init (PhocBlingInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocViewDeco, phoc_view_deco, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (PHOC_TYPE_BLING, bling_interface_init))


static PhocBox
phoc_view_deco_bling_get_box (PhocBling *bling)
{
  PhocViewDeco *self = PHOC_VIEW_DECO (bling);
  PhocBox box;

  phoc_view_get_box (self->view, &box);

  box.x -= self->border_width;
  box.y -= (self->border_width + self->titlebar_height);
  box.width += self->border_width * 2;
  box.height += (self->border_width * 2 + self->titlebar_height);

  return box;
}


static void
phoc_view_deco_damage_box (PhocViewDeco *self)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocOutput *output;

  if (!self->mapped)
    return;

  wl_list_for_each (output, &desktop->outputs, link) {
    struct wlr_box damage_box = phoc_bling_get_box (PHOC_BLING (self));
    bool intersects = wlr_output_layout_intersects (desktop->layout, output->wlr_output, &damage_box);
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
phoc_view_deco_bling_render (PhocBling *bling, PhocRenderContext *ctx)
{
  struct wlr_box box = phoc_view_deco_bling_get_box (bling);

  box.x -= ctx->output->lx;
  box.y -= ctx->output->ly;
  phoc_utils_scale_box (&box, ctx->output->wlr_output->scale);

  pixman_region32_t damage;
  if (!phoc_utils_is_damaged (&box, ctx->damage, NULL, &damage)) {
    pixman_region32_fini (&damage);
    return;
  }

  phoc_output_transform_damage (ctx->output, &damage);
  phoc_output_transform_box (ctx->output, &box);

  wlr_render_pass_add_rect(ctx->render_pass, &(struct wlr_render_rect_options){
      .box = box,
      .color = PHOC_DECO_COLOR,
      .clip = &damage,
    });
  pixman_region32_fini (&damage);
}


static void
phoc_view_deco_bling_map (PhocBling *bling)
{
  PhocViewDeco *self = PHOC_VIEW_DECO (bling);

  self->mapped = TRUE;
  phoc_view_deco_damage_box (self);
}


static void
phoc_view_deco_bling_unmap (PhocBling *bling)
{
  PhocViewDeco *self = PHOC_VIEW_DECO (bling);

  phoc_view_deco_damage_box (self);
  self->mapped = FALSE;
}


static gboolean
phoc_view_deco_bling_is_mapped (PhocBling *bling)
{
  PhocViewDeco *self = PHOC_VIEW_DECO (bling);

  return self->mapped;
}


static void
bling_interface_init (PhocBlingInterface *iface)
{
  iface->get_box = phoc_view_deco_bling_get_box;
  iface->render = phoc_view_deco_bling_render;
  iface->map = phoc_view_deco_bling_map;
  iface->unmap = phoc_view_deco_bling_unmap;
  iface->is_mapped = phoc_view_deco_bling_is_mapped;
}


static void
phoc_view_deco_set_view (PhocViewDeco *self, PhocView *view)
{
  g_assert (PHOC_IS_VIEW_DECO (self));

  if (self->view == view)
    return;

  if (self->view)
    g_object_remove_weak_pointer (G_OBJECT (self->view),
                                  (gpointer *)&self->view);

  self->view = view;

  if (self->view) {
    g_object_add_weak_pointer (G_OBJECT (self->view),
                               (gpointer *)&self->view);
  }
}


static void
phoc_view_deco_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PhocViewDeco *self = PHOC_VIEW_DECO (object);

  switch (property_id) {
  case PROP_VIEW:
    phoc_view_deco_set_view (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_view_deco_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PhocViewDeco *self = PHOC_VIEW_DECO (object);

  switch (property_id) {
  case PROP_VIEW:
    g_value_set_object (value, self->view);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}



static void
phoc_view_deco_dispose (GObject *object)
{
  PhocViewDeco *self = PHOC_VIEW_DECO (object);

  phoc_view_deco_set_view (self, NULL);

  G_OBJECT_CLASS (phoc_view_deco_parent_class)->dispose (object);
}


static void
phoc_view_deco_class_init (PhocViewDecoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_view_deco_get_property;
  object_class->set_property = phoc_view_deco_set_property;
  object_class->dispose = phoc_view_deco_dispose;

  props[PROP_VIEW] =
    g_param_spec_object ("view", "", "",
                         PHOC_TYPE_VIEW,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_view_deco_init (PhocViewDeco *self)
{
  self->border_width = PHOC_DECO_BORDER_WIDTH;
  self->titlebar_height = PHOC_DECO_TITLEBAR_HEIGHT;
}


PhocViewDeco *
phoc_view_deco_new (PhocView *view)
{
  return g_object_new (PHOC_TYPE_VIEW_DECO, "view", view, NULL);
}


PhocViewDecoPart
phoc_view_deco_get_part (PhocViewDeco *self, double sx, double sy)
{
  g_assert (PHOC_IS_VIEW_DECO (self));

  int sw = self->view->wlr_surface->current.width;
  int sh = self->view->wlr_surface->current.height;
  int bw = self->border_width;
  int titlebar_h = self->titlebar_height;

  if (sx > 0 && sx < sw && sy < 0 && sy > -titlebar_h)
    return PHOC_VIEW_DECO_PART_TITLEBAR;

  PhocViewDecoPart parts = 0;
  if (sy >= -(titlebar_h + bw) && sy <= sh + bw) {
    if (sx < 0 && sx > -bw)
      parts |= PHOC_VIEW_DECO_PART_LEFT_BORDER;
    else if (sx > sw && sx < sw + bw)
      parts |= PHOC_VIEW_DECO_PART_RIGHT_BORDER;
  }

  if (sx >= -bw && sx <= sw + bw) {
    if (sy > sh && sy <= sh + bw)
      parts |= PHOC_VIEW_DECO_PART_BOTTOM_BORDER;
    else if (sy >= -(titlebar_h + bw) && sy < 0)
      parts |= PHOC_VIEW_DECO_PART_TOP_BORDER;
  }

  // TODO corners

  return parts;
}
