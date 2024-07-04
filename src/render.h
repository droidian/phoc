/*
 * Copyright (C) 2020,2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <glib-object.h>

#include <wlr/render/wlr_renderer.h>

G_BEGIN_DECLS

#define PHOC_TYPE_RENDERER (phoc_renderer_get_type ())

G_DECLARE_FINAL_TYPE (PhocRenderer, phoc_renderer, PHOC, RENDERER, GObject)

typedef struct _PhocOutput PhocOutput;
typedef struct _PhocView PhocView;


typedef struct _PhocRenderContext {
  PhocOutput                 *output;
  pixman_region32_t          *damage;
  float                       alpha;
  struct wlr_render_pass     *render_pass;
  enum wlr_scale_filter_mode  tex_filter;
} PhocRenderContext;


PhocRenderer *phoc_renderer_new (struct wlr_backend *wlr_backend, GError **error);

void          phoc_renderer_render_output (PhocRenderer      *self,
                                           PhocOutput        *output,
                                           PhocRenderContext *context);
gboolean      phoc_renderer_render_view_to_buffer (PhocRenderer           *self,
                                                   PhocView               *view,
                                                   struct wlr_buffer      *data);

G_END_DECLS
