#define G_LOG_DOMAIN "phoc-view"

#include "phoc-config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output_layout.h>
#include "cursor.h"
#include "desktop.h"
#include "input.h"
#include "seat.h"
#include "server.h"
#include "utils.h"
#include "view-private.h"


enum {
  PROP_0,
  PROP_SCALE_TO_FIT,
  PROP_ACTIVATION_TOKEN,
  PROP_IS_MAPPED,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  SURFACE_DESTROY,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

typedef struct _PhocViewPrivate {
  char          *title;
  char          *app_id;
  GSettings     *settings;

  float          alpha;
  float          scale;
  gboolean       decorated;
  int            titlebar_height;
  int            border_width;
  PhocViewState  state;

  PhocOutput    *fullscreen_output;

  PhocViewTileDirection tile_direction;

  gulong         notify_scale_to_fit_id;
  gboolean       scale_to_fit;
  char          *activation_token;

  /* wlr-toplevel-management handling */
  struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
  struct wl_listener toplevel_handle_request_maximize;
  struct wl_listener toplevel_handle_request_activate;
  struct wl_listener toplevel_handle_request_fullscreen;
  struct wl_listener toplevel_handle_request_close;

  /* Subsurface and popups */
  struct wl_listener surface_new_subsurface;
  struct wl_list child_surfaces; // PhocViewChild::link
} PhocViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhocView, phoc_view, G_TYPE_OBJECT)

#define PHOC_VIEW_SELF(p) PHOC_PRIV_CONTAINER(PHOC_VIEW, PhocView, (p))

typedef struct _PhocSubsurface {
  PhocViewChild child;
  struct wlr_subsurface *wlr_subsurface;

  struct wl_listener destroy;
  struct wl_listener map;
  struct wl_listener unmap;
} PhocSubsurface;


static struct wlr_foreign_toplevel_handle_v1 *
phoc_view_get_toplevel_handle (PhocView *self)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (self);

  return priv->toplevel_handle;
}


gboolean
view_is_floating (PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  return priv->state == PHOC_VIEW_STATE_FLOATING && !view_is_fullscreen (view);
}

gboolean
view_is_maximized (PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  return priv->state == PHOC_VIEW_STATE_MAXIMIZED && !view_is_fullscreen (view);
}

gboolean
view_is_tiled (PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  return priv->state == PHOC_VIEW_STATE_TILED && !view_is_fullscreen (view);
}

gboolean
view_is_fullscreen (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->fullscreen_output != NULL;
}

/**
 * phoc_view_get_fullscreen_output:
 * @self: The view
 *
 * Gets the output a view is fullscreen on. Returns %NULL if
 * the view isn't currently fullscreen.
 *
 * Returns:(nullable): The fullscreen output
 */
PhocOutput *
phoc_view_get_fullscreen_output (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->fullscreen_output;
}

void view_get_box(PhocView *view, struct wlr_box *box) {
        PhocViewPrivate *priv;

        g_assert (PHOC_IS_VIEW (view));
        priv = phoc_view_get_instance_private (view);

	box->x = view->box.x;
	box->y = view->box.y;
	box->width = view->box.width * priv->scale;
	box->height = view->box.height * priv->scale;
}


void view_get_deco_box(PhocView *view, struct wlr_box *box) {
        PhocViewPrivate *priv;

        g_assert (PHOC_IS_VIEW (view));
        priv = phoc_view_get_instance_private (view);

	view_get_box(view, box);
	if (!priv->decorated) {
		return;
	}

	box->x -= priv->border_width;
	box->y -= (priv->border_width + priv->titlebar_height);
	box->width += priv->border_width * 2;
	box->height += (priv->border_width * 2 + priv->titlebar_height);
}

PhocViewDecoPart view_get_deco_part(PhocView *view, double sx, double sy) {
       PhocViewPrivate *priv;

        g_assert (PHOC_IS_VIEW (view));
        priv = phoc_view_get_instance_private (view);

	if (!priv->decorated) {
		return PHOC_VIEW_DECO_PART_NONE;
	}

	int sw = view->wlr_surface->current.width;
	int sh = view->wlr_surface->current.height;
	int bw = priv->border_width;
	int titlebar_h = priv->titlebar_height;

	if (sx > 0 && sx < sw && sy < 0 && sy > -priv->titlebar_height) {
		return PHOC_VIEW_DECO_PART_TITLEBAR;
	}

	PhocViewDecoPart parts = 0;
	if (sy >= -(titlebar_h + bw) &&
			sy <= sh + bw) {
		if (sx < 0 && sx > -bw) {
			parts |= PHOC_VIEW_DECO_PART_LEFT_BORDER;
		} else if (sx > sw && sx < sw + bw) {
			parts |= PHOC_VIEW_DECO_PART_RIGHT_BORDER;
		}
	}

	if (sx >= -bw && sx <= sw + bw) {
		if (sy > sh && sy <= sh + bw) {
			parts |= PHOC_VIEW_DECO_PART_BOTTOM_BORDER;
		} else if (sy >= -(titlebar_h + bw) && sy < 0) {
			parts |= PHOC_VIEW_DECO_PART_TOP_BORDER;
		}
	}

	// TODO corners

	return parts;
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_leave(surface, wlr_output);
}

static void
view_update_output (PhocView *view, const struct wlr_box *before)
{
  PhocDesktop *desktop = view->desktop;
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  if (!phoc_view_is_mapped (view))
    return;

  struct wlr_box box;
  view_get_box(view, &box);

  PhocOutput *output;
  wl_list_for_each (output, &desktop->outputs, link) {
    bool intersected = before != NULL && wlr_output_layout_intersects(
      desktop->layout, output->wlr_output, before);
    bool intersects = wlr_output_layout_intersects (desktop->layout, output->wlr_output, &box);

    if (intersected && !intersects) {
      phoc_view_for_each_surface (view, surface_send_leave_iterator, output->wlr_output);
      if (priv->toplevel_handle) {
        wlr_foreign_toplevel_handle_v1_output_leave (
          priv->toplevel_handle, output->wlr_output);
      }
    }

    if (!intersected && intersects) {
      phoc_view_for_each_surface (view, surface_send_enter_iterator, output->wlr_output);

      if (priv->toplevel_handle) {
        wlr_foreign_toplevel_handle_v1_output_enter (
          priv->toplevel_handle, output->wlr_output);
      }
    }
  }
}

static void
view_save(PhocView *view)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  if (!view_is_floating (view))
    return;

  /* backup window state */
  struct wlr_box geom;
  phoc_view_get_geometry (view, &geom);
  view->saved.x = view->box.x + geom.x * priv->scale;
  view->saved.y = view->box.y + geom.y * priv->scale;
  view->saved.width = view->box.width;
  view->saved.height = view->box.height;
}


static void
phoc_view_move_default (PhocView *view, double x, double y)
{
  view_update_position (view, x, y);
}

void
phoc_view_appear_activated (PhocView *view, bool activated)
{
  g_assert (PHOC_IS_VIEW (view));

  PHOC_VIEW_GET_CLASS (view)->set_active (view, activated);
}


void
phoc_view_activate (PhocView *self, bool activate)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  /* UGLY: waydroid windows always need the activate signal */
  if ((priv->app_id && g_str_has_prefix (priv->app_id, "waydroid.")) || !self->desktop->maximize) {
    phoc_view_appear_activated (self, activate);
  }

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_activated (priv->toplevel_handle, activate);

  if (activate && view_is_fullscreen (self)) {
    phoc_output_force_shell_reveal (priv->fullscreen_output, false);
  }

  if (priv->activation_token) {
    phoc_phosh_private_notify_startup_id (phoc_server_get_default()->desktop->phosh,
                                          priv->activation_token,
                                          PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_XDG_ACTIVATION);
    phoc_view_set_activation_token (self, NULL);
  }
}

static void
phoc_view_resize (PhocView *self, uint32_t width, uint32_t height)
{
  g_assert (PHOC_IS_VIEW (self));

  PHOC_VIEW_GET_CLASS (self)->resize(self, width, height);
}

void
phoc_view_move_resize (PhocView *view, double x, double y, uint32_t width, uint32_t height)
{
  bool update_x = x != view->box.x;
  bool update_y = y != view->box.y;
  bool update_width = width != view->box.width;
  bool update_height = height != view->box.height;

  view->pending_move_resize.update_x = false;
  view->pending_move_resize.update_y = false;

  if (!update_x && !update_y) {
    phoc_view_resize (view, width, height);
    return;
  }

  if (!update_width && !update_height) {
    phoc_view_move (view, x, y);
    return;
  }

  PHOC_VIEW_GET_CLASS (view)->move_resize(view, x, y, width, height);
}

static struct wlr_output *view_get_output(PhocView *view) {
	struct wlr_box view_box;
	view_get_box(view, &view_box);

	double output_x, output_y;
	wlr_output_layout_closest_point(view->desktop->layout, NULL,
		view->box.x + (double)view_box.width/2,
		view->box.y + (double)view_box.height/2,
		&output_x, &output_y);
	return wlr_output_layout_output_at(view->desktop->layout, output_x,
		output_y);
}

void view_arrange_maximized(PhocView *view, struct wlr_output *output) {
        PhocViewPrivate *priv;

        g_assert (PHOC_IS_VIEW (view));
        priv = phoc_view_get_instance_private (view);

	if (view_is_fullscreen (view))
		return;

	if (!output)
		output = view_get_output(view);

	if (!output)
		return;

	PhocOutput *phoc_output = output->data;
	struct wlr_box *output_box =
		wlr_output_layout_get_box(view->desktop->layout, output);
	struct wlr_box usable_area = phoc_output->usable_area;
	usable_area.x += output_box->x;
	usable_area.y += output_box->y;

	struct wlr_box geom;
	phoc_view_get_geometry (view, &geom);
	phoc_view_move_resize (view,
			       (usable_area.x - geom.x) / priv->scale,
			       (usable_area.y - geom.y) / priv->scale,
			       usable_area.width / priv->scale,
			       usable_area.height / priv->scale);
}

void
view_arrange_tiled (PhocView *view, struct wlr_output *output)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  if (view_is_fullscreen (view))
    return;

  if (!output)
    output = view_get_output(view);

  if (!output)
    return;

  PhocOutput *phoc_output = output->data;
  struct wlr_box *output_box = wlr_output_layout_get_box (view->desktop->layout, output);
  struct wlr_box usable_area = phoc_output->usable_area;
  int x;

  usable_area.x += output_box->x;
  usable_area.y += output_box->y;

  switch (priv->tile_direction) {
  case PHOC_VIEW_TILE_LEFT:
    x = usable_area.x;
    break;
  case PHOC_VIEW_TILE_RIGHT:
    x = usable_area.x + (0.5 * usable_area.width);
    break;
  default:
    g_error ("Invalid tiling direction %d", priv->tile_direction);
  }

  struct wlr_box geom;
  phoc_view_get_geometry (view, &geom);
  phoc_view_move_resize (view,
                         (x - geom.x) / priv->scale,
                         (usable_area.y - geom.y) / priv->scale,
                         usable_area.width / 2 / priv->scale,
                         usable_area.height / priv->scale);
}

/*
 * Check if a view needs to be maximized
 */
static bool
want_auto_maximize(PhocView *view) {
  if (!view->desktop->maximize)
    return false;

  return PHOC_VIEW_GET_CLASS (view)->want_auto_maximize(view);
}

void view_maximize(PhocView *view, struct wlr_output *output) {
        PhocViewPrivate *priv;

        g_assert (PHOC_IS_VIEW (view));
        priv = phoc_view_get_instance_private (view);

	if (view_is_maximized (view) && view_get_output(view) == output) {
		return;
	}

	if (view_is_fullscreen (view)) {
		return;
	}

	PHOC_VIEW_GET_CLASS (view)->set_tiled (view, false);
	PHOC_VIEW_GET_CLASS (view)->set_maximized (view, true);

	if (priv->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_maximized(priv->toplevel_handle, true);
	}

	view_save (view);

	priv->state = PHOC_VIEW_STATE_MAXIMIZED;
	view_arrange_maximized(view, output);
}

/*
 * Maximize view if in auto-maximize mode otherwise do nothing.
 */
void
view_auto_maximize(PhocView *view)
{
  if (want_auto_maximize (view))
    view_maximize (view, NULL);
}

void
view_restore(PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  if (!view_is_maximized (view) && !view_is_tiled (view))
    return;

  if (want_auto_maximize (view))
    return;

  struct wlr_box geom;
  phoc_view_get_geometry (view, &geom);

  priv->state = PHOC_VIEW_STATE_FLOATING;
  if (!wlr_box_empty(&view->saved)) {
    phoc_view_move_resize (view, view->saved.x - geom.x * priv->scale,
                           view->saved.y - geom.y * priv->scale,
                           view->saved.width, view->saved.height);
  } else {
    phoc_view_resize (view, 0, 0);
    view->pending_centering = true;
  }

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_maximized (priv->toplevel_handle, false);

  PHOC_VIEW_GET_CLASS (view)->set_maximized (view, false);
  PHOC_VIEW_GET_CLASS (view)->set_tiled (view, false);
}

/**
 * phoc_view_set_fullscreen:
 * @view: The view
 * @fullscreen: Whether to fullscreen or unfulscreen
 * @output: The output to fullscreen the view on.
 *
 * If @fullscreen is `true`. fullscreens a view on the given output or
 * (if @output is %NULL) on the view's current output. Unfullscreens
 * the @view if @fullscreens is `false`.
 */
void phoc_view_set_fullscreen(PhocView *view, bool fullscreen, struct wlr_output *output) {
        PhocViewPrivate *priv;

        g_assert (PHOC_IS_VIEW (view));
        priv = phoc_view_get_instance_private (view);

	bool was_fullscreen = view_is_fullscreen (view);

	if (was_fullscreen != fullscreen) {
		/* don't allow unfocused surfaces to make themselves fullscreen */
		if (fullscreen && phoc_view_is_mapped (view))
			g_return_if_fail (phoc_input_view_has_focus (phoc_server_get_default()->input, view));

		PHOC_VIEW_GET_CLASS (view)->set_fullscreen (view, fullscreen);

		if (priv->toplevel_handle) {
			wlr_foreign_toplevel_handle_v1_set_fullscreen(priv->toplevel_handle,
					fullscreen);
		}
	}

	struct wlr_box view_geom;
	phoc_view_get_geometry (view, &view_geom);

	if (fullscreen) {
		if (output == NULL) {
			output = view_get_output(view);
		}
		PhocOutput *phoc_output = output->data;
		if (phoc_output == NULL) {
			return;
		}

		if (was_fullscreen) {
			priv->fullscreen_output->fullscreen_view = NULL;
		}

		struct wlr_box view_box;
		view_get_box(view, &view_box);

		view_save (view);

		struct wlr_box *output_box =
			wlr_output_layout_get_box(view->desktop->layout, output);
		phoc_view_move_resize (view, output_box->x,
				       output_box->y,
				       output_box->width,
				       output_box->height);

		phoc_output->fullscreen_view = view;
		phoc_output_force_shell_reveal (phoc_output, false);
		priv->fullscreen_output = phoc_output;
		phoc_output_damage_whole(phoc_output);
	}

	if (was_fullscreen && !fullscreen) {
		PhocOutput *phoc_output = priv->fullscreen_output;
		priv->fullscreen_output->fullscreen_view = NULL;
		priv->fullscreen_output = NULL;

		phoc_output_damage_whole(phoc_output);

		if (priv->state == PHOC_VIEW_STATE_MAXIMIZED) {
			view_arrange_maximized (view, phoc_output->wlr_output);
		} else if (priv->state == PHOC_VIEW_STATE_TILED) {
			view_arrange_tiled (view, phoc_output->wlr_output);
		} else if (!wlr_box_empty(&view->saved)) {
			phoc_view_move_resize (view,
					       view->saved.x - view_geom.x * priv->scale,
					       view->saved.y - view_geom.y * priv->scale,
					       view->saved.width,
					       view->saved.height);
		} else {
			phoc_view_resize (view, 0, 0);
			view->pending_centering = true;
		}

		view_auto_maximize(view);
	}
}


bool
view_move_to_next_output (PhocView *view, enum wlr_direction direction)
{
  PhocDesktop *desktop = view->desktop;
  struct wlr_output_layout *layout = view->desktop->layout;
  const struct wlr_output_layout_output *l_output;
  PhocOutput *phoc_output;
  struct wlr_output *output, *new_output;
  struct wlr_box usable_area;
  double x, y;

  output = view_get_output(view);
  if (!output)
    return false;

  /* use current view's x,y as ref_lx, ref_ly */
  new_output = wlr_output_layout_adjacent_output (layout, direction, output,
						  view->box.x, view->box.y);
  if (!new_output)
    return false;

  phoc_output = new_output->data;
  usable_area = phoc_output->usable_area;
  l_output = wlr_output_layout_get(desktop->layout, new_output);

  /* update saved position to the new output */
  x = usable_area.x + l_output->x + usable_area.width / 2 - view->saved.width / 2;
  y = usable_area.y + l_output->y + usable_area.height / 2 - view->saved.height / 2;
  g_debug("moving view's saved position to %f %f", x, y);
  view->saved.x = x;
  view->saved.y = y;

  if (view_is_fullscreen (view)) {
    phoc_view_set_fullscreen (view, true, new_output);
    return true;
  }

  if (view_is_maximized (view)) {
    view_arrange_maximized (view, new_output);
  } else if (view_is_tiled (view)) {
    view_arrange_tiled (view, new_output);
  } else {
    view_center (view, new_output);
  }

  return true;
}

void
view_tile(PhocView *view, PhocViewTileDirection direction, struct wlr_output *output)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  if (view_is_fullscreen (view))
    return;

  view_save (view);

  priv->state = PHOC_VIEW_STATE_TILED;
  priv->tile_direction = direction;

  PHOC_VIEW_GET_CLASS (view)->set_maximized (view, false);
  PHOC_VIEW_GET_CLASS (view)->set_tiled (view, true);

  view_arrange_tiled (view, output);
}

bool view_center(PhocView *view, struct wlr_output *wlr_output) {
	PhocServer *server = phoc_server_get_default ();
	struct wlr_box box, geom;
	PhocViewPrivate *priv;

	g_assert (PHOC_IS_VIEW (view));
	priv = phoc_view_get_instance_private (view);
	view_get_box(view, &box);
	phoc_view_get_geometry (view, &geom);

	if (!view_is_floating (view))
		return false;

	PhocDesktop *desktop = view->desktop;
	PhocInput *input = server->input;
	PhocSeat *seat = phoc_input_get_last_active_seat(input);
	PhocCursor *cursor;

	if (!seat) {
		return false;
	}
	cursor = phoc_seat_get_cursor (seat);

	struct wlr_output *output = wlr_output ?: wlr_output_layout_output_at(desktop->layout,
		cursor->cursor->x, cursor->cursor->y);
	if (!output) {
		// empty layout
		return false;
	}

	const struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(desktop->layout, output);

	PhocOutput *phoc_output = PHOC_OUTPUT (output->data);
	g_assert (PHOC_IS_OUTPUT (phoc_output));

	struct wlr_box usable_area = phoc_output->usable_area;

	double view_x = (double)(usable_area.width - box.width) / 2 +
	  usable_area.x + l_output->x - geom.x * priv->scale;
	double view_y = (double)(usable_area.height - box.height) / 2 +
	  usable_area.y + l_output->y - geom.y * priv->scale;

	g_debug ("moving view to %f %f", view_x, view_y);
	phoc_view_move (view, view_x / priv->scale, view_y / priv->scale);

	if (!desktop->maximize) {
		// TODO: fitting floating oversized windows requires more work (!228)
		return true;
	}

	if (view->box.width > phoc_output->usable_area.width || view->box.height > phoc_output->usable_area.height) {
		phoc_view_resize (view,
		             (view->box.width > phoc_output->usable_area.width) ? phoc_output->usable_area.width : view->box.width,
		             (view->box.height > phoc_output->usable_area.height) ? phoc_output->usable_area.height : view->box.height);
	}

	return true;
}

static bool
phoc_view_child_is_mapped (PhocViewChild *child)
{
  while (child) {
    if (!child->mapped) {
      return false;
    }
    child = child->parent;
  }
  return true;
}

static void
phoc_view_child_handle_commit (struct wl_listener *listener, void *data)
{
  PhocViewChild *child = wl_container_of(listener, child, commit);

  phoc_view_child_apply_damage (child);
}

static void phoc_view_subsurface_create (PhocView *view, struct wlr_subsurface *wlr_subsurface);
static void phoc_view_child_subsurface_create (PhocViewChild *child, struct wlr_subsurface *wlr_subsurface);

static void
phoc_view_child_handle_new_subsurface (struct wl_listener *listener, void *data)
{
  PhocViewChild *child = wl_container_of(listener, child, new_subsurface);
  struct wlr_subsurface *wlr_subsurface = data;

  phoc_view_child_subsurface_create (child, wlr_subsurface);
}

static void
phoc_view_init_subsurfaces (PhocView *view, struct wlr_surface *surface)
{
  struct wlr_subsurface *subsurface;

  wl_list_for_each(subsurface, &surface->current.subsurfaces_below, current.link)
    phoc_view_subsurface_create (view, subsurface);

  wl_list_for_each(subsurface, &surface->current.subsurfaces_above, current.link)
    phoc_view_subsurface_create (view, subsurface);
}

static void
phoc_view_child_init_subsurfaces (PhocViewChild *child, struct wlr_surface *surface)
{
  struct wlr_subsurface *subsurface;

  wl_list_for_each (subsurface, &surface->current.subsurfaces_below, current.link)
    phoc_view_child_subsurface_create (child, subsurface);

  wl_list_for_each (subsurface, &surface->current.subsurfaces_above, current.link)
    phoc_view_child_subsurface_create (child, subsurface);
}

void
phoc_view_child_init (PhocViewChild *child,
                      const struct phoc_view_child_interface *impl,
                      PhocView *view,
                      struct wlr_surface *wlr_surface)
{
  PhocViewPrivate *priv;

  g_assert (impl->destroy);
  child->impl = impl;
  child->view = view;
  child->wlr_surface = wlr_surface;

  child->commit.notify = phoc_view_child_handle_commit;
  wl_signal_add(&wlr_surface->events.commit, &child->commit);

  child->new_subsurface.notify = phoc_view_child_handle_new_subsurface;
  wl_signal_add(&wlr_surface->events.new_subsurface, &child->new_subsurface);

  priv = phoc_view_get_instance_private (view);
  wl_list_insert (&priv->child_surfaces, &child->link);

  phoc_view_child_init_subsurfaces (child, wlr_surface);
}

static const struct phoc_view_child_interface subsurface_impl;

static void subsurface_destroy(PhocViewChild *child) {
	g_assert (child->impl == &subsurface_impl);
	PhocSubsurface *subsurface = (PhocSubsurface *)child;
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	g_free (subsurface);
}

static const struct phoc_view_child_interface subsurface_impl = {
	.destroy = subsurface_destroy,
};

static void subsurface_handle_destroy(struct wl_listener *listener,
		void *data) {
	PhocSubsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);
	phoc_view_child_destroy(&subsurface->child);
}

static void subsurface_handle_map(struct wl_listener *listener,
		void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocSubsurface *subsurface = wl_container_of(listener, subsurface, map);
	PhocView *view = subsurface->child.view;
	subsurface->child.mapped = true;
	phoc_view_child_damage_whole (&subsurface->child);
	phoc_input_update_cursor_focus(server->input);

	struct wlr_box box;
	view_get_box(view, &box);
	PhocOutput *output;
	wl_list_for_each(output, &view->desktop->outputs, link) {
		bool intersects = wlr_output_layout_intersects(view->desktop->layout,
			output->wlr_output, &box);
		if (intersects) {
			wlr_surface_send_enter (subsurface->wlr_subsurface->surface, output->wlr_output);
		}
	}
}

static void subsurface_handle_unmap(struct wl_listener *listener,
		void *data) {
	PhocServer *server = phoc_server_get_default ();
	PhocSubsurface *subsurface = wl_container_of(listener, subsurface, unmap);

	phoc_view_child_damage_whole (&subsurface->child);
	phoc_input_update_cursor_focus(server->input);

	subsurface->child.mapped = false;
}

static void
phoc_view_subsurface_create (PhocView *view, struct wlr_subsurface *wlr_subsurface)
{
  PhocSubsurface *subsurface = g_new0 (PhocSubsurface, 1);

  subsurface->wlr_subsurface = wlr_subsurface;
  phoc_view_child_init (&subsurface->child, &subsurface_impl,
                        view, wlr_subsurface->surface);
  subsurface->child.mapped = wlr_subsurface->mapped;

  subsurface->destroy.notify = subsurface_handle_destroy;
  wl_signal_add (&wlr_subsurface->events.destroy, &subsurface->destroy);

  subsurface->map.notify = subsurface_handle_map;
  wl_signal_add (&wlr_subsurface->events.map, &subsurface->map);

  subsurface->unmap.notify = subsurface_handle_unmap;
  wl_signal_add (&wlr_subsurface->events.unmap, &subsurface->unmap);
}

static void
phoc_view_child_subsurface_create (PhocViewChild *child, struct wlr_subsurface *wlr_subsurface)
{
  PhocSubsurface *subsurface = g_new0 (PhocSubsurface, 1);

  subsurface->child.parent = child;
  child->children = g_slist_prepend (child->children, &subsurface->child);
  subsurface->wlr_subsurface = wlr_subsurface;
  phoc_view_child_init (&subsurface->child, &subsurface_impl, child->view,
                        wlr_subsurface->surface);
  subsurface->child.mapped = wlr_subsurface->mapped;

  subsurface->destroy.notify = subsurface_handle_destroy;
  wl_signal_add (&wlr_subsurface->events.destroy, &subsurface->destroy);

  subsurface->map.notify = subsurface_handle_map;
  wl_signal_add (&wlr_subsurface->events.map, &subsurface->map);

  subsurface->unmap.notify = subsurface_handle_unmap;
  wl_signal_add (&wlr_subsurface->events.unmap, &subsurface->unmap);

  phoc_view_child_damage_whole (&subsurface->child);
}

static void
phoc_view_handle_surface_new_subsurface (struct wl_listener *listener, void *data)
{
  PhocViewPrivate *priv = wl_container_of (listener, priv, surface_new_subsurface);
  PhocView *self = PHOC_VIEW_SELF (priv);
  struct wlr_subsurface *wlr_subsurface = data;

  phoc_view_subsurface_create (self, wlr_subsurface);
}

static gchar *
munge_app_id (const gchar *app_id)
{
  gchar *id = g_strdup (app_id);
  gint i;

  g_strcanon (id,
              "0123456789"
              "abcdefghijklmnopqrstuvwxyz"
              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              "-",
              '-');
  for (i = 0; id[i] != '\0'; i++)
    id[i] = g_ascii_tolower (id[i]);

  return id;
}

static void
view_update_scale (PhocView *view)
{
  PhocServer *server = phoc_server_get_default ();
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  if (!PHOC_VIEW_GET_CLASS (view)->want_scaling(view))
    return;

  struct wlr_output *output = view_get_output(view);
  if (!output)
    return;

  PhocOutput *phoc_output = output->data;
  float scalex = 1.0f, scaley = 1.0f, oldscale = priv->scale;

  if (priv->scale_to_fit || phoc_desktop_get_scale_to_fit (server->desktop)) {
    scalex = phoc_output->usable_area.width / (float)view->box.width;
    scaley = phoc_output->usable_area.height / (float)view->box.height;
    if (scaley < scalex)
      priv->scale = scaley;
    else
      priv->scale = scalex;

    if (priv->scale < 0.5f)
      priv->scale = 0.5f;

    if (priv->scale > 1.0f || view_is_fullscreen (view))
      priv->scale = 1.0f;
  } else {
    priv->scale = 1.0;
  }

  if (priv->scale != oldscale) {
    if (view_is_maximized(view)) {
      view_arrange_maximized (view, NULL);
    } else if (view_is_tiled (view)) {
      view_arrange_tiled (view, NULL);
    } else {
      view_center (view, NULL);
    }
  }
}


static void
on_global_scale_to_fit_changed (PhocView *self, GParamSpec *pspec, gpointer unused)
{
  view_update_scale (self);
}


void
phoc_view_map (PhocView *view, struct wlr_surface *surface)
{
  PhocServer *server = phoc_server_get_default ();
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  g_assert (view->wlr_surface == NULL);
  view->wlr_surface = surface;

  struct wlr_subsurface *subsurface;
  wl_list_for_each(subsurface, &view->wlr_surface->current.subsurfaces_below, current.link) {
    phoc_view_subsurface_create(view, subsurface);
  }
  wl_list_for_each(subsurface, &view->wlr_surface->current.subsurfaces_above, current.link) {
    phoc_view_subsurface_create(view, subsurface);
  }

  phoc_view_init_subsurfaces (view, surface);
  priv->surface_new_subsurface.notify = phoc_view_handle_surface_new_subsurface;
  wl_signal_add (&view->wlr_surface->events.new_subsurface, &priv->surface_new_subsurface);

  if (view->desktop->maximize) {
    phoc_view_appear_activated (view, true);

    if (!wl_list_empty(&view->desktop->views)) {
      // mapping a new stack may make the old stack disappear, so damage its area
      PhocView *top_view = wl_container_of(view->desktop->views.next, view, link);
      while (top_view) {
        phoc_view_damage_whole (top_view);
        top_view = top_view->parent;
      }
    }
  }

  wl_list_insert(&view->desktop->views, &view->link);
  phoc_view_damage_whole (view);
  phoc_input_update_cursor_focus(server->input);

  priv->notify_scale_to_fit_id =
    g_signal_connect_swapped (view->desktop,
                              "notify::scale-to-fit",
                              G_CALLBACK (on_global_scale_to_fit_changed),
                              view);


  /* Process pending activation */
  if (priv->activation_token)
    phoc_view_activate (view, TRUE);

  g_object_notify_by_pspec (G_OBJECT (view), props[PROP_IS_MAPPED]);
}

void view_unmap(PhocView *view) {
	PhocViewPrivate *priv = phoc_view_get_instance_private (view);

	g_assert (view->wlr_surface != NULL);

	bool was_visible = phoc_desktop_view_is_visible(view->desktop, view);

	phoc_view_damage_whole (view);

	wl_list_remove (&priv->surface_new_subsurface.link);

	PhocViewChild *child, *tmp;
	wl_list_for_each_safe(child, tmp, &priv->child_surfaces, link) {
		phoc_view_child_destroy(child);
	}

	if (view_is_fullscreen (view)) {
		phoc_output_damage_whole (priv->fullscreen_output);
		priv->fullscreen_output->fullscreen_view = NULL;
		priv->fullscreen_output = NULL;
	}

	wl_list_remove(&view->link);

	if (was_visible && view->desktop->maximize && !wl_list_empty(&view->desktop->views)) {
		// damage the newly activated stack as well since it may have just become visible
		PhocView *top_view = wl_container_of(view->desktop->views.next, view, link);
		while (top_view) {
			phoc_view_damage_whole (top_view);
			top_view = top_view->parent;
		}
	}

	view->wlr_surface = NULL;
	view->box.width = view->box.height = 0;

	if (priv->toplevel_handle) {
		priv->toplevel_handle->data = NULL;
		wlr_foreign_toplevel_handle_v1_destroy(priv->toplevel_handle);
		priv->toplevel_handle = NULL;
	}

	g_clear_signal_handler (&priv->notify_scale_to_fit_id, view->desktop);

	g_object_notify_by_pspec (G_OBJECT (view), props[PROP_IS_MAPPED]);
}

void view_initial_focus(PhocView *view) {
	PhocServer *server = phoc_server_get_default ();
	PhocInput *input = server->input;
	// TODO what seat gets focus? the one with the last input event?
	for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
		PhocSeat *seat = PHOC_SEAT (elem->data);

		g_assert (PHOC_IS_SEAT (seat));
		phoc_seat_set_focus(seat, view);
	}
}

/**
 * view_send_frame_done_if_not_visible:
 * @view: The #PhocView
 *
 * For views that aren't visible, EGL-Wayland can be stuck
 * in eglSwapBuffers waiting for frame done event. This function
 * helps it get unstuck, so further events can actually be processed
 * by the client. It's worth calling this function when sending
 * events like `configure` or `close`, as these should get processed
 * immediately regardless of surface visibility.
 */
void
view_send_frame_done_if_not_visible (PhocView *view)
{
  if (!phoc_desktop_view_is_visible (view->desktop, view) && phoc_view_is_mapped (view)) {
    struct timespec now;
    clock_gettime (CLOCK_MONOTONIC, &now);
    wlr_surface_send_frame_done (view->wlr_surface, &now);
  }
}

static void view_create_foreign_toplevel_handle (PhocView *view);

void view_setup(PhocView *view) {
        PhocViewPrivate *priv = phoc_view_get_instance_private (view);
        struct wlr_foreign_toplevel_handle_v1 *toplevel_handle = NULL;

	view_create_foreign_toplevel_handle(view);
	view_initial_focus(view);

	view_center(view, NULL);
	view_update_scale(view);

	view_update_output(view, NULL);

	wlr_foreign_toplevel_handle_v1_set_fullscreen(priv->toplevel_handle,
	                                              view_is_fullscreen (view));
	wlr_foreign_toplevel_handle_v1_set_maximized(priv->toplevel_handle,
	                                             view_is_maximized(view));
	wlr_foreign_toplevel_handle_v1_set_title(priv->toplevel_handle,
	                                         priv->title ?: "");
	wlr_foreign_toplevel_handle_v1_set_app_id(priv->toplevel_handle,
	                                          priv->app_id ?: "");
	if (view->parent)
		toplevel_handle = phoc_view_get_toplevel_handle (view->parent);

	wlr_foreign_toplevel_handle_v1_set_parent(priv->toplevel_handle, toplevel_handle);
}

/**
 * phoc_view_apply_damage:
 * @view: A view
 *
 * Add the accumulated buffer damage of all surfaces belonging to a
 * [class@PhocView] to the damaged screen area that needs repaint.
 */
void
phoc_view_apply_damage (PhocView *view)
{
  PhocOutput *output;

  wl_list_for_each (output, &view->desktop->outputs, link)
    phoc_output_damage_from_view (output, view, false);
}

/**
 * phoc_view_damage_whole:
 * @view: A view
 *
 * Add the damage of all surfaces belonging to a [class@PhocView] to the
 * damaged screen area that needs repaint. This damages the whole
 * @view (possibly including server side window decorations) ignoring
 * any buffer damage.
 */
void
phoc_view_damage_whole (PhocView *view)
{
  PhocOutput *output;
  wl_list_for_each(output, &view->desktop->outputs, link)
    phoc_output_damage_from_view (output, view, true);
}


void
view_update_position (PhocView *view, int x, int y)
{
  if (view->box.x == x && view->box.y == y)
    return;

  struct wlr_box before;
  view_get_box(view, &before);
  phoc_view_damage_whole (view);
  view->box.x = x;
  view->box.y = y;
  view_update_output(view, &before);
  phoc_view_damage_whole (view);
}

void
view_update_size (PhocView *view, int width, int height)
{
  struct wlr_box before;

  if (view->box.width == width && view->box.height == height)
    return;

  view_get_box(view, &before);
  phoc_view_damage_whole (view);
  view->box.width = width;
  view->box.height = height;
  if (view->pending_centering ||
      (view_is_floating (view) && phoc_desktop_get_auto_maximize (view->desktop))) {
    view_center (view, NULL);
    view->pending_centering = false;
  }
  view_update_scale (view);
  view_update_output (view, &before);
  phoc_view_damage_whole (view);
}

void
view_update_decorated (PhocView *view, bool decorated)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  if (priv->decorated == decorated)
    return;

  phoc_view_damage_whole (view);
  if (decorated)
    phoc_view_set_decoration (view, TRUE, 12, 4);
  else
    phoc_view_set_decoration (view, FALSE, 0, 0);

  phoc_view_damage_whole (view);
}

void
view_set_title (PhocView *view, const char *title)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  g_free (priv->title);
  priv->title = g_strdup (title);

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_title(priv->toplevel_handle, title ?: "");
}

void view_set_parent (PhocView *view, PhocView *parent)
{
  // setting a new parent may cause a cycle
  PhocView *node = parent;
  PhocViewPrivate *priv;
  struct wlr_foreign_toplevel_handle_v1 *toplevel_handle = NULL;

  while (node) {
    g_return_if_fail (node != view);
    node = node->parent;
  }

  if (view->parent) {
    wl_list_remove (&view->parent_link);
    wl_list_init (&view->parent_link);
  }

  view->parent = parent;
  if (parent)
    wl_list_insert (&parent->stack, &view->parent_link);

  priv = phoc_view_get_instance_private (view);
  if (view->parent)
    toplevel_handle = phoc_view_get_toplevel_handle (view->parent);

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_parent(priv->toplevel_handle, toplevel_handle);
}


static void
bind_scale_to_fit_setting (PhocView *self)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (self);

  g_clear_object (&priv->settings);

  if (priv->app_id) {
    g_autofree gchar *munged_app_id = munge_app_id (priv->app_id);
    g_autofree gchar *path = g_strconcat ("/sm/puri/phoc/application/", munged_app_id, "/", NULL);
    priv->settings = g_settings_new_with_path ("sm.puri.phoc.application", path);

    g_settings_bind (priv->settings,
                     "scale-to-fit",
                     self,
                     "scale-to-fit",
                     G_SETTINGS_BIND_GET);
  }
}


void
phoc_view_set_app_id (PhocView *view, const char *app_id)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  if (g_strcmp0 (priv->app_id, app_id)) {
    g_free (priv->app_id);
    priv->app_id = g_strdup (app_id);

    bind_scale_to_fit_setting (view);
  }

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_app_id (priv->toplevel_handle, app_id ?: "");
}

static void
handle_toplevel_handle_request_maximize (struct wl_listener *listener,void *data)
{
  PhocViewPrivate *priv = wl_container_of (listener, priv, toplevel_handle_request_maximize);
  PhocView *self = PHOC_VIEW_SELF (priv);
  struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;

  if (event->maximized)
    view_maximize (self, NULL);
  else
    view_restore (self);
}

static void
handle_toplevel_handle_request_activate (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocViewPrivate *priv = wl_container_of(listener, priv, toplevel_handle_request_activate);
  PhocView *self = PHOC_VIEW_SELF (priv);
  struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;

  for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    if (event->seat == seat->seat)
      phoc_seat_set_focus (seat, self);
  }
}

static void
handle_toplevel_handle_request_fullscreen (struct wl_listener *listener, void *data)
{
  PhocViewPrivate *priv = wl_container_of(listener, priv, toplevel_handle_request_fullscreen);
  PhocView *self = PHOC_VIEW_SELF (priv);
  struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

  phoc_view_set_fullscreen (self, event->fullscreen, event->output);
}

static void
handle_toplevel_handle_request_close (struct wl_listener *listener, void *data)
{
  PhocViewPrivate *priv = wl_container_of(listener, priv, toplevel_handle_request_close);
  PhocView *self = PHOC_VIEW_SELF (priv);

  phoc_view_close (self);
}

static void
view_create_foreign_toplevel_handle (PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  priv->toplevel_handle =
    wlr_foreign_toplevel_handle_v1_create(view->desktop->foreign_toplevel_manager_v1);
  g_assert (priv->toplevel_handle);

  priv->toplevel_handle_request_maximize.notify = handle_toplevel_handle_request_maximize;
  wl_signal_add(&priv->toplevel_handle->events.request_maximize,
                &priv->toplevel_handle_request_maximize);

  priv->toplevel_handle_request_activate.notify = handle_toplevel_handle_request_activate;
  wl_signal_add(&priv->toplevel_handle->events.request_activate,
                &priv->toplevel_handle_request_activate);

  priv->toplevel_handle_request_fullscreen.notify = handle_toplevel_handle_request_fullscreen;
  wl_signal_add(&priv->toplevel_handle->events.request_fullscreen,
                &priv->toplevel_handle_request_fullscreen);

  priv->toplevel_handle_request_close.notify = handle_toplevel_handle_request_close;
  wl_signal_add(&priv->toplevel_handle->events.request_close, &priv->toplevel_handle_request_close);

  priv->toplevel_handle->data = view;
}


static void
phoc_view_set_property (GObject      *object,
			  guint         property_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
  PhocView *self = PHOC_VIEW (object);

  switch (property_id) {
  case PROP_SCALE_TO_FIT:
    phoc_view_set_scale_to_fit (self, g_value_get_boolean (value));
    break;
  case PROP_ACTIVATION_TOKEN:
    phoc_view_set_activation_token (self, g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_view_get_property (GObject    *object,
                        guint       property_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  PhocView *self = PHOC_VIEW (object);
  PhocViewPrivate *priv = phoc_view_get_instance_private (self);

  switch (property_id) {
  case PROP_SCALE_TO_FIT:
    g_value_set_boolean (value, priv->scale_to_fit);
    break;
  case PROP_ACTIVATION_TOKEN:
    g_value_set_string (value, phoc_view_get_activation_token (self));
    break;
  case PROP_IS_MAPPED:
    g_value_set_boolean (value, phoc_view_is_mapped (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_view_finalize (GObject *object)
{
  PhocView *self = PHOC_VIEW (object);
  PhocViewPrivate *priv = phoc_view_get_instance_private (self);

  if (self->parent) {
    wl_list_remove(&self->parent_link);
    wl_list_init(&self->parent_link);
  }
  PhocView *child, *tmp;
  wl_list_for_each_safe(child, tmp, &self->stack, parent_link) {
    wl_list_remove(&child->parent_link);
    wl_list_init(&child->parent_link);
    child->parent = self->parent;
    if (child->parent) {
      wl_list_insert(&child->parent->stack, &child->parent_link);
    }
  }

  if (self->wlr_surface != NULL) {
    view_unmap(self);
  }

  // Can happen if fullscreened while unmapped, and hasn't been mapped
  if (view_is_fullscreen (self)) {
    priv->fullscreen_output->fullscreen_view = NULL;
  }

  g_clear_pointer (&priv->title, g_free);
  g_clear_pointer (&priv->app_id, g_free);
  g_clear_pointer (&priv->activation_token, g_free);
  g_clear_object (&priv->settings);

  G_OBJECT_CLASS (phoc_view_parent_class)->finalize (object);
}


static void
phoc_view_for_each_surface_default (PhocView                    *self,
                                    wlr_surface_iterator_func_t  iterator,
                                    gpointer                     user_data)
{
  if (self->wlr_surface == NULL)
    return;

  wlr_surface_for_each_surface (self->wlr_surface, iterator, user_data);
}


static void
phoc_view_get_geometry_default (PhocView *self, struct wlr_box *geom)
{
   PhocViewPrivate *priv;

   priv = phoc_view_get_instance_private (self);

   geom->x = 0;
   geom->y = 0;
   geom->width = self->box.width * priv->scale;
   geom->height = self->box.height * priv->scale;
}


static void
phoc_view_set_tiled_default (PhocView *self, bool tiled)
{
  if (tiled) {
    /* fallback to the maximized flag on the toplevel so it can remove its drop shadows */
    PHOC_VIEW_GET_CLASS (self)->set_maximized (self, true);
  }
}


static struct wlr_surface *
phoc_view_get_wlr_surface_at_default (PhocView *self,
                                      double    sx,
                                      double    sy,
                                      double   *sub_x,
                                      double   *sub_y)
{
  return wlr_surface_surface_at (self->wlr_surface, sx, sy, sub_x, sub_y);
}


static void
phoc_view_resize_default (PhocView *self, uint32_t width, uint32_t height)
{
  g_assert_not_reached ();
}


static void
phoc_view_move_resize_default (PhocView *self, double x, double y, uint32_t width, uint32_t height)
{
  g_assert_not_reached ();
}


static bool
phoc_view_want_automaximize_default (PhocView *self)
{
  g_assert_not_reached ();
}


static void
phoc_view_set_active_default (PhocView *self, bool active)
{
  g_assert_not_reached ();
}


static void
phoc_view_set_fullscreen_default (PhocView *self, bool fullscreen)
{
  g_assert_not_reached ();
}


static void
phoc_view_set_maximized_default (PhocView *self, bool maximized)
{
  g_assert_not_reached ();
}


static void
phoc_view_set_close_default (PhocView *self)
{
  g_assert_not_reached ();
}


static void
phoc_view_class_init (PhocViewClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  PhocViewClass *view_class = PHOC_VIEW_CLASS (klass);

  object_class->finalize = phoc_view_finalize;
  object_class->get_property = phoc_view_get_property;
  object_class->set_property = phoc_view_set_property;

  /* Optional */
  view_class->for_each_surface = phoc_view_for_each_surface_default;
  view_class->get_geometry = phoc_view_get_geometry_default;
  view_class->move = phoc_view_move_default;
  view_class->set_tiled = phoc_view_set_tiled_default;
  view_class->get_wlr_surface_at = phoc_view_get_wlr_surface_at_default;
  /* Mandatory */
  view_class->resize = phoc_view_resize_default;
  view_class->move_resize = phoc_view_move_resize_default;
  view_class->want_auto_maximize = phoc_view_want_automaximize_default;
  view_class->set_active = phoc_view_set_active_default;
  view_class->set_fullscreen = phoc_view_set_fullscreen_default;
  view_class->set_maximized = phoc_view_set_maximized_default;
  view_class->close = phoc_view_set_close_default;

  /**
   * PhocView:scale-to-fit:
   *
   * If %TRUE if surface will be scaled down to fit the screen.
   */
  props[PROP_SCALE_TO_FIT] =
    g_param_spec_boolean ("scale-to-fit", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * PhocView:activation-token:
   *
   * If not %NULL this token will be used to activate the view once mapped.
   */
  props[PROP_ACTIVATION_TOKEN] =
    g_param_spec_string ("activation-token", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhocView:is-mapped:
   *
   * Whether the view is currently mapped
   */
  props[PROP_IS_MAPPED] =
    g_param_spec_boolean ("is-mapped", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhocView:surface-destroy:
   *
   * Derived classes emit this signal just before dropping their ref so reference holders
   * can react.
   */
  signals[SURFACE_DESTROY] =
    g_signal_new ("surface-destroy",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
}


static void
phoc_view_init (PhocView *self)
{
  PhocViewPrivate *priv;

  priv = phoc_view_get_instance_private (self);
  priv->alpha = 1.0f;
  priv->scale = 1.0f;
  priv->state = PHOC_VIEW_STATE_FLOATING;

  wl_list_init (&priv->child_surfaces);
  wl_list_init(&self->stack);

  self->desktop = phoc_server_get_default ()->desktop;
}


/**
 * phoc_view_from_wlr_surface:
 * @wlr_surface: The wlr_surface
 *
 * Given a `wlr_surface` return the corresponding [class@View].
 *
 * Returns: (transfer none): The corresponding view
 */
PhocView *
phoc_view_from_wlr_surface (struct wlr_surface *wlr_surface)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  PhocView *view;

  wl_list_for_each(view, &desktop->views, link) {
    if (view->wlr_surface == wlr_surface) {
      return view;
    }
  }

  return NULL;
}

/**
 * phoc_view_is_mapped:
 * @view: (nullable): The view to check
 *
 * Check if a @view is currently mapped
 * Returns: %TRUE if a view is currently mapped, otherwise %FALSE
 */
bool
phoc_view_is_mapped (PhocView *view)
{
  return view && view->wlr_surface;
}


PhocViewTileDirection
phoc_view_get_tile_direction (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->tile_direction;
}


/**
 * phoc_view_child_destroy:
 * @child: The view child to destroy
 *
 * Destroys a view child freeing its resources.
 */
void
phoc_view_child_destroy (PhocViewChild *child)
{
  if (child == NULL)
    return;

  if (phoc_view_child_is_mapped (child) && phoc_view_is_mapped (child->view))
    phoc_view_child_damage_whole (child);

  /* Remove from parent if it's also a PhocChild */
  if (child->parent != NULL) {
    child->parent->children = g_slist_remove (child->parent->children, child);
    child->parent = NULL;
  }

  /* Detach us from all children */
  for (GSList *elem = child->children; elem; elem = elem->next) {
    PhocViewChild *subchild = elem->data;
    subchild->parent = NULL;
    /* The subchild lost its parent, so it cannot see that the parent is unmapped. Unmap it directly */
    subchild->mapped = false;
  }
  g_clear_pointer (&child->children, g_slist_free);

  wl_list_remove(&child->link);
  wl_list_remove(&child->commit.link);
  wl_list_remove(&child->new_subsurface.link);

  child->impl->destroy(child);
}

/*
 * phoc_view_child_apply_damage:
 * @child: A view child
 *
 * This is the equivalent of [method@Phoc.View.apply_damage] but for
 * [struct@Phoc.ViewChild].
 */
void
phoc_view_child_apply_damage (PhocViewChild *child)
{
  if (!child || !phoc_view_child_is_mapped (child) || !phoc_view_is_mapped (child->view))
    return;

  phoc_view_apply_damage (child->view);
}

/**
 * phoc_view_child_damage_whole:
 * @child: A view child
 *
 * This is the equivalent of [method@Phoc.View.damage_whole] but for
 * [struct@Phoc.ViewChild].
 */
void
phoc_view_child_damage_whole (PhocViewChild *child)
{
  if (!child || !phoc_view_child_is_mapped (child) || !phoc_view_is_mapped (child->view))
    return;

  /* TODO: just damage the whole child instead of the whole view */
  phoc_view_damage_whole (child->view);
}


/**
 * phoc_view_set_scale_to_fit:
 * @self: The view
 * @enable: Whether to enable or disable scale to fit
 *
 * Turn auto scaling if oversized for this surface on (%TRUE) or off (%FALSE)
 */
void
phoc_view_set_scale_to_fit (PhocView *self, gboolean enable)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (priv->scale_to_fit == enable)
    return;

  priv->scale_to_fit = enable;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SCALE_TO_FIT]);

  view_update_scale (self);
}

/**
 * phoc_view_get_scale_to_fit:
 * @self: The view
 *
 * Returns: %TRUE if scaling of oversized surfaces is enabled, %FALSE otherwise
 */
gboolean
phoc_view_get_scale_to_fit (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->scale_to_fit;
}


/**
 * phoc_view_set_activation_token:
 * @self: The view
 * @token: The activation token to use
 *
 * Sets the activation token that will be used when activate the view
 * once mapped. It will be cleared once the view got activated.
 */
void
phoc_view_set_activation_token (PhocView *self, const char* token)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (g_strcmp0 (priv->activation_token, token) == 0)
    return;

  g_free (priv->activation_token);
  priv->activation_token = g_strdup (token);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVATION_TOKEN]);
}

/**
 * phoc_view_get_activation_token:
 * @self: The view
 *
 * Get the current activation token.
 *
 * Returns: The activation token
 */
const char *
phoc_view_get_activation_token (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->activation_token;
}

/**
 * phoc_view_get_alpha
 * @self: The view
 *
 * Get the surface's transparency
 *
 * Returns: The surface alpha
 */
float
phoc_view_get_alpha (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->alpha;
}

/**
 * phoc_view_get_scale
 * @self: The view
 *
 * Get the surface's scale
 *
 * Returns: The surface scale
 */
float
phoc_view_get_scale (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->scale;
}

/**
 * phoc_view_set_decoration
 * @self: The view
 * @decorated: Whether the compositor should draw window decorations
 * @titlebar_height: The height of the titlebar
 * @border_width: The border width
 *
 * Sets whether the window is decorated. If %TRUE also specifies the
 * decoration.
 */
void
phoc_view_set_decoration (PhocView *self, gboolean decorated, int titlebar_height, int border_width)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  priv->decorated = decorated;
  if (decorated) {
    priv->titlebar_height = titlebar_height;
    priv->border_width = border_width;
  } else {
    priv->titlebar_height = 0;
    priv->border_width = 0;
  }
}


gboolean
phoc_view_is_decorated (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->decorated;
}


void
phoc_view_for_each_surface (PhocView                    *self,
                            wlr_surface_iterator_func_t  iterator,
                            void                        *user_data)
{
  g_assert (PHOC_IS_VIEW (self));

  PHOC_VIEW_GET_CLASS (self)->for_each_surface (self, iterator, user_data);
}


void
phoc_view_get_geometry (PhocView *self, struct wlr_box *geom)
{
  g_assert (PHOC_IS_VIEW (self));

  PHOC_VIEW_GET_CLASS (self)->get_geometry (self, geom);
}


void
phoc_view_move (PhocView *self, double x, double y)
{
  g_assert (PHOC_IS_VIEW (self));

  if (self->box.x == x && self->box.y == y)
    return;

  self->pending_move_resize.update_x = false;
  self->pending_move_resize.update_y = false;
  self->pending_centering = false;

  PHOC_VIEW_GET_CLASS (self)->move(self, x, y);
}


void
phoc_view_close (PhocView *self)
{
  PHOC_VIEW_GET_CLASS (self)->close(self);
}

struct wlr_surface *
phoc_view_get_wlr_surface_at (PhocView *self, double sx, double sy, double *sub_x, double *sub_y)
{
  g_assert (PHOC_IS_VIEW (self));

  return PHOC_VIEW_GET_CLASS (self)->get_wlr_surface_at (self, sx, sy, sub_x, sub_y);
}

const char *
phoc_view_get_app_id (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->app_id;
}
