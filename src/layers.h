#pragma once

#include <stdbool.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include "output.h"
#include "layer-surface.h"

G_BEGIN_DECLS

enum layer_parent {
	LAYER_PARENT_LAYER,
	LAYER_PARENT_POPUP,
	LAYER_PARENT_SUBSURFACE
};

struct roots_layer_popup {
	enum layer_parent parent_type;
	union {
		PhocLayerSurface *parent_layer;
		struct roots_layer_popup *parent_popup;
	};

	struct wlr_xdg_popup *wlr_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener new_popup;
	struct wl_listener new_subsurface;
	struct wl_list subsurfaces; // roots_layer_subsurface::link
};

struct roots_layer_subsurface {
	enum layer_parent parent_type;
	union {
		PhocLayerSurface *parent_layer;
		struct roots_layer_popup *parent_popup;
		struct roots_layer_subsurface *parent_subsurface;
	};
	struct wl_list link;

	struct wlr_subsurface *wlr_subsurface;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener new_subsurface;
	struct wl_list subsurfaces; // roots_layer_subsurface::link
};

void phoc_layer_shell_arrange (PhocOutput *output);
void phoc_layer_shell_update_focus (void);

G_END_DECLS
