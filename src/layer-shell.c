#define G_LOG_DOMAIN "phoc-layer-shell"

#include "phoc-config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/box.h>
#include "cursor.h"
#include "desktop.h"
#include "layer-shell.h"
#include "layer-shell-private.h"
#include "output.h"
#include "seat.h"
#include "server.h"
#include "utils.h"

#include <glib.h>

#define LAYER_SHELL_LAYER_COUNT 4

static void
apply_exclusive (struct wlr_box *usable_area,
                 uint32_t anchor, int32_t exclusive,
                 int32_t margin_top, int32_t margin_right,
                 int32_t margin_bottom, int32_t margin_left)
{
  if (exclusive <= 0)
    return;

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
  for (size_t i = 0; i < sizeof (edges) / sizeof (edges[0]); ++i) {
    if ((anchor & edges[i].anchors) == edges[i].anchors && exclusive + edges[i].margin > 0) {
      if (edges[i].positive_axis)
        *edges[i].positive_axis += exclusive + edges[i].margin;

      if (edges[i].negative_axis)
        *edges[i].negative_axis -= exclusive + edges[i].margin;
    }
  }
}

/**
 * phoc_layer_shell_update_cursors:
 * @layer_surface: The layer surface
 * @seats:(element-type PhocSeat): List of seats
 *
 * Updates the cursor position for the given layer surface
 */
void
phoc_layer_shell_update_cursors (PhocLayerSurface *layer_surface, GSList *seats)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = phoc_server_get_desktop (server);
  PhocInput *input = phoc_server_get_input (server);

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    PhocCursor *cursor = phoc_seat_get_cursor (seat);
    double sx, sy;

    struct wlr_surface *surface = phoc_desktop_wlr_surface_at (desktop,
                                                               cursor->cursor->x,
                                                               cursor->cursor->y,
                                                               &sx, &sy, NULL);
    if (surface == layer_surface->layer_surface->surface) {
      struct timespec time;
      if (clock_gettime (CLOCK_MONOTONIC, &time) == 0) {
        phoc_cursor_update_position (cursor,
                                     time.tv_sec * 1000 + time.tv_nsec / 1000000);
      } else {
        g_critical ("Failed to get time, not updating position. Errno: %s\n", strerror (errno));
      }
    }
  }
}


static void
arrange_layer (PhocOutput                     *output,
               GSList                         *seats, /* PhocSeat */
               enum zwlr_layer_shell_v1_layer  layer,
               struct wlr_box                 *usable_area,
               bool                            exclusive)
{
  PhocLayerSurface *layer_surface;
  struct wlr_box full_area = { 0 };

  g_assert (PHOC_IS_OUTPUT (output));
  wlr_output_effective_resolution (output->wlr_output, &full_area.width, &full_area.height);
  wl_list_for_each_reverse (layer_surface, &output->layer_surfaces, link) {
    struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->layer_surface;
    struct wlr_layer_surface_v1_state *state = &wlr_layer_surface->current;

    if (layer_surface->layer != layer)
      continue;

    if (exclusive != (state->exclusive_zone > 0))
      continue;

    struct wlr_box bounds;
    if (state->exclusive_zone == -1)
      bounds = full_area;
    else
      bounds = *usable_area;

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
      g_warning_once ("Layer surface '%s' has negative bounds %dx%d - ignoring",
                      layer_surface->layer_surface->namespace ?: "<unknown>",
                      box.width, box.height);
      /* The layer surface never gets configured, hence the client sees a protocol error */
      continue;
    }

    // Apply
    struct wlr_box old_geo = layer_surface->geo;
    layer_surface->geo = box;
    if (wlr_layer_surface->surface->mapped) {
      apply_exclusive (usable_area, state->anchor, state->exclusive_zone,
                       state->margin.top, state->margin.right,
                       state->margin.bottom, state->margin.left);
    }

    if (box.width != old_geo.width || box.height != old_geo.height)
      wlr_layer_surface_v1_configure (wlr_layer_surface, box.width, box.height);

    // Having a cursor newly end up over the moved layer will not
    // automatically send a motion event to the surface. The event needs to
    // be synthesized.
    // Only update layer surfaces which kept their size (and so buffers) the
    // same, because those with resized buffers will be handled separately.

    if (layer_surface->geo.x != old_geo.x || layer_surface->geo.y != old_geo.y)
      phoc_layer_shell_update_cursors (layer_surface, seats);
  }
}

/**
 * phoc_layer_shell_find_osk:
 * @output: An output
 *
 * Checks whether the given [type@Output] has the on screen keyboard and returns
 * the associated [type@LayerSurface] if found.
 *
 * Returns:(transfer none)(nullable): The OSKs layer surface or %NULL
 */
PhocLayerSurface *
phoc_layer_shell_find_osk (PhocOutput *output)
{
  PhocLayerSurface *layer_surface;

  wl_list_for_each (layer_surface, &output->layer_surfaces, link) {
    if (strcmp (layer_surface->layer_surface->namespace, "osk") == 0)
      return layer_surface;
  }

  return NULL;
}


void
phoc_layer_shell_arrange (PhocOutput *output)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = phoc_server_get_desktop (server);
  PhocInput *input = phoc_server_get_input (server);
  struct wlr_box usable_area = { 0 };
  GSList *seats = phoc_input_get_seats (input);
  enum zwlr_layer_shell_v1_layer layers[] = {
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND
  };

  /*
   * Whenever we rearrange layers we need to check for the OSK's layer as
   * the new surface might need it raised (or the new surface might be the OSK itself)
   */
  phoc_layer_shell_update_osk (output, FALSE);

  wlr_output_effective_resolution (output->wlr_output, &usable_area.width, &usable_area.height);
  // Arrange exclusive surfaces from top->bottom
  for (size_t i = 0; i < G_N_ELEMENTS (layers); ++i)
    arrange_layer (output, seats, layers[i], &usable_area, true);
  output->usable_area = usable_area;

  for (GList *l = phoc_desktop_get_views (desktop)->head; l; l = l->next) {
    PhocView *view = PHOC_VIEW (l->data);

    phoc_view_arrange (view, NULL, output->desktop->maximize);
  }

  // Arrange non-exlusive surfaces from top->bottom
  for (size_t i = 0; i < G_N_ELEMENTS (layers); ++i)
    arrange_layer (output, seats, layers[i], &usable_area, false);

  phoc_output_update_shell_reveal (output);

  if (G_UNLIKELY (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_LAYER_SHELL))) {
    PhocLayerSurface *layer_surface;
    g_message ("Dumping layers:");
    wl_list_for_each (layer_surface, &output->layer_surfaces, link) {
      g_message ("layer-surface: %-20s, l: %d, m: %d, cm: %4d,%4d,%4d,%4d, e: %4d",
                 layer_surface->layer_surface->namespace,
                 layer_surface->layer,
                 phoc_layer_surface_get_mapped (layer_surface),
                 layer_surface->layer_surface->current.margin.top,
                 layer_surface->layer_surface->current.margin.right,
                 layer_surface->layer_surface->current.margin.bottom,
                 layer_surface->layer_surface->current.margin.left,
                 layer_surface->layer_surface->current.exclusive_zone);
    }
  }
}


void
phoc_layer_shell_update_focus (void)
{
  PhocServer *server = phoc_server_get_default ();
  PhocInput *input = phoc_server_get_input (server);
  PhocDesktop *desktop = phoc_server_get_desktop (server);
  enum zwlr_layer_shell_v1_layer layers_above_shell[] = {
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP,
  };
  PhocLayerSurface *layer_surface, *topmost = NULL;
  // Find topmost keyboard interactive layer, if such a layer exists
  // TODO: Make layer surface focus per-output based on cursor position
  PhocOutput *output;
  wl_list_for_each (output, &desktop->outputs, link) {
    for (size_t i = 0; i < G_N_ELEMENTS (layers_above_shell); ++i) {
      wl_list_for_each_reverse (layer_surface, &output->layer_surfaces, link) {
        if (layer_surface->layer != layers_above_shell[i])
          continue;

        if (layer_surface->layer_surface->current.exclusive_zone <= 0)
          continue;

        if (layer_surface->layer_surface->current.keyboard_interactive &&
            layer_surface->layer_surface->surface->mapped) {
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
            layer_surface->layer_surface->surface->mapped) {
          topmost = layer_surface;
          break;
        }
      }
      if (topmost != NULL)
        break;
    }
  }

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    phoc_seat_set_focus_layer (seat, topmost ? topmost->layer_surface : NULL);
  }
}


void
phoc_layer_subsurface_destroy (PhocLayerSubsurface *subsurface)
{
  wl_list_remove (&subsurface->map.link);
  wl_list_remove (&subsurface->unmap.link);
  wl_list_remove (&subsurface->destroy.link);
  wl_list_remove (&subsurface->commit.link);
  wl_list_remove (&subsurface->link);
  free (subsurface);
}


static PhocLayerSurface *
popup_get_root_layer (PhocLayerPopup *popup)
{
  while (popup->parent_type == LAYER_PARENT_POPUP)
    popup = popup->parent_popup;

  return popup->parent_layer;
}


void
phoc_layer_popup_unconstrain (PhocLayerPopup *popup)
{
  PhocLayerSurface *layer = popup_get_root_layer (popup);
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

  wlr_xdg_popup_unconstrain_from_box (wlr_popup, &output_toplevel_sx_box);
}


static void
popup_damage (PhocLayerPopup *layer_popup, bool whole)
{
  struct wlr_xdg_popup *popup = layer_popup->wlr_popup;
  struct wlr_surface *surface = popup->base->surface;
  int popup_sx = popup->current.geometry.x - popup->base->current.geometry.x;
  int popup_sy = popup->current.geometry.y - popup->base->current.geometry.y;
  int ox = popup_sx, oy = popup_sy;
  PhocLayerSurface *layer;
  while (layer_popup->parent_type == LAYER_PARENT_POPUP) {
    layer_popup = layer_popup->parent_popup;
    ox += layer_popup->wlr_popup->current.geometry.x;
    oy += layer_popup->wlr_popup->current.geometry.y;
  }
  layer = layer_popup->parent_layer;
  ox += layer->geo.x;
  oy += layer->geo.y;

  PhocOutput *output = phoc_layer_surface_get_output (layer);
  if (!output)
    return;

  if (whole)
    phoc_output_damage_whole_surface (output, surface, ox, oy);
  else
    phoc_output_damage_from_surface (output, surface, ox, oy);
}


static void
popup_new_popup (struct wl_listener *listener, void *data)
{
  PhocLayerPopup *popup = wl_container_of (listener, popup, new_popup);
  struct wlr_xdg_popup *wlr_popup = data;
  PhocLayerPopup *new_popup = phoc_layer_popup_create (wlr_popup);

  new_popup->parent_type = LAYER_PARENT_POPUP;
  new_popup->parent_popup = popup;
  phoc_layer_popup_unconstrain (new_popup);
}


static void
popup_new_subsurface (struct wl_listener *listener, void *data)
{
  PhocLayerPopup *popup = wl_container_of (listener, popup, new_subsurface);
  struct wlr_subsurface *wlr_subsurface = data;
  PhocLayerSubsurface *subsurface = phoc_layer_subsurface_create (wlr_subsurface);

  subsurface->parent_type = LAYER_PARENT_POPUP;
  subsurface->parent_popup = popup;
  wl_list_insert (&popup->subsurfaces, &subsurface->link);
}


static void
popup_handle_map (struct wl_listener *listener, void *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocLayerPopup *popup = wl_container_of (listener, popup, map);
  PhocLayerSurface *layer = popup_get_root_layer (popup);
  struct wlr_output *wlr_output = layer->layer_surface->output;

  if (!wlr_output)
    return;

  struct wlr_subsurface *child;
  wl_list_for_each (child,
                    &popup->wlr_popup->base->surface->current.subsurfaces_below,
                    current.link) {
    PhocLayerSubsurface *new_subsurface = phoc_layer_subsurface_create (child);

    new_subsurface->parent_type = LAYER_PARENT_POPUP;
    new_subsurface->parent_popup = popup;
    wl_list_insert (&popup->subsurfaces, &new_subsurface->link);
  }
  wl_list_for_each (child,
                    &popup->wlr_popup->base->surface->current.subsurfaces_above,
                    current.link) {
    PhocLayerSubsurface *new_subsurface = phoc_layer_subsurface_create (child);

    new_subsurface->parent_type = LAYER_PARENT_POPUP;
    new_subsurface->parent_popup = popup;
    wl_list_insert (&popup->subsurfaces, &new_subsurface->link);
  }
  popup->new_subsurface.notify = popup_new_subsurface;
  wl_signal_add (&popup->wlr_popup->base->surface->events.new_subsurface, &popup->new_subsurface);

  phoc_utils_wlr_surface_enter_output (popup->wlr_popup->base->surface, wlr_output);
  popup_damage (popup, true);
  phoc_input_update_cursor_focus (input);
}

static void
popup_handle_unmap (struct wl_listener *listener, void *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocLayerPopup *popup = wl_container_of (listener, popup, unmap);
  PhocLayerSubsurface *child, *tmp;

  wl_list_for_each_safe (child, tmp, &popup->subsurfaces, link)
    phoc_layer_subsurface_destroy (child);

  wl_list_remove (&popup->new_subsurface.link);
  popup_damage (popup, true);
  phoc_input_update_cursor_focus (input);
}

static void
popup_handle_commit (struct wl_listener *listener, void *data)
{
  PhocLayerPopup *popup = wl_container_of (listener, popup, commit);

  popup_damage (popup, false);
}

static void
popup_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocLayerPopup *popup = wl_container_of (listener, popup, destroy);

  wl_list_remove (&popup->map.link);
  wl_list_remove (&popup->unmap.link);
  wl_list_remove (&popup->destroy.link);
  wl_list_remove (&popup->commit.link);
  wl_list_remove (&popup->new_popup.link);
  free (popup);
}

PhocLayerPopup *
phoc_layer_popup_create (struct wlr_xdg_popup *wlr_popup)
{
  PhocLayerPopup *popup = calloc (1, sizeof (PhocLayerPopup));

  if (popup == NULL)
    return NULL;

  popup->wlr_popup = wlr_popup;
  popup->map.notify = popup_handle_map;
  wl_signal_add (&wlr_popup->base->surface->events.map, &popup->map);
  popup->unmap.notify = popup_handle_unmap;
  wl_signal_add (&wlr_popup->base->surface->events.unmap, &popup->unmap);
  popup->destroy.notify = popup_handle_destroy;
  wl_signal_add (&wlr_popup->base->events.destroy, &popup->destroy);
  popup->commit.notify = popup_handle_commit;
  wl_signal_add (&wlr_popup->base->surface->events.commit, &popup->commit);
  popup->new_popup.notify = popup_new_popup;
  wl_signal_add (&wlr_popup->base->events.new_popup, &popup->new_popup);

  wl_list_init (&popup->subsurfaces);

  return popup;
}


static PhocLayerSurface *
subsurface_get_root_layer (PhocLayerSubsurface *subsurface)
{
  while (subsurface->parent_type == LAYER_PARENT_SUBSURFACE)
    subsurface = subsurface->parent_subsurface;

  if (subsurface->parent_type == LAYER_PARENT_POPUP)
    return popup_get_root_layer (subsurface->parent_popup);

  return subsurface->parent_layer;
}

static void
subsurface_damage (PhocLayerSubsurface *subsurface, bool whole)
{
  PhocLayerSurface *layer = subsurface_get_root_layer (subsurface);
  PhocOutput *output = phoc_layer_surface_get_output (layer);
  if (!output)
    return;

  int ox = subsurface->wlr_subsurface->current.x + layer->geo.x;
  int oy = subsurface->wlr_subsurface->current.y + layer->geo.y;
  if (whole) {
    phoc_output_damage_whole_surface (output,
                                      subsurface->wlr_subsurface->surface,
                                      ox, oy);
  } else {
    phoc_output_damage_from_surface (output,
                                     subsurface->wlr_subsurface->surface,
                                     ox, oy);
  }
}

static void
subsurface_new_subsurface (struct wl_listener *listener, void *data)
{
  PhocLayerSubsurface *subsurface = wl_container_of (listener, subsurface, new_subsurface);
  struct wlr_subsurface *wlr_subsurface = data;
  PhocLayerSubsurface *new_subsurface = phoc_layer_subsurface_create (wlr_subsurface);

  new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
  new_subsurface->parent_subsurface = subsurface;
  wl_list_insert (&subsurface->subsurfaces, &new_subsurface->link);
}

static void
subsurface_handle_map (struct wl_listener *listener, void *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocLayerSubsurface *subsurface = wl_container_of (listener, subsurface, map);

  struct wlr_subsurface *child;
  wl_list_for_each (child,
                    &subsurface->wlr_subsurface->surface->current.subsurfaces_below,
                    current.link) {
    PhocLayerSubsurface *new_subsurface = phoc_layer_subsurface_create (child);

    new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
    new_subsurface->parent_subsurface = subsurface;
    wl_list_insert (&subsurface->subsurfaces, &new_subsurface->link);
  }
  wl_list_for_each (child,
                    &subsurface->wlr_subsurface->surface->current.subsurfaces_above,
                    current.link) {
    PhocLayerSubsurface *new_subsurface = phoc_layer_subsurface_create (child);

    new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
    new_subsurface->parent_subsurface = subsurface;
    wl_list_insert (&subsurface->subsurfaces, &new_subsurface->link);
  }
  subsurface->new_subsurface.notify = subsurface_new_subsurface;
  wl_signal_add (&subsurface->wlr_subsurface->surface->events.new_subsurface,
                 &subsurface->new_subsurface);

  PhocLayerSurface *layer_surface = subsurface_get_root_layer (subsurface);
  phoc_utils_wlr_surface_enter_output (subsurface->wlr_subsurface->surface,
                                       layer_surface->layer_surface->output);
  subsurface_damage (subsurface, true);
  phoc_input_update_cursor_focus (input);
}

static void
subsurface_handle_unmap (struct wl_listener *listener, void *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocLayerSubsurface *subsurface = wl_container_of (listener, subsurface, unmap);
  PhocLayerSubsurface *child, *tmp;

  wl_list_for_each_safe (child, tmp, &subsurface->subsurfaces, link)
    phoc_layer_subsurface_destroy (child);

  wl_list_remove (&subsurface->new_subsurface.link);
  subsurface_damage (subsurface, true);
  phoc_input_update_cursor_focus (input);
}

static void
subsurface_handle_commit (struct wl_listener *listener, void *data)
{
  PhocLayerSubsurface *subsurface = wl_container_of (listener, subsurface, commit);
  subsurface_damage (subsurface, false);
}

static void
subsurface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocLayerSubsurface *subsurface = wl_container_of (listener, subsurface, destroy);

  phoc_layer_subsurface_destroy (subsurface);
}

PhocLayerSubsurface *
phoc_layer_subsurface_create (struct wlr_subsurface *wlr_subsurface)
{
  PhocLayerSubsurface *subsurface = calloc (1, sizeof (PhocLayerSubsurface));

  if (subsurface == NULL)
    return NULL;

  subsurface->wlr_subsurface = wlr_subsurface;

  subsurface->map.notify = subsurface_handle_map;
  wl_signal_add (&wlr_subsurface->surface->events.map, &subsurface->map);
  subsurface->unmap.notify = subsurface_handle_unmap;
  wl_signal_add (&wlr_subsurface->surface->events.unmap, &subsurface->unmap);
  subsurface->destroy.notify = subsurface_handle_destroy;
  wl_signal_add (&wlr_subsurface->events.destroy, &subsurface->destroy);
  subsurface->commit.notify = subsurface_handle_commit;
  wl_signal_add (&wlr_subsurface->surface->events.commit, &subsurface->commit);

  wl_list_init (&subsurface->subsurfaces);
  wl_list_init (&subsurface->link);

  return subsurface;
}


void
phoc_handle_layer_shell_surface (struct wl_listener *listener, void *data)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface = data;
  PhocDesktop *desktop = wl_container_of (listener, desktop, layer_shell_surface);

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
    PhocSeat *seat = phoc_server_get_last_active_seat (phoc_server_get_default ());
    g_assert (PHOC_IS_SEAT (seat)); // Technically speaking we should handle this case
    PhocCursor *cursor = phoc_seat_get_cursor (seat);
    struct wlr_output *output = wlr_output_layout_output_at (desktop->layout,
                                                             cursor->cursor->x,
                                                             cursor->cursor->y);
    if (!output) {
      g_critical ("Couldn't find output at (%.0f,%.0f)", cursor->cursor->x, cursor->cursor->y);
      output = wlr_output_layout_get_center_output (desktop->layout);
    }
    if (output) {
      wlr_layer_surface->output = output;
    } else {
      wlr_layer_surface_v1_destroy (wlr_layer_surface);
      return;
    }
  }

  phoc_layer_surface_new (wlr_layer_surface);
  PhocOutput *output = PHOC_OUTPUT (wlr_layer_surface->output->data);

  // Temporarily set the layer's current state to pending
  // So that we can easily arrange it
  struct wlr_layer_surface_v1_state old_state = wlr_layer_surface->current;
  wlr_layer_surface->current = wlr_layer_surface->pending;

  phoc_layer_shell_arrange (output);
  phoc_layer_shell_update_focus ();

  wlr_layer_surface->current = old_state;
}

/**
 * phoc_layer_shell_update_osk:
 * @output: The output to act on
 * @arrange: Whether to arrange other layers too
 *
 * When a layer surface gets focus and there's an OSK we need to make
 * sure the OSK is above that layer as otherwise keyboard input isn't possible.
 * This can be used to adjust the OSKs layer accordingly.
 *
 * When `arrange` is `TRUE` the layers will also be rearranged to reflect that change
 * immediately.
 */
void
phoc_layer_shell_update_osk (PhocOutput *output, gboolean arrange)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocLayerSurface *osk;
  GSList *seats = phoc_input_get_seats (input);
  gboolean force_overlay = FALSE;
  enum zwlr_layer_shell_v1_layer old_layer;

  g_assert (PHOC_IS_OUTPUT (output));

  osk = phoc_layer_shell_find_osk (output);
  if (osk == NULL)
    return;

  for (GSList *elem = seats; elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    if (!seat->focused_layer)
      continue;

    g_assert (PHOC_IS_SEAT (seat));
    if (seat->focused_layer->pending.layer >= osk->layer_surface->pending.layer &&
        phoc_input_method_relay_is_enabled (&seat->im_relay, seat->focused_layer->surface)) {
      force_overlay = TRUE;
      break;
    }
  }

  old_layer = osk->layer;
  if (force_overlay && osk->layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
    osk->layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;

  if (!force_overlay && osk->layer != osk->layer_surface->pending.layer)
    osk->layer = osk->layer_surface->pending.layer;

  if (old_layer != osk->layer) {
    phoc_output_set_layer_dirty (output, old_layer);
    phoc_output_set_layer_dirty (output, osk->layer);
  }

  if (force_overlay && arrange)
    phoc_layer_shell_arrange (output);
}
