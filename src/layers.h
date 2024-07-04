#pragma once

#include "gesture.h"

#include <stdbool.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/box.h>
#include "output.h"
#include "layer-surface.h"

G_BEGIN_DECLS

enum layer_parent {
  LAYER_PARENT_LAYER,
  LAYER_PARENT_POPUP,
  LAYER_PARENT_SUBSURFACE
};


typedef struct _PhocLayerPopup PhocLayerPopup;
struct _PhocLayerPopup {
  enum layer_parent parent_type;
  union {
    PhocLayerSurface *parent_layer;
    PhocLayerPopup   *parent_popup;
  };

  struct wlr_xdg_popup *wlr_popup;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener commit;
  struct wl_listener new_popup;
  struct wl_listener new_subsurface;
  struct wl_list subsurfaces; // phoc_layer_subsurface::link
};


typedef struct _PhocLayerSubsurface PhocLayerSubsurface;
typedef struct _PhocLayerSubsurface {
  enum layer_parent parent_type;
  union {
    PhocLayerSurface    *parent_layer;
    PhocLayerPopup      *parent_popup;
    PhocLayerSubsurface *parent_subsurface;
  };
  struct wl_list link;

  struct wlr_subsurface *wlr_subsurface;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener commit;
  struct wl_listener new_subsurface;
  struct wl_list subsurfaces; // phoc_layer_subsurface::link
} PhocLayerSubsurface;

void phoc_layer_shell_arrange (PhocOutput *output);
void phoc_layer_shell_update_focus (void);
void phoc_layer_shell_update_osk (PhocOutput *output, gboolean arrange);
PhocLayerSurface *phoc_layer_shell_find_osk (PhocOutput *output);

void phoc_handle_layer_shell_surface (struct wl_listener *listener, void *data);

G_END_DECLS
