#define G_LOG_DOMAIN "phoc-layer-shell"

#include "config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/box.h>
#include "cursor.h"
#include "desktop.h"
#include "layers.h"
#include "output.h"
#include "seat.h"
#include "server.h"

#include <glib.h>

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

static void update_cursors(PhocLayerSurface *layer_surface, GSList *seats /* PhocSeat */) {
	PhocServer *server = phoc_server_get_default ();

	for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
		PhocSeat *seat = PHOC_SEAT (elem->data);

		g_assert (PHOC_IS_SEAT (seat));
		PhocCursor *cursor = phoc_seat_get_cursor(seat);
		double sx, sy;

		struct wlr_surface *surface = phoc_desktop_surface_at(
			server->desktop,
			cursor->cursor->x, cursor->cursor->y, &sx, &sy, NULL);

		if (surface == layer_surface->layer_surface->surface) {
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

static void arrange_layer(PhocOutput *output,
		GSList *seats /* PhocSeat */,
		enum zwlr_layer_shell_v1_layer layer,
		struct wlr_box *usable_area, bool exclusive) {
	PhocLayerSurface *layer_surface;
	struct wlr_box full_area = { 0 };

	g_assert (PHOC_IS_OUTPUT (output));
	wlr_output_effective_resolution(output->wlr_output,
			&full_area.width, &full_area.height);
	wl_list_for_each_reverse(layer_surface, &output->layer_surfaces, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->layer_surface;
		struct wlr_layer_surface_v1_state *state = &wlr_layer_surface->current;

		if (layer_surface->layer != layer)
		  continue;

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
			wlr_layer_surface_v1_destroy(wlr_layer_surface);
			continue;
		}

		// Apply
		struct wlr_box old_geo = layer_surface->geo;
		layer_surface->geo = box;
		if (wlr_layer_surface->mapped) {
			apply_exclusive(usable_area, state->anchor, state->exclusive_zone,
					state->margin.top, state->margin.right,
					state->margin.bottom, state->margin.left);
		}

		if (box.width != old_geo.width || box.height != old_geo.height)
			wlr_layer_surface_v1_configure(wlr_layer_surface, box.width, box.height);

		// Having a cursor newly end up over the moved layer will not
		// automatically send a motion event to the surface. The event needs to
		// be synthesized.
		// Only update layer surfaces which kept their size (and so buffers) the
		// same, because those with resized buffers will be handled separately.

		if (layer_surface->geo.x != old_geo.x
				|| layer_surface->geo.y != old_geo.y) {
			update_cursors(layer_surface, seats);
		}
	}
}

static PhocLayerSurface *
find_osk (PhocOutput *output)
{
  PhocLayerSurface *layer_surface;

  wl_list_for_each(layer_surface, &output->layer_surfaces, link) {
    if (strcmp(layer_surface->layer_surface->namespace, "osk") == 0)
      return layer_surface;
  }

  return NULL;
}

/// Adjusts keyboard properties
static void
change_osk (PhocLayerSurface *osk, struct wl_list layer_surfaces, bool force_overlay)
{
  if (force_overlay && osk->layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
    osk->layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;

  if (!force_overlay && osk->layer != osk->layer_surface->pending.layer)
    osk->layer = osk->layer_surface->pending.layer;
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

  PhocLayerSurface *osk = find_osk (output);
  if (osk) {
    bool osk_force_overlay = false;

    for (GSList *elem = seats; elem; elem = elem->next) {
      PhocSeat *seat = PHOC_SEAT (elem->data);

      g_assert (PHOC_IS_SEAT (seat));
      if (seat->focused_layer && seat->focused_layer->pending.layer >= osk->layer_surface->pending.layer &&
          phoc_input_method_relay_is_enabled (&seat->im_relay, seat->focused_layer->surface)) {
        osk_force_overlay = true;
        break;
      }
    }
    change_osk (osk, output->layer_surfaces, osk_force_overlay);
  }

  // Arrange exclusive surfaces from top->bottom
  for (size_t i = 0; i < G_N_ELEMENTS(layers); ++i)
    arrange_layer (output, seats, layers[i], &usable_area, true);
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
    arrange_layer (output, seats, layers[i], &usable_area, false);
}

void
phoc_layer_shell_update_focus (void)
{
  PhocServer *server = phoc_server_get_default ();
  enum zwlr_layer_shell_v1_layer layers_above_shell[] = {
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP,
  };
  PhocLayerSurface *layer_surface, *topmost = NULL;
  // Find topmost keyboard interactive layer, if such a layer exists
  // TODO: Make layer surface focus per-output based on cursor position
  PhocOutput *output;
  wl_list_for_each (output, &server->desktop->outputs, link) {
    for (size_t i = 0; i < G_N_ELEMENTS(layers_above_shell); ++i) {
      wl_list_for_each_reverse (layer_surface, &output->layer_surfaces, link) {
        if (layer_surface->layer != layers_above_shell[i])
          continue;

        if (layer_surface->layer_surface->current.exclusive_zone <= 0)
          continue;

        if (layer_surface->layer_surface->current.keyboard_interactive &&
            layer_surface->layer_surface->mapped) {
          topmost = layer_surface;
          break;
        }
      }

      if (topmost != NULL)
        break;

      wl_list_for_each (layer_surface, &output->layer_surfaces, link) {
        if (layer_surface->layer != layers_above_shell[i])
          continue;

        if (layer_surface->layer_surface->current.exclusive_zone > 0)
          continue;

        if (layer_surface->layer_surface->current.keyboard_interactive &&
            layer_surface->layer_surface->mapped) {
          topmost = layer_surface;
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

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocLayerSurface *layer_surface = wl_container_of(listener, layer_surface, surface_commit);
	struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->layer_surface;
	struct wlr_output *wlr_output = wlr_layer_surface->output;
	if (wlr_output != NULL) {
		PhocOutput *output = wlr_output->data;
		struct wlr_box old_geo = layer_surface->geo;

		bool layer_changed = false;
		if (wlr_layer_surface->current.committed != 0) {
			layer_changed = layer_surface->layer != wlr_layer_surface->current.layer;

			layer_surface->layer = wlr_layer_surface->current.layer;
			phoc_layer_shell_arrange (output);
			phoc_layer_shell_update_focus ();
		}

		// Cursor changes which happen as a consequence of resizing a layer
		// surface are applied in phoc_layer_shell_arrange. Because the resize happens
		// before the underlying surface changes, it will only receive a cursor
		// update if the new cursor position crosses the *old* sized surface in
		// the *new* layer surface.
		// Another cursor move event is needed when the surface actually
		// changes.
		struct wlr_surface *surface = wlr_layer_surface->surface;
		if (surface->previous.width != surface->current.width ||
				surface->previous.height != surface->current.height) {
			update_cursors(layer_surface, phoc_input_get_seats (server->input));
		}

		bool geo_changed =
			memcmp(&old_geo, &layer_surface->geo, sizeof(struct wlr_box)) != 0;
		if (geo_changed || layer_changed) {
			phoc_output_damage_whole_local_surface(output, wlr_layer_surface->surface,
							       old_geo.x, old_geo.y);
			phoc_output_damage_whole_local_surface(output, wlr_layer_surface->surface,
							       layer_surface->geo.x, layer_surface->geo.y);
		} else {
			phoc_output_damage_from_local_surface(output, wlr_layer_surface->surface,
							      layer_surface->geo.x, layer_surface->geo.y);
		}
	}
}


static void handle_destroy(struct wl_listener *listener, void *data) {
	PhocLayerSurface *layer_surface = wl_container_of(listener, layer_surface, destroy);
	g_object_unref (layer_surface);
}

static void subsurface_destroy(struct phoc_layer_subsurface *subsurface) {
	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->commit.link);
	wl_list_remove(&subsurface->link);
	free(subsurface);
}

static struct phoc_layer_popup *popup_create(struct wlr_xdg_popup *wlr_popup);
static struct phoc_layer_subsurface *layer_subsurface_create(struct wlr_subsurface *wlr_subsurface);

static PhocLayerSurface *popup_get_root_layer(struct phoc_layer_popup *popup) {
	while (popup->parent_type == LAYER_PARENT_POPUP) {
		popup = popup->parent_popup;
	}
	return popup->parent_layer;
}

static void popup_unconstrain(struct phoc_layer_popup *popup) {
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

static void popup_damage(struct phoc_layer_popup *layer_popup, bool whole) {
	struct wlr_xdg_popup *popup = layer_popup->wlr_popup;
	struct wlr_surface *surface = popup->base->surface;
	int popup_sx = popup->geometry.x - popup->base->current.geometry.x;
	int popup_sy = popup->geometry.y - popup->base->current.geometry.y;
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
	struct phoc_layer_popup *popup =
		wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	struct phoc_layer_popup *new_popup = popup_create(wlr_popup);
	new_popup->parent_type = LAYER_PARENT_POPUP;
	new_popup->parent_popup = popup;
	popup_unconstrain(new_popup);
}

static void popup_new_subsurface(struct wl_listener *listener, void *data) {
	struct phoc_layer_popup *popup =
		wl_container_of(listener, popup, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	struct phoc_layer_subsurface *subsurface = layer_subsurface_create(wlr_subsurface);
	subsurface->parent_type = LAYER_PARENT_POPUP;
	subsurface->parent_popup = popup;
	wl_list_insert(&popup->subsurfaces, &subsurface->link);
}

static void popup_handle_map(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct phoc_layer_popup *popup = wl_container_of(listener, popup, map);
	PhocLayerSurface *layer = popup_get_root_layer(popup);
	struct wlr_output *wlr_output = layer->layer_surface->output;
	if (!wlr_output) {
		return;
	}

	struct wlr_subsurface *child;
	wl_list_for_each(child, &popup->wlr_popup->base->surface->current.subsurfaces_below, current.link) {
		struct phoc_layer_subsurface *new_subsurface = layer_subsurface_create(child);
		new_subsurface->parent_type = LAYER_PARENT_POPUP;
		new_subsurface->parent_popup = popup;
		wl_list_insert(&popup->subsurfaces, &new_subsurface->link);
	}
	wl_list_for_each(child, &popup->wlr_popup->base->surface->current.subsurfaces_above, current.link) {
		struct phoc_layer_subsurface *new_subsurface = layer_subsurface_create(child);
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
	struct phoc_layer_popup *popup = wl_container_of(listener, popup, unmap);
	struct phoc_layer_subsurface *child, *tmp;
	wl_list_for_each_safe(child, tmp, &popup->subsurfaces, link) {
		subsurface_destroy(child);
	}
	wl_list_remove(&popup->new_subsurface.link);
	popup_damage(popup, true);
	phoc_input_update_cursor_focus(server->input);
}

static void popup_handle_commit(struct wl_listener *listener, void *data) {
	struct phoc_layer_popup *popup = wl_container_of(listener, popup, commit);
	popup_damage(popup, false);
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct phoc_layer_popup *popup =
		wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->unmap.link);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->new_popup.link);
	free(popup);
}

static struct phoc_layer_popup *popup_create(struct wlr_xdg_popup *wlr_popup) {
	struct phoc_layer_popup *popup =
		calloc(1, sizeof(struct phoc_layer_popup));
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
	PhocLayerSurface *phoc_layer_surface =
		wl_container_of(listener, phoc_layer_surface, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	struct phoc_layer_popup *popup = popup_create(wlr_popup);
	popup->parent_type = LAYER_PARENT_LAYER;
	popup->parent_layer = phoc_layer_surface;
	popup_unconstrain(popup);
}

static PhocLayerSurface *subsurface_get_root_layer(struct phoc_layer_subsurface *subsurface) {
	while (subsurface->parent_type == LAYER_PARENT_SUBSURFACE) {
		subsurface = subsurface->parent_subsurface;
	}
	if (subsurface->parent_type == LAYER_PARENT_POPUP) {
		return popup_get_root_layer(subsurface->parent_popup);
	}
	return subsurface->parent_layer;
}

static void subsurface_damage(struct phoc_layer_subsurface *subsurface, bool whole) {
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
	struct phoc_layer_subsurface *subsurface =
		wl_container_of(listener, subsurface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;

	struct phoc_layer_subsurface *new_subsurface = layer_subsurface_create(wlr_subsurface);
	new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
	new_subsurface->parent_subsurface = subsurface;
	wl_list_insert(&subsurface->subsurfaces, &new_subsurface->link);
}

static void subsurface_handle_map(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct phoc_layer_subsurface *subsurface = wl_container_of(listener, subsurface, map);

	struct wlr_subsurface *child;
	wl_list_for_each(child, &subsurface->wlr_subsurface->surface->current.subsurfaces_below, current.link) {
		struct phoc_layer_subsurface *new_subsurface = layer_subsurface_create(child);
		new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
		new_subsurface->parent_subsurface = subsurface;
		wl_list_insert(&subsurface->subsurfaces, &new_subsurface->link);
	}
	wl_list_for_each(child, &subsurface->wlr_subsurface->surface->current.subsurfaces_above, current.link) {
		struct phoc_layer_subsurface *new_subsurface = layer_subsurface_create(child);
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
	struct phoc_layer_subsurface *subsurface = wl_container_of(listener, subsurface, unmap);
	struct phoc_layer_subsurface *child, *tmp;
	wl_list_for_each_safe(child, tmp, &subsurface->subsurfaces, link) {
		subsurface_destroy(child);
	}
	wl_list_remove(&subsurface->new_subsurface.link);
	subsurface_damage(subsurface, true);
	phoc_input_update_cursor_focus(server->input);
}

static void subsurface_handle_commit(struct wl_listener *listener, void *data) {
	struct phoc_layer_subsurface *subsurface = wl_container_of(listener, subsurface, commit);
	subsurface_damage(subsurface, false);
}

static void subsurface_handle_destroy(struct wl_listener *listener, void *data) {
	struct phoc_layer_subsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);

	subsurface_destroy(subsurface);
}

static struct phoc_layer_subsurface *layer_subsurface_create(struct wlr_subsurface *wlr_subsurface) {
	struct phoc_layer_subsurface *subsurface =
		calloc(1, sizeof(struct phoc_layer_subsurface));
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
	PhocLayerSurface *phoc_layer_surface =
		wl_container_of(listener, phoc_layer_surface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;

	struct phoc_layer_subsurface *subsurface = layer_subsurface_create(wlr_subsurface);
	subsurface->parent_type = LAYER_PARENT_LAYER;
	subsurface->parent_layer = phoc_layer_surface;
	wl_list_insert(&phoc_layer_surface->subsurfaces, &subsurface->link);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface_v1 *wlr_layer_surface = data;
	PhocLayerSurface *layer_surface = PHOC_LAYER_SURFACE (wlr_layer_surface->data);
	PhocOutput *output = phoc_layer_surface_get_output (layer_surface);
	if (!output) {
		return;
	}

	layer_surface->mapped = true;

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &wlr_layer_surface->surface->current.subsurfaces_below, current.link) {
		struct phoc_layer_subsurface *phoc_subsurface = layer_subsurface_create(subsurface);
		phoc_subsurface->parent_type = LAYER_PARENT_LAYER;
		phoc_subsurface->parent_layer = layer_surface;
		wl_list_insert(&layer_surface->subsurfaces, &phoc_subsurface->link);
	}
	wl_list_for_each(subsurface, &wlr_layer_surface->surface->current.subsurfaces_above, current.link) {
		struct phoc_layer_subsurface *phoc_subsurface = layer_subsurface_create(subsurface);
		phoc_subsurface->parent_type = LAYER_PARENT_LAYER;
		phoc_subsurface->parent_layer = layer_surface;
		wl_list_insert(&layer_surface->subsurfaces, &phoc_subsurface->link);
	}

	layer_surface->new_subsurface.notify = handle_new_subsurface;
	wl_signal_add(&wlr_layer_surface->surface->events.new_subsurface, &layer_surface->new_subsurface);

	phoc_output_damage_whole_local_surface(output,
					       wlr_layer_surface->surface, layer_surface->geo.x,
					       layer_surface->geo.y);
	wlr_surface_send_enter(wlr_layer_surface->surface, output->wlr_output);

	phoc_layer_shell_arrange (output);
	phoc_layer_shell_update_focus ();
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocLayerSurface *layer_surface = wl_container_of(listener, layer_surface, unmap);
	PhocOutput *output = phoc_layer_surface_get_output (layer_surface);

	layer_surface->mapped = false;

	struct phoc_layer_subsurface *subsurface, *tmp;
	wl_list_for_each_safe(subsurface, tmp, &layer_surface->subsurfaces, link) {
		subsurface_destroy(subsurface);
	}
	wl_list_remove(&layer_surface->new_subsurface.link);

	phoc_layer_surface_unmap (layer_surface);
	phoc_input_update_cursor_focus(server->input);

	if (output)
		phoc_layer_shell_arrange (output);
	phoc_layer_shell_update_focus ();
}

void handle_layer_shell_surface(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct wlr_layer_surface_v1 *wlr_layer_surface = data;
	PhocDesktop *desktop = wl_container_of(listener, desktop, layer_shell_surface);

	g_debug ("new layer surface: namespace %s layer %d anchor %d "
			"size %dx%d margin %d,%d,%d,%d",
		wlr_layer_surface->namespace, wlr_layer_surface->pending.layer,
		wlr_layer_surface->pending.anchor,
		wlr_layer_surface->pending.desired_width,
		wlr_layer_surface->pending.desired_height,
		wlr_layer_surface->pending.margin.top,
		wlr_layer_surface->pending.margin.right,
		wlr_layer_surface->pending.margin.bottom,
		wlr_layer_surface->pending.margin.left);

	if (!wlr_layer_surface->output) {
		PhocInput *input = server->input;
		PhocSeat *seat = phoc_input_get_last_active_seat(input);
		g_assert (PHOC_IS_SEAT (seat)); // Technically speaking we should handle this case
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
			wlr_layer_surface->output = output;
		} else {
			wlr_layer_surface_v1_destroy(wlr_layer_surface);
			return;
		}
	}


	PhocLayerSurface *layer_surface = phoc_layer_surface_new (wlr_layer_surface);

	layer_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&wlr_layer_surface->surface->events.commit,
		&layer_surface->surface_commit);

	layer_surface->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_layer_surface->events.destroy, &layer_surface->destroy);
	layer_surface->map.notify = handle_map;
	wl_signal_add(&wlr_layer_surface->events.map, &layer_surface->map);
	layer_surface->unmap.notify = handle_unmap;
	wl_signal_add(&wlr_layer_surface->events.unmap, &layer_surface->unmap);
	layer_surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&wlr_layer_surface->events.new_popup, &layer_surface->new_popup);

	PhocOutput *output = wlr_layer_surface->output->data;
	wl_list_insert(&output->layer_surfaces, &layer_surface->link);

	// Temporarily set the layer's current state to pending
	// So that we can easily arrange it
	struct wlr_layer_surface_v1_state old_state = wlr_layer_surface->current;
	wlr_layer_surface->current = wlr_layer_surface->pending;

	phoc_layer_shell_arrange (output);
	phoc_layer_shell_update_focus ();

	wlr_layer_surface->current = old_state;
}
