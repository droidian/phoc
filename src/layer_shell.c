#define G_LOG_DOMAIN "phoc-layer-shell"

#include "config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include "cursor.h"
#include "desktop.h"
#include "layers.h"
#include "output.h"
#include "seat.h"
#include "server.h"

#define LAYER_SHELL_LAYER_COUNT 4

static void apply_exclusive(struct wlr_box *usable_area,
		uint32_t anchor, int32_t exclusive,
		int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left) {
	if (exclusive <= 0) {
		return;
	}
	struct {
		uint32_t anchors;
		int *positive_axis;
		int *negative_axis;
		int margin;
	} edges[] = {
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.positive_axis = &usable_area->y,
			.negative_axis = &usable_area->height,
			.margin = margin_top,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->height,
			.margin = margin_bottom,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = &usable_area->x,
			.negative_axis = &usable_area->width,
			.margin = margin_left,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->width,
			.margin = margin_right,
		},
	};
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
		if ((anchor & edges[i].anchors) == edges[i].anchors && exclusive + edges[i].margin > 0) {
			if (edges[i].positive_axis) {
				*edges[i].positive_axis += exclusive + edges[i].margin;
			}
			if (edges[i].negative_axis) {
				*edges[i].negative_axis -= exclusive + edges[i].margin;
			}
		}
	}
}

static void update_cursors(PhocLayerSurface *roots_surface,
		GSList *seats /* PhocSeat */) {
	PhocServer *server = phoc_server_get_default ();

	for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
		PhocSeat *seat = PHOC_SEAT (elem->data);

		g_assert (PHOC_IS_SEAT (seat));
		PhocCursor *cursor = phoc_seat_get_cursor(seat);
		double sx, sy;

		struct wlr_surface *surface = phoc_desktop_surface_at(
			server->desktop,
			cursor->cursor->x, cursor->cursor->y, &sx, &sy, NULL);

		if (surface == roots_surface->layer_surface->surface) {
			struct timespec time;
			if (clock_gettime(CLOCK_MONOTONIC, &time) == 0) {
				phoc_cursor_update_position(cursor,
							    time.tv_sec * 1000 + time.tv_nsec / 1000000);
			} else {
				g_critical ("Failed to get time, not updating position. Errno: %s\n",
					    strerror(errno));
			}
		}
	}
}

static void arrange_layer(struct wlr_output *output,
		GSList *seats /* PhocSeat */,
		struct wl_list *list /* PhocLayerSurface */,
		struct wlr_box *usable_area, bool exclusive) {
	PhocLayerSurface *roots_surface;
	struct wlr_box full_area = { 0 };
	wlr_output_effective_resolution(output,
			&full_area.width, &full_area.height);
	wl_list_for_each_reverse(roots_surface, list, link) {
		struct wlr_layer_surface_v1 *layer = roots_surface->layer_surface;
		struct wlr_layer_surface_v1_state *state = &layer->current;
		if (exclusive != (state->exclusive_zone > 0)) {
			continue;
		}
		struct wlr_box bounds;
		if (state->exclusive_zone == -1) {
			bounds = full_area;
		} else {
			bounds = *usable_area;
		}
		struct wlr_box box = {
			.width = state->desired_width,
			.height = state->desired_height
		};
		// Horizontal axis
		const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		if ((state->anchor & both_horiz) && box.width == 0) {
			box.x = bounds.x;
			box.width = bounds.width;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x = bounds.x;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x = bounds.x + (bounds.width - box.width);
		} else {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		}
		// Vertical axis
		const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		if ((state->anchor & both_vert) && box.height == 0) {
			box.y = bounds.y;
			box.height = bounds.height;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y = bounds.y;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y = bounds.y + (bounds.height - box.height);
		} else {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		}
		// Margin
		if ((state->anchor & both_horiz) == both_horiz) {
			box.x += state->margin.left;
			box.width -= state->margin.left + state->margin.right;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x += state->margin.left;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x -= state->margin.right;
		}
		if ((state->anchor & both_vert) == both_vert) {
			box.y += state->margin.top;
			box.height -= state->margin.top + state->margin.bottom;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y += state->margin.top;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y -= state->margin.bottom;
		}
		if (box.width < 0 || box.height < 0) {
			// TODO: Bubble up a protocol error?
			wlr_layer_surface_v1_close(layer);
			continue;
		}

		// Apply
		struct wlr_box old_geo = roots_surface->geo;
		roots_surface->geo = box;
		if (layer->mapped) {
			apply_exclusive(usable_area, state->anchor, state->exclusive_zone,
					state->margin.top, state->margin.right,
					state->margin.bottom, state->margin.left);
		}
		wlr_layer_surface_v1_configure(layer, box.width, box.height);

		// Having a cursor newly end up over the moved layer will not
		// automatically send a motion event to the surface. The event needs to
		// be synthesized.
		// Only update layer surfaces which kept their size (and so buffers) the
		// same, because those with resized buffers will be handled separately.

		if (roots_surface->geo.x != old_geo.x
				|| roots_surface->geo.y != old_geo.y) {
			update_cursors(roots_surface, seats);
		}
	}
}

struct osk_origin {
	struct wlr_layer_surface_v1_state state;
	PhocLayerSurface *surface;
	enum zwlr_layer_shell_v1_layer layer;
};

static struct osk_origin find_osk(struct wl_list layers[LAYER_SHELL_LAYER_COUNT]) {
	struct osk_origin origin = {0};
	for (unsigned i = 0; i < LAYER_SHELL_LAYER_COUNT; i++) {
		struct wl_list *list = &layers[i];
		PhocLayerSurface *roots_surface;
		wl_list_for_each(roots_surface, list, link) {
			if (strcmp(roots_surface->layer_surface->namespace, "osk") == 0) {
				origin.state = roots_surface->layer_surface->current;
				origin.surface = roots_surface;
				origin.layer = i;
				return origin;
			}
		}
	}
	return origin;
}

/// Adjusts keyboard properties
static void change_osk(const struct osk_origin *osk, struct wl_list layers[LAYER_SHELL_LAYER_COUNT], bool force_overlay) {
	if (force_overlay && osk->layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
		wl_list_remove(&osk->surface->link);
		wl_list_insert(&layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &osk->surface->link);
	}

	if (!force_overlay && osk->layer != osk->surface->layer_surface->client_pending.layer) {
		wl_list_remove(&osk->surface->link);
		wl_list_insert(&layers[osk->surface->layer_surface->client_pending.layer], &osk->surface->link);
	}
}

void
phoc_layer_shell_arrange (PhocOutput *output)
{
  struct wlr_box usable_area = { 0 };
  PhocServer *server = phoc_server_get_default ();
  GSList *seats = phoc_input_get_seats (server->input);
  enum zwlr_layer_shell_v1_layer layers[] = {
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND
  };

  wlr_output_effective_resolution (output->wlr_output, &usable_area.width, &usable_area.height);

  struct osk_origin osk_place = find_osk (output->layers);
  if (osk_place.surface) {
    bool osk_force_overlay = false;

    for (GSList *elem = seats; elem; elem = elem->next) {
      PhocSeat *seat = PHOC_SEAT (elem->data);

      g_assert (PHOC_IS_SEAT (seat));
      if (seat->focused_layer && seat->focused_layer->client_pending.layer >= osk_place.surface->layer_surface->client_pending.layer) {
        osk_force_overlay = true;
        break;
      }
    }
    change_osk (&osk_place, output->layers, osk_force_overlay);
  }

  // Arrange exclusive surfaces from top->bottom
  for (size_t i = 0; i < G_N_ELEMENTS(layers); ++i)
    arrange_layer (output->wlr_output, seats, &output->layers[layers[i]], &usable_area, true);
  output->usable_area = usable_area;

  PhocView *view;
  wl_list_for_each (view, &output->desktop->views, link) {
    if (view_is_maximized (view)) {
      view_arrange_maximized (view, NULL);
    } else if (view_is_tiled (view)) {
      view_arrange_tiled (view, NULL);
    } else if (output->desktop->maximize) {
      view_center (view, NULL);
    }
  }

  // Arrange non-exlusive surfaces from top->bottom
  for (size_t i = 0; i < G_N_ELEMENTS(layers); ++i)
    arrange_layer (output->wlr_output, seats, &output->layers[layers[i]], &usable_area, false);
}

void
phoc_layer_shell_update_focus (void)
{
  PhocServer *server = phoc_server_get_default ();
  enum zwlr_layer_shell_v1_layer layers_above_shell[] = {
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP,
  };
  PhocLayerSurface *layer, *topmost = NULL;
  // Find topmost keyboard interactive layer, if such a layer exists
  // TODO: Make layer surface focus per-output based on cursor position
  PhocOutput *output;
  wl_list_for_each (output, &server->desktop->outputs, link) {
    for (size_t i = 0; i < G_N_ELEMENTS(layers_above_shell); ++i) {
      if (output->fullscreen_view && !output->force_shell_reveal) {
        if (layers_above_shell[i] == ZWLR_LAYER_SHELL_V1_LAYER_TOP)
          continue;
      }
      wl_list_for_each(layer, &output->layers[layers_above_shell[i]], link) {
        if (layer->layer_surface->current.keyboard_interactive && layer->layer_surface->mapped) {
          topmost = layer;
          break;
        }
      }
      if (topmost != NULL)
        break;
    }
  }

  for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    phoc_seat_set_focus_layer(seat, topmost ? topmost->layer_surface : NULL);
  }
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	PhocLayerSurface *layer =
		wl_container_of(listener, layer, output_destroy);
	layer->layer_surface->output = NULL;
	wl_list_remove(&layer->output_destroy.link);
	wlr_layer_surface_v1_close(layer->layer_surface);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocLayerSurface *layer =
		wl_container_of(listener, layer, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = layer->layer_surface;
	struct wlr_output *wlr_output = layer_surface->output;
	if (wlr_output != NULL) {
		PhocOutput *output = wlr_output->data;
		struct wlr_box old_geo = layer->geo;

		bool layer_changed = layer->layer != layer_surface->current.layer;
		if (layer_changed) {
			wl_list_remove(&layer->link);
			wl_list_insert(&output->layers[layer_surface->current.layer],
				&layer->link);
			layer->layer = layer_surface->current.layer;
		}

		phoc_layer_shell_arrange (output);
		phoc_layer_shell_update_focus ();

		// Cursor changes which happen as a consequence of resizing a layer
		// surface are applied in phoc_layer_shell_arrange. Because the resize happens
		// before the underlying surface changes, it will only receive a cursor
		// update if the new cursor position crosses the *old* sized surface in
		// the *new* layer surface.
		// Another cursor move event is needed when the surface actually
		// changes.
		struct wlr_surface *surface = layer_surface->surface;
		if (surface->previous.width != surface->current.width ||
				surface->previous.height != surface->current.height) {
			update_cursors(layer, phoc_input_get_seats (server->input));
		}

		bool geo_changed =
			memcmp(&old_geo, &layer->geo, sizeof(struct wlr_box)) != 0;
		if (geo_changed || layer_changed) {
			phoc_output_damage_whole_local_surface(output, layer_surface->surface,
							       old_geo.x, old_geo.y);
			phoc_output_damage_whole_local_surface(output, layer_surface->surface,
							       layer->geo.x, layer->geo.y);
		} else {
			phoc_output_damage_from_local_surface(output, layer_surface->surface,
							      layer->geo.x, layer->geo.y);
		}
	}
}


static void handle_destroy(struct wl_listener *listener, void *data) {
	PhocLayerSurface *layer = wl_container_of(
			listener, layer, destroy);
	g_object_unref (layer);
}

static void subsurface_destroy(struct roots_layer_subsurface *subsurface) {
	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->commit.link);
	wl_list_remove(&subsurface->link);
	free(subsurface);
}

static struct roots_layer_popup *popup_create(struct wlr_xdg_popup *wlr_popup);
static struct roots_layer_subsurface *layer_subsurface_create(struct wlr_subsurface *wlr_subsurface);

static PhocLayerSurface *popup_get_root_layer(struct roots_layer_popup *popup) {
	while (popup->parent_type == LAYER_PARENT_POPUP) {
		popup = popup->parent_popup;
	}
	return popup->parent_layer;
}

static void popup_unconstrain(struct roots_layer_popup *popup) {
	PhocLayerSurface *layer = popup_get_root_layer(popup);
	struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;

	PhocOutput *output = phoc_layer_surface_get_output (layer);

	// the output box expressed in the coordinate system of the toplevel parent
	// of the popup
	struct wlr_box output_toplevel_sx_box = {
		.x = -layer->geo.x,
		.y = -layer->geo.y,
		.width = output->usable_area.width,
		.height = output->usable_area.height,
	};

	wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static void popup_damage(struct roots_layer_popup *layer_popup, bool whole) {
	struct wlr_xdg_popup *popup = layer_popup->wlr_popup;
	struct wlr_surface *surface = popup->base->surface;
	int popup_sx = popup->geometry.x - popup->base->geometry.x;
	int popup_sy = popup->geometry.y - popup->base->geometry.y;
	int ox = popup_sx, oy = popup_sy;
	PhocLayerSurface *layer;
	while (layer_popup->parent_type == LAYER_PARENT_POPUP) {
		layer_popup = layer_popup->parent_popup;
		ox += layer_popup->wlr_popup->geometry.x;
		oy += layer_popup->wlr_popup->geometry.y;
	}
	layer = layer_popup->parent_layer;
	ox += layer->geo.x;
	oy += layer->geo.y;

	PhocOutput *output = phoc_layer_surface_get_output (layer);
	if (!output) {
		return;
	}

	if (whole) {
		phoc_output_damage_whole_local_surface(output, surface, ox, oy);
	} else {
		phoc_output_damage_from_local_surface(output, surface, ox, oy);
	}
}

static void popup_new_popup(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup =
		wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	struct roots_layer_popup *new_popup = popup_create(wlr_popup);
	new_popup->parent_type = LAYER_PARENT_POPUP;
	new_popup->parent_popup = popup;
	popup_unconstrain(new_popup);
}

static void popup_new_subsurface(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup =
		wl_container_of(listener, popup, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	struct roots_layer_subsurface *subsurface = layer_subsurface_create(wlr_subsurface);
	subsurface->parent_type = LAYER_PARENT_POPUP;
	subsurface->parent_popup = popup;
	wl_list_insert(&popup->subsurfaces, &subsurface->link);
}

static void popup_handle_map(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_popup *popup = wl_container_of(listener, popup, map);
	PhocLayerSurface *layer = popup_get_root_layer(popup);
	struct wlr_output *wlr_output = layer->layer_surface->output;
	if (!wlr_output) {
		return;
	}

	struct wlr_subsurface *child;
	wl_list_for_each(child, &popup->wlr_popup->base->surface->subsurfaces_below, parent_link) {
		struct roots_layer_subsurface *new_subsurface = layer_subsurface_create(child);
		new_subsurface->parent_type = LAYER_PARENT_POPUP;
		new_subsurface->parent_popup = popup;
		wl_list_insert(&popup->subsurfaces, &new_subsurface->link);
	}
	wl_list_for_each(child, &popup->wlr_popup->base->surface->subsurfaces_above, parent_link) {
		struct roots_layer_subsurface *new_subsurface = layer_subsurface_create(child);
		new_subsurface->parent_type = LAYER_PARENT_POPUP;
		new_subsurface->parent_popup = popup;
		wl_list_insert(&popup->subsurfaces, &new_subsurface->link);
	}
	popup->new_subsurface.notify = popup_new_subsurface;
	wl_signal_add(&popup->wlr_popup->base->surface->events.new_subsurface, &popup->new_subsurface);

	wlr_surface_send_enter(popup->wlr_popup->base->surface, wlr_output);
	popup_damage(popup, true);
	phoc_input_update_cursor_focus(server->input);
}

static void popup_handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_popup *popup = wl_container_of(listener, popup, unmap);
	struct roots_layer_subsurface *child, *tmp;
	wl_list_for_each_safe(child, tmp, &popup->subsurfaces, link) {
		subsurface_destroy(child);
	}
	wl_list_remove(&popup->new_subsurface.link);
	popup_damage(popup, true);
	phoc_input_update_cursor_focus(server->input);
}

static void popup_handle_commit(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup = wl_container_of(listener, popup, commit);
	popup_damage(popup, false);
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup =
		wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->unmap.link);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->new_popup.link);
	free(popup);
}

static struct roots_layer_popup *popup_create(struct wlr_xdg_popup *wlr_popup) {
	struct roots_layer_popup *popup =
		calloc(1, sizeof(struct roots_layer_popup));
	if (popup == NULL) {
		return NULL;
	}
	popup->wlr_popup = wlr_popup;
	popup->map.notify = popup_handle_map;
	wl_signal_add(&wlr_popup->base->events.map, &popup->map);
	popup->unmap.notify = popup_handle_unmap;
	wl_signal_add(&wlr_popup->base->events.unmap, &popup->unmap);
	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);
	popup->commit.notify = popup_handle_commit;
	wl_signal_add(&wlr_popup->base->surface->events.commit, &popup->commit);
	popup->new_popup.notify = popup_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

	wl_list_init(&popup->subsurfaces);

	return popup;
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	PhocLayerSurface *roots_layer_surface =
		wl_container_of(listener, roots_layer_surface, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	struct roots_layer_popup *popup = popup_create(wlr_popup);
	popup->parent_type = LAYER_PARENT_LAYER;
	popup->parent_layer = roots_layer_surface;
	popup_unconstrain(popup);
}

static PhocLayerSurface *subsurface_get_root_layer(struct roots_layer_subsurface *subsurface) {
	while (subsurface->parent_type == LAYER_PARENT_SUBSURFACE) {
		subsurface = subsurface->parent_subsurface;
	}
	if (subsurface->parent_type == LAYER_PARENT_POPUP) {
		return popup_get_root_layer(subsurface->parent_popup);
	}
	return subsurface->parent_layer;
}

static void subsurface_damage(struct roots_layer_subsurface *subsurface, bool whole) {
	PhocLayerSurface *layer = subsurface_get_root_layer(subsurface);
	PhocOutput *output = phoc_layer_surface_get_output (layer);
	if (!output) {
		return;
	}
	int ox = subsurface->wlr_subsurface->current.x + layer->geo.x;
	int oy = subsurface->wlr_subsurface->current.y + layer->geo.y;
	if (whole) {
		phoc_output_damage_whole_local_surface(output,
						       subsurface->wlr_subsurface->surface,
						       ox, oy);
	} else {
		phoc_output_damage_from_local_surface(output,
						      subsurface->wlr_subsurface->surface,
						      ox, oy);
	}
}

static void subsurface_new_subsurface(struct wl_listener *listener, void *data) {
	struct roots_layer_subsurface *subsurface =
		wl_container_of(listener, subsurface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;

	struct roots_layer_subsurface *new_subsurface = layer_subsurface_create(wlr_subsurface);
	new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
	new_subsurface->parent_subsurface = subsurface;
	wl_list_insert(&subsurface->subsurfaces, &new_subsurface->link);
}

static void subsurface_handle_map(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_subsurface *subsurface = wl_container_of(listener, subsurface, map);

	struct wlr_subsurface *child;
	wl_list_for_each(child, &subsurface->wlr_subsurface->surface->subsurfaces_below, parent_link) {
		struct roots_layer_subsurface *new_subsurface = layer_subsurface_create(child);
		new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
		new_subsurface->parent_subsurface = subsurface;
		wl_list_insert(&subsurface->subsurfaces, &new_subsurface->link);
	}
	wl_list_for_each(child, &subsurface->wlr_subsurface->surface->subsurfaces_above, parent_link) {
		struct roots_layer_subsurface *new_subsurface = layer_subsurface_create(child);
		new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
		new_subsurface->parent_subsurface = subsurface;
		wl_list_insert(&subsurface->subsurfaces, &new_subsurface->link);
	}
	subsurface->new_subsurface.notify = subsurface_new_subsurface;
	wl_signal_add(&subsurface->wlr_subsurface->surface->events.new_subsurface, &subsurface->new_subsurface);

	wlr_surface_send_enter(subsurface->wlr_subsurface->surface, subsurface_get_root_layer(subsurface)->layer_surface->output);
	subsurface_damage(subsurface, true);
	phoc_input_update_cursor_focus(server->input);
}

static void subsurface_handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_subsurface *subsurface = wl_container_of(listener, subsurface, unmap);
	struct roots_layer_subsurface *child, *tmp;
	wl_list_for_each_safe(child, tmp, &subsurface->subsurfaces, link) {
		subsurface_destroy(child);
	}
	wl_list_remove(&subsurface->new_subsurface.link);
	subsurface_damage(subsurface, true);
	phoc_input_update_cursor_focus(server->input);
}

static void subsurface_handle_commit(struct wl_listener *listener, void *data) {
	struct roots_layer_subsurface *subsurface = wl_container_of(listener, subsurface, commit);
	subsurface_damage(subsurface, false);
}

static void subsurface_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_subsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);

	subsurface_destroy(subsurface);
}

static struct roots_layer_subsurface *layer_subsurface_create(struct wlr_subsurface *wlr_subsurface) {
	struct roots_layer_subsurface *subsurface =
		calloc(1, sizeof(struct roots_layer_subsurface));
	if (subsurface == NULL) {
		return NULL;
	}
	subsurface->wlr_subsurface = wlr_subsurface;

	subsurface->map.notify = subsurface_handle_map;
	wl_signal_add(&wlr_subsurface->events.map, &subsurface->map);
	subsurface->unmap.notify = subsurface_handle_unmap;
	wl_signal_add(&wlr_subsurface->events.unmap, &subsurface->unmap);
	subsurface->destroy.notify = subsurface_handle_destroy;
	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->commit.notify = subsurface_handle_commit;
	wl_signal_add(&wlr_subsurface->surface->events.commit, &subsurface->commit);

	wl_list_init(&subsurface->subsurfaces);
	wl_list_init(&subsurface->link);

	return subsurface;
}

static void handle_new_subsurface(struct wl_listener *listener, void *data) {
	PhocLayerSurface *roots_layer_surface =
		wl_container_of(listener, roots_layer_surface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;

	struct roots_layer_subsurface *subsurface = layer_subsurface_create(wlr_subsurface);
	subsurface->parent_type = LAYER_PARENT_LAYER;
	subsurface->parent_layer = roots_layer_surface;
	wl_list_insert(&roots_layer_surface->subsurfaces, &subsurface->link);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface_v1 *layer_surface = data;
	PhocLayerSurface *layer = PHOC_LAYER_SURFACE (layer_surface->data);
	PhocOutput *output = phoc_layer_surface_get_output (layer);
	if (!output) {
		return;
	}

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &layer_surface->surface->subsurfaces_below, parent_link) {
		struct roots_layer_subsurface *roots_subsurface = layer_subsurface_create(subsurface);
		roots_subsurface->parent_type = LAYER_PARENT_LAYER;
		roots_subsurface->parent_layer = layer;
		wl_list_insert(&layer->subsurfaces, &roots_subsurface->link);
	}
	wl_list_for_each(subsurface, &layer_surface->surface->subsurfaces_above, parent_link) {
		struct roots_layer_subsurface *roots_subsurface = layer_subsurface_create(subsurface);
		roots_subsurface->parent_type = LAYER_PARENT_LAYER;
		roots_subsurface->parent_layer = layer;
		wl_list_insert(&layer->subsurfaces, &roots_subsurface->link);
	}

	layer->new_subsurface.notify = handle_new_subsurface;
	wl_signal_add(&layer_surface->surface->events.new_subsurface, &layer->new_subsurface);

	phoc_output_damage_whole_local_surface(output,
					       layer_surface->surface, layer->geo.x,
					       layer->geo.y);
	wlr_surface_send_enter(layer_surface->surface, output->wlr_output);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocLayerSurface *layer = wl_container_of(
			listener, layer, unmap);

	struct roots_layer_subsurface *subsurface, *tmp;
	wl_list_for_each_safe(subsurface, tmp, &layer->subsurfaces, link) {
		subsurface_destroy(subsurface);
	}
	wl_list_remove(&layer->new_subsurface.link);

	phoc_layer_surface_unmap (layer);
	phoc_input_update_cursor_focus(server->input);
}

void handle_layer_shell_surface(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct wlr_layer_surface_v1 *layer_surface = data;
	PhocDesktop *desktop =
		wl_container_of(listener, desktop, layer_shell_surface);
	g_debug ("new layer surface: namespace %s layer %d anchor %d "
			"size %dx%d margin %d,%d,%d,%d",
		layer_surface->namespace, layer_surface->client_pending.layer,
		layer_surface->client_pending.anchor,
		layer_surface->client_pending.desired_width,
		layer_surface->client_pending.desired_height,
		layer_surface->client_pending.margin.top,
		layer_surface->client_pending.margin.right,
		layer_surface->client_pending.margin.bottom,
		layer_surface->client_pending.margin.left);

	if (!layer_surface->output) {
		PhocInput *input = server->input;
		PhocSeat *seat = phoc_input_get_last_active_seat(input);
		assert(seat); // Technically speaking we should handle this case
		PhocCursor *cursor = phoc_seat_get_cursor(seat);
		struct wlr_output *output =
			wlr_output_layout_output_at(desktop->layout,
					cursor->cursor->x,
					cursor->cursor->y);
		if (!output) {
			g_critical ("Couldn't find output at (%.0f,%.0f)",
                                    cursor->cursor->x,
                                    cursor->cursor->y);
			output = wlr_output_layout_get_center_output(desktop->layout);
		}
		if (output) {
			layer_surface->output = output;
		} else {
			wlr_layer_surface_v1_close(layer_surface);
			return;
		}
	}

	PhocLayerSurface *roots_surface = phoc_layer_surface_new ();
	if (!roots_surface) {
		return;
	}

	wl_list_init(&roots_surface->subsurfaces);

	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit,
		&roots_surface->surface_commit);

	roots_surface->output_destroy.notify = handle_output_destroy;
	wl_signal_add(&layer_surface->output->events.destroy,
		&roots_surface->output_destroy);

	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&layer_surface->events.destroy, &roots_surface->destroy);
	roots_surface->map.notify = handle_map;
	wl_signal_add(&layer_surface->events.map, &roots_surface->map);
	roots_surface->unmap.notify = handle_unmap;
	wl_signal_add(&layer_surface->events.unmap, &roots_surface->unmap);
	roots_surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&layer_surface->events.new_popup, &roots_surface->new_popup);

	roots_surface->layer_surface = layer_surface;
	layer_surface->data = roots_surface;

	PhocOutput *output = layer_surface->output->data;
	wl_list_insert(&output->layers[layer_surface->client_pending.layer], &roots_surface->link);

	// Temporarily set the layer's current state to client_pending
	// So that we can easily arrange it
	struct wlr_layer_surface_v1_state old_state = layer_surface->current;
	layer_surface->current = layer_surface->client_pending;

	phoc_layer_shell_arrange (output);
	phoc_layer_shell_update_focus ();

	layer_surface->current = old_state;
}
