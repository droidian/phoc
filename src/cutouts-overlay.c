/*
 * Copyright (C) 2022 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-cutouts-overlay"

#include "phoc-config.h"

#include "server.h"
#include "render-private.h"
#include "cutouts-overlay.h"

#include <gmobile.h>
#include <cairo/cairo.h>
#include <drm_fourcc.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (cairo_t, cairo_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (cairo_surface_t, cairo_surface_destroy)

/**
 * PhocCutoutsOverlay:
 *
 * An overlay texture to render a devices cutouts.
 */

enum {
  PROP_0,
  PROP_COMPATIBLES,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocCutoutsOverlay {
  GObject          parent;

  GStrv            compatibles;
  GmDisplayPanel  *panel;
};
G_DEFINE_TYPE (PhocCutoutsOverlay, phoc_cutouts_overlay, G_TYPE_OBJECT)


static void
cutouts_overlay_set_compatibles (PhocCutoutsOverlay *self, const char *const *compatibles)
{
  GmDisplayPanel *panel = NULL;
  g_autoptr (GmDeviceInfo) info = NULL;

  if (compatibles == NULL) {
    g_strfreev (self->compatibles);
    return;
  }

  if (self->compatibles && g_strv_equal ((const char *const *)self->compatibles, compatibles))
    return;

  self->compatibles = g_strdupv ((GStrv)compatibles);
  info = gm_device_info_new ((const char * const *)self->compatibles);
  panel = gm_device_info_get_display_panel (info);

  if (panel == NULL)
    g_warning ("No panel found for compatibles");

  g_debug ("Found panel '%s'", gm_display_panel_get_name (panel));
  g_set_object (&self->panel, panel);
}


static void
phoc_cutouts_overlay_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  PhocCutoutsOverlay *self = PHOC_CUTOUTS_OVERLAY (object);

  switch (property_id) {
  case PROP_COMPATIBLES:
    cutouts_overlay_set_compatibles (self, g_value_get_boxed (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_cutouts_overlay_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PhocCutoutsOverlay *self = PHOC_CUTOUTS_OVERLAY (object);

  switch (property_id) {
  case PROP_COMPATIBLES:
    g_value_set_boxed (value, self->compatibles);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_cutouts_overlay_finalize (GObject *object)
{
  PhocCutoutsOverlay *self = PHOC_CUTOUTS_OVERLAY(object);

  g_clear_object (&self->panel);
  g_clear_pointer (&self->compatibles, g_strfreev);

  G_OBJECT_CLASS (phoc_cutouts_overlay_parent_class)->finalize (object);
}


static void
phoc_cutouts_overlay_class_init (PhocCutoutsOverlayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_cutouts_overlay_get_property;
  object_class->set_property = phoc_cutouts_overlay_set_property;
  object_class->finalize = phoc_cutouts_overlay_finalize;

  props[PROP_COMPATIBLES] =
    g_param_spec_boxed ("compatibles", "", "",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_cutouts_overlay_init (PhocCutoutsOverlay *self)
{
}


PhocCutoutsOverlay *
phoc_cutouts_overlay_new (const char * const *compatibles)
{
  return g_object_new (PHOC_TYPE_CUTOUTS_OVERLAY,
                       "compatibles", compatibles,
                       NULL);
}


struct wlr_texture *
phoc_cutouts_overlay_get_cutouts_texture (PhocCutoutsOverlay *self, PhocOutput *output)
{
  int width, height, radius, stride;
  GListModel *cutouts;
  unsigned char *data;
  PhocServer *server = phoc_server_get_default ();
  PhocRenderer *renderer = phoc_server_get_renderer (server);
  struct wlr_texture *texture;
  g_autoptr (cairo_surface_t) surface = NULL;
  g_autoptr (cairo_t) cr = NULL;

  g_return_val_if_fail (PHOC_IS_CUTOUTS_OVERLAY (self), NULL);

  if (self->panel == NULL)
    return NULL;

  width = gm_display_panel_get_x_res (self->panel);
  height = gm_display_panel_get_y_res (self->panel);
  radius = gm_display_panel_get_border_radius (self->panel);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  cairo_set_line_width (cr, 5.0);
  cairo_set_source_rgba (cr, 0.5f, 0.0f, 0.5f, 0.5f);

  cutouts = gm_display_panel_get_cutouts (self->panel);
  for (int i = 0; i < g_list_model_get_n_items (cutouts); i++) {
    g_autoptr (GmCutout) cutout = g_list_model_get_item (cutouts, i);
    const GmRect *bounds = gm_cutout_get_bounds (cutout);

    cairo_rectangle (cr, bounds->x, bounds->y, bounds->width, bounds->height);
    cairo_fill (cr);
  }

  /* top left */
  cairo_move_to (cr, 0, 0);
  cairo_arc (cr, radius, radius, radius, M_PI, 1.5 * M_PI);
  cairo_close_path (cr);
  cairo_fill (cr);

  /* top right */
  cairo_move_to (cr, width, 0);
  cairo_arc (cr, width - radius, radius, radius, 1.5 * M_PI, 2 * M_PI);
  cairo_close_path (cr);
  cairo_fill (cr);

  /* bottom right */
  cairo_move_to (cr, width, height);
  cairo_arc (cr, width - radius, height - radius, radius, 0, 0.5 * M_PI);
  cairo_close_path (cr);
  cairo_fill (cr);

  /* bottom left */
  cairo_move_to (cr, 0, height);
  cairo_arc (cr, radius, height - radius, radius, 0.5 * M_PI, M_PI);
  cairo_close_path (cr);
  cairo_fill (cr);

  cairo_surface_flush (surface);
  data = cairo_image_surface_get_data (surface);
  stride = cairo_image_surface_get_stride (surface);

  texture = wlr_texture_from_pixels (phoc_renderer_get_wlr_renderer (renderer),
                                     DRM_FORMAT_ARGB8888, stride, width, height, data);

  return texture;
}
