#pragma once

#include "gesture.h"

#include <stdbool.h>
#include <wlr/types/wlr_surface.h>
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

typedef struct phoc_layer_popup {
	enum layer_parent parent_type;
	union {
		PhocLayerSurface *parent_layer;
		struct phoc_layer_popup *parent_popup;
	};

	struct wlr_xdg_popup *wlr_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener new_popup;
	struct wl_listener new_subsurface;
	struct wl_list subsurfaces; // phoc_layer_subsurface::link
} PhocLayerPopup;

typedef struct phoc_layer_subsurface {
	enum layer_parent parent_type;
	union {
		PhocLayerSurface *parent_layer;
		struct phoc_layer_popup *parent_popup;
		struct phoc_layer_subsurface *parent_subsurface;
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
PhocLayerSurface *phoc_layer_shell_find_osk (PhocOutput *output);

G_END_DECLS
