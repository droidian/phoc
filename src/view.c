#define G_LOG_DOMAIN "phoc-view"

#include "phoc-config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_output_layout.h>

#include "phoc-enums.h"

#include "bling.h"
#include "cursor.h"
#include "view-deco.h"
#include "desktop.h"
#include "input.h"
#include "seat.h"
#include "server.h"
#include "subsurface.h"
#include "utils.h"
#include "timed-animation.h"
#include "view-child-private.h"
#include "view-private.h"

#define PHOC_ANIM_DURATION_WINDOW_FADE 150
#define PHOC_MOVE_TO_CORNER_MARGIN 12

enum {
  PROP_0,
  PROP_SCALE_TO_FIT,
  PROP_ACTIVATION_TOKEN,
  PROP_IS_MAPPED,
  PROP_ALPHA,
  PROP_DECORATED,
  PROP_STATE,
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
  pid_t          pid;

  float          alpha;
  float          scale;
  PhocViewDeco  *deco;
  gboolean       decorated;
  PhocViewState  state;
  PhocViewTileDirection tile_direction;
  gboolean       always_on_top;

  PhocOutput    *fullscreen_output;

  gulong         notify_scale_to_fit_id;
  gboolean       scale_to_fit;
  char          *activation_token;
  int            activation_token_type;
  GSList        *blings; /* PhocBlings */

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

static bool view_center (PhocView *view, PhocOutput *output);


static void
toggle_decoration (PhocView *self)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (self);
  gboolean needs_decoration;

  needs_decoration = priv->decorated;

  /* TODO: only in auto-moximize mode */
  if (priv->state == PHOC_VIEW_STATE_MAXIMIZED)
    needs_decoration = FALSE;

  if (!!needs_decoration == !!priv->deco)
    return;

  if (needs_decoration) {
    priv->deco = phoc_view_deco_new (self);
    phoc_view_add_bling (self, PHOC_BLING (priv->deco));
    phoc_bling_map (PHOC_BLING (priv->deco));
  } else {
    if (priv->deco) {
      phoc_bling_unmap (PHOC_BLING (priv->deco));
      phoc_view_remove_bling (self, PHOC_BLING (priv->deco));
    }
    g_clear_object (&priv->deco);
  }
}


static struct wlr_foreign_toplevel_handle_v1 *
phoc_view_get_toplevel_handle (PhocView *self)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (self);

  return priv->toplevel_handle;
}


gboolean
phoc_view_is_floating (PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  return priv->state == PHOC_VIEW_STATE_FLOATING && !phoc_view_is_fullscreen (view);
}

gboolean
phoc_view_is_maximized (PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  return priv->state == PHOC_VIEW_STATE_MAXIMIZED && !phoc_view_is_fullscreen (view);
}

gboolean
phoc_view_is_tiled (PhocView *view)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  return priv->state == PHOC_VIEW_STATE_TILED && !phoc_view_is_fullscreen (view);
}

gboolean
phoc_view_is_fullscreen (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return (priv->fullscreen_output != NULL);
}

/**
 * phoc_view_get_fullscreen_output:
 * @self: The view
 *
 * Gets the output a view is fullscreen on. Returns %NULL if
 * the view isn't currently fullscreen.
 *
 * Returns:(transfer none)(nullable): The fullscreen output
 */
PhocOutput *
phoc_view_get_fullscreen_output (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->fullscreen_output;
}

void
phoc_view_get_box (PhocView *view, struct wlr_box *box)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  box->x = view->box.x;
  box->y = view->box.y;
  box->width = view->box.width * priv->scale;
  box->height = view->box.height * priv->scale;
}


PhocViewDecoPart
phoc_view_get_deco_part (PhocView *self, double sx, double sy)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (!priv->deco)
    return PHOC_VIEW_DECO_PART_NONE;

  return phoc_view_deco_get_part (priv->deco, sx, sy);
}


static void
surface_send_enter_iterator (struct wlr_surface *wlr_surface, int x, int y, void *data)
{
  struct wlr_output *wlr_output = data;

  phoc_utils_wlr_surface_enter_output (wlr_surface, wlr_output);
}


static void
surface_send_leave_iterator (struct wlr_surface *wlr_surface, int x, int y, void *data)
{
  struct wlr_output *wlr_output = data;

  phoc_utils_wlr_surface_leave_output (wlr_surface, wlr_output);
}


static void
view_update_output (PhocView *view, const struct wlr_box *before)
{
  PhocDesktop *desktop = view->desktop;
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  if (!phoc_view_is_mapped (view))
    return;

  struct wlr_box box;
  phoc_view_get_box (view, &box);

  PhocOutput *output;
  wl_list_for_each (output, &desktop->outputs, link) {
    bool intersected, intersects;

    intersected = before && wlr_output_layout_intersects (desktop->layout,
                                                          output->wlr_output,
                                                          before);
    intersects = wlr_output_layout_intersects (desktop->layout, output->wlr_output, &box);

    if (intersected && !intersects) {
      phoc_view_for_each_surface (view, surface_send_leave_iterator, output->wlr_output);
      if (priv->toplevel_handle)
        wlr_foreign_toplevel_handle_v1_output_leave (priv->toplevel_handle, output->wlr_output);
    }

    if (!intersected && intersects) {
      phoc_view_for_each_surface (view, surface_send_enter_iterator, output->wlr_output);

      if (priv->toplevel_handle)
        wlr_foreign_toplevel_handle_v1_output_enter (priv->toplevel_handle, output->wlr_output);
    }
  }
}


static void
view_save (PhocView *view)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  if (!phoc_view_is_floating (view))
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

/**
 * phoc_view_activate:
 * @self : The view
 * @activate: Whether to activate or deactivate a view
 *
 * Performs the necessary steps to make the view itself appear activated
 * and send out the corresponding view related protocol events.
 * Note that this is not enough to actually focus the view for the user
 * See [method@Seat.set_focus_view].
 */
void
phoc_view_activate (PhocView *self, bool activate)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  /* UGLY: waydroid windows always need the activate signal */
  if ((priv->app_id && g_str_has_prefix (priv->app_id, "waydroid.")) || !self->desktop->maximize)
    phoc_view_appear_activated (self, activate);

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_activated (priv->toplevel_handle, activate);

  if (activate && phoc_view_is_fullscreen (self))
    phoc_output_force_shell_reveal (priv->fullscreen_output, false);
}


static void
phoc_view_resize (PhocView *self, uint32_t width, uint32_t height)
{
  g_assert (PHOC_IS_VIEW (self));

  PHOC_VIEW_GET_CLASS (self)->resize (self, width, height);
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

  PHOC_VIEW_GET_CLASS (view)->move_resize (view, x, y, width, height);
}

/**
 * phoc_view_get_maximized_box:
 * self: The view to get the box for
 * output: The output the view is on
 * box: (out): The box used if the view was maximized
 *
 * Gets the "visible bounds" that a view will use on a given output
 * when maximized.
 *
 * Returns: %TRUE if the box can be maximized, otherwise %FALSE.
 */
gboolean
phoc_view_get_maximized_box (PhocView *self, PhocOutput *output, struct wlr_box *box)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (phoc_view_is_fullscreen (self))
    return FALSE;

  if (!output)
    output = phoc_view_get_output (self);

  if (!output)
    return FALSE;

  struct wlr_box output_box;
  wlr_output_layout_get_box (self->desktop->layout, output->wlr_output, &output_box);
  struct wlr_box usable_area = output->usable_area;
  usable_area.x += output_box.x;
  usable_area.y += output_box.y;

  box->x = usable_area.x / priv->scale;
  box->y = usable_area.y / priv->scale;
  box->width = usable_area.width / priv->scale;
  box->height = usable_area.height / priv->scale;

  return TRUE;
}


static void
view_arrange_maximized (PhocView *self, PhocOutput *output)
{
  PhocViewPrivate *priv;
  struct wlr_box box, geom;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (!phoc_view_get_maximized_box (self, output, &box))
    return;

  phoc_view_get_geometry (self, &geom);
  box.x -= geom.x / priv->scale;
  box.y -= geom.y / priv->scale;

  phoc_view_move_resize (self, box.x, box.y, box.width, box.height);
}


/**
 * phoc_view_get_tiled_box:
 * self: The view to get the box for
 * output: The output the view is on
 * box: (out): The box used if the view was tiled
 *
 * Gets the "visible bounds" a view will use on a given output when
 * tiled.
 *
 * Returns: %TRUE if the box can be maximized, otherwise %FALSE.
 */
gboolean
phoc_view_get_tiled_box (PhocView               *self,
                         PhocViewTileDirection   dir,
                         PhocOutput             *output,
                         struct wlr_box         *box)
{
  PhocViewPrivate *priv;

  g_assert (box);
  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (phoc_view_is_fullscreen (self))
    return FALSE;

  if (!output)
    output = phoc_view_get_output (self);

  if (!output)
    return FALSE;

  struct wlr_box output_box;
  wlr_output_layout_get_box (self->desktop->layout, output->wlr_output, &output_box);
  struct wlr_box usable_area = output->usable_area;
  int x;

  usable_area.x += output_box.x;
  usable_area.y += output_box.y;

  switch (dir) {
  case PHOC_VIEW_TILE_LEFT:
    x = usable_area.x;
    break;
  case PHOC_VIEW_TILE_RIGHT:
    x = usable_area.x + (0.5 * usable_area.width);
    break;
  default:
    g_error ("Invalid tiling direction %d", dir);
  }

  box->x = x / priv->scale;
  box->y = usable_area.y / priv->scale;
  box->width = usable_area.width / 2 / priv->scale;
  box->height = usable_area.height / priv->scale;

  return TRUE;
}


static void
view_arrange_tiled (PhocView *self, PhocOutput *output)
{
  PhocViewPrivate *priv;
  struct wlr_box box, geom;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (!phoc_view_get_tiled_box (self, priv->tile_direction, output, &box))
    return;

  phoc_view_get_geometry (self, &geom);
  box.x -= geom.x / priv->scale;
  box.y -= geom.y / priv->scale;

  phoc_view_move_resize (self, box.x, box.y, box.width, box.height);
}


void
phoc_view_maximize (PhocView *self, PhocOutput *output)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (phoc_view_is_maximized (self) && phoc_view_get_output (self) == output)
    return;

  if (phoc_view_is_fullscreen (self))
    return;

  PHOC_VIEW_GET_CLASS (self)->set_tiled (self, false);
  PHOC_VIEW_GET_CLASS (self)->set_maximized (self, true);

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_maximized (priv->toplevel_handle, true);

  view_save (self);

  priv->state = PHOC_VIEW_STATE_MAXIMIZED;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);

  view_arrange_maximized (self, output);
}

/**
 * phoc_view_auto_maximize:
 * @view: a view
 *
 * Maximize `view` if in auto-maximize mode otherwise do nothing.
 */
void
phoc_view_auto_maximize (PhocView *view)
{
  if (phoc_view_want_auto_maximize (view))
    phoc_view_maximize (view, NULL);
}

/**
 * phoc_view_restore:
 * @view: The view to restore
 *
 * Put a view back into floating state while restoring it's previous
 * size and position.
 */
void
phoc_view_restore (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (!phoc_view_is_maximized (self) && !phoc_view_is_tiled (self))
    return;

  if (phoc_view_want_auto_maximize (self))
    return;

  struct wlr_box geom;
  phoc_view_get_geometry (self, &geom);

  priv->state = PHOC_VIEW_STATE_FLOATING;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);

  if (!wlr_box_empty (&self->saved)) {
    phoc_view_move_resize (self, self->saved.x - geom.x * priv->scale,
                           self->saved.y - geom.y * priv->scale,
                           self->saved.width, self->saved.height);
  } else {
    phoc_view_resize (self, 0, 0);
    self->pending_centering = true;
  }

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_maximized (priv->toplevel_handle, false);

  PHOC_VIEW_GET_CLASS (self)->set_maximized (self, false);
  PHOC_VIEW_GET_CLASS (self)->set_tiled (self, false);
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
void
phoc_view_set_fullscreen (PhocView *view, bool fullscreen, PhocOutput *output)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  bool was_fullscreen = phoc_view_is_fullscreen (view);

  if (was_fullscreen != fullscreen) {
    /* don't allow unfocused surfaces to make themselves fullscreen */
    if (fullscreen && phoc_view_is_mapped (view))
      g_return_if_fail (phoc_input_view_has_focus (input, view));

    PHOC_VIEW_GET_CLASS (view)->set_fullscreen (view, fullscreen);

    if (priv->toplevel_handle)
      wlr_foreign_toplevel_handle_v1_set_fullscreen (priv->toplevel_handle, fullscreen);
  }

  struct wlr_box view_geom;
  phoc_view_get_geometry (view, &view_geom);

  if (fullscreen) {
    if (output == NULL)
      output = phoc_view_get_output (view);

    if (was_fullscreen)
      priv->fullscreen_output->fullscreen_view = NULL;

    struct wlr_box view_box;
    phoc_view_get_box (view, &view_box);

    view_save (view);

    struct wlr_box output_box;
    wlr_output_layout_get_box (view->desktop->layout, output->wlr_output, &output_box);
    phoc_view_move_resize (view,
                           output_box.x - view_geom.x * priv->scale,
                           output_box.y - view_geom.y * priv->scale,
                           output_box.width,
                           output_box.height);

    output->fullscreen_view = view;
    phoc_output_force_shell_reveal (output, false);
    priv->fullscreen_output = output;
    phoc_output_damage_whole (output);
  }

  if (was_fullscreen && !fullscreen) {
    PhocOutput *phoc_output = priv->fullscreen_output;
    priv->fullscreen_output->fullscreen_view = NULL;
    priv->fullscreen_output = NULL;

    phoc_output_damage_whole (phoc_output);

    if (priv->state == PHOC_VIEW_STATE_MAXIMIZED) {
      view_arrange_maximized (view, phoc_output);
    } else if (priv->state == PHOC_VIEW_STATE_TILED) {
      view_arrange_tiled (view, phoc_output);
    } else if (!wlr_box_empty (&view->saved)) {
      phoc_view_move_resize (view,
                             view->saved.x - view_geom.x * priv->scale,
                             view->saved.y - view_geom.y * priv->scale,
                             view->saved.width,
                             view->saved.height);
    } else {
      phoc_view_resize (view, 0, 0);
      view->pending_centering = true;
    }

    phoc_view_auto_maximize (view);
  }

  phoc_server_set_linux_dmabuf_surface_feedback (phoc_server_get_default (),
                                                 view, priv->fullscreen_output, fullscreen);
}


bool
phoc_view_move_to_next_output (PhocView *view, enum wlr_direction direction)
{
  PhocDesktop *desktop = view->desktop;
  struct wlr_output_layout *layout = view->desktop->layout;
  const struct wlr_output_layout_output *l_output;
  PhocOutput *output;
  struct wlr_output *new_output;
  struct wlr_box usable_area;
  double x, y;

  output = phoc_view_get_output (view);
  if (!output)
    return false;

  /* use current view's x,y as ref_lx, ref_ly */
  new_output = wlr_output_layout_adjacent_output (layout, direction, output->wlr_output,
                                                  view->box.x, view->box.y);
  if (!new_output)
    return false;

  output = PHOC_OUTPUT (new_output->data);
  usable_area = output->usable_area;
  l_output = wlr_output_layout_get (desktop->layout, new_output);

  /* update saved position to the new output */
  x = usable_area.x + l_output->x + usable_area.width / 2 - view->saved.width / 2;
  y = usable_area.y + l_output->y + usable_area.height / 2 - view->saved.height / 2;
  g_debug ("moving view's saved position to %f %f", x, y);
  view->saved.x = x;
  view->saved.y = y;

  if (phoc_view_is_fullscreen (view)) {
    phoc_view_set_fullscreen (view, true, PHOC_OUTPUT (new_output->data));
    return true;
  }

  phoc_view_arrange (view, output, TRUE);
  return true;
}


void
phoc_view_move_to_corner (PhocView *self, PhocViewCorner corner)
{
  PhocViewPrivate *priv;
  PhocOutput *output;
  struct wlr_box usable_area, box, geom;
  float x,y;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  output = phoc_view_get_output (self);
  if (!output)
    return;

  /* TODO: Simplify saved vs actual state before enabling */
  if (priv->state != PHOC_VIEW_STATE_FLOATING || phoc_view_is_fullscreen (self))
    return;

  /* TODO: Simplify scale-to-fit vs geom before enabling */
  if (!G_APPROX_VALUE (priv->scale, 1.0, FLT_EPSILON)) {
    g_warning_once ("move-to-center not allowed for scale-to-fit-views");
    return;
  }

  usable_area = output->usable_area;
  phoc_view_get_box (self, &box);
  phoc_view_get_geometry (self, &geom);

  x = output->lx + usable_area.x - geom.x;
  y = output->ly + usable_area.y - geom.y;

  switch (corner) {
  case PHOC_VIEW_CORNER_NORTH_WEST:
    x += PHOC_MOVE_TO_CORNER_MARGIN;
    y += PHOC_MOVE_TO_CORNER_MARGIN;
    break;
  case PHOC_VIEW_CORNER_NORTH_EAST:
    x += usable_area.width - box.width - PHOC_MOVE_TO_CORNER_MARGIN;
    y += PHOC_MOVE_TO_CORNER_MARGIN;
    break;
  case PHOC_VIEW_CORNER_SOUTH_EAST:
    x += usable_area.width - box.width - PHOC_MOVE_TO_CORNER_MARGIN;
    y += usable_area.height - box.height - PHOC_MOVE_TO_CORNER_MARGIN;
    break;
  case PHOC_VIEW_CORNER_SOUTH_WEST:
    x += PHOC_MOVE_TO_CORNER_MARGIN;
    y += usable_area.height - box.height - PHOC_MOVE_TO_CORNER_MARGIN;
    break;
  default:
    g_assert_not_reached ();
  }

  phoc_view_move (self, x, y);
}


void
phoc_view_tile (PhocView *self, PhocViewTileDirection direction, PhocOutput *output)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (phoc_view_is_fullscreen (self))
    return;

  view_save (self);

  priv->state = PHOC_VIEW_STATE_TILED;
  priv->tile_direction = direction;

  PHOC_VIEW_GET_CLASS (self)->set_maximized (self, false);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
  PHOC_VIEW_GET_CLASS (self)->set_tiled (self, true);

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_maximized (priv->toplevel_handle, false);

  view_arrange_tiled (self, output);
}


static bool
view_center (PhocView *view, PhocOutput *output)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  struct wlr_box box, geom;
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);
  phoc_view_get_box (view, &box);
  phoc_view_get_geometry (view, &geom);

  if (!phoc_view_is_floating (view))
    return false;

  PhocDesktop *desktop = view->desktop;
  PhocSeat *seat = phoc_input_get_last_active_seat (input);
  PhocCursor *cursor;

  if (!seat)
    return false;

  cursor = phoc_seat_get_cursor (seat);

  if (!output) {
    struct wlr_output *wlr_output = wlr_output_layout_output_at (desktop->layout,
                                                                 cursor->cursor->x,
                                                                 cursor->cursor->y);
    if (!wlr_output) {
      // empty layout
      return false;
    }
    output = PHOC_OUTPUT (wlr_output->data);
  }

  const struct wlr_output_layout_output *l_output = wlr_output_layout_get (desktop->layout,
                                                                           output->wlr_output);
  struct wlr_box usable_area = output->usable_area;

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

  if (view->box.width > output->usable_area.width ||
      view->box.height > output->usable_area.height) {
    phoc_view_resize (view,
                      MIN (view->box.width, output->usable_area.width),
                      MIN (view->box.height, output->usable_area.height));
  }

  return true;
}


static void
phoc_view_init_subsurfaces (PhocView *view, struct wlr_surface *surface)
{
  struct wlr_subsurface *subsurface;

  wl_list_for_each (subsurface, &surface->current.subsurfaces_below, current.link)
    phoc_subsurface_new (view, subsurface);

  wl_list_for_each (subsurface, &surface->current.subsurfaces_above, current.link)
    phoc_subsurface_new (view, subsurface);
}


static void
phoc_view_handle_surface_new_subsurface (struct wl_listener *listener, void *data)
{
  PhocViewPrivate *priv = wl_container_of (listener, priv, surface_new_subsurface);
  PhocView *self = PHOC_VIEW_SELF (priv);
  struct wlr_subsurface *wlr_subsurface = data;

  phoc_subsurface_new (self, wlr_subsurface);
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
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (view));
  priv = phoc_view_get_instance_private (view);

  if (!PHOC_VIEW_GET_CLASS (view)->want_scaling (view))
    return;

  PhocOutput *output = phoc_view_get_output (view);
  if (!output)
    return;

  float scalex = 1.0f, scaley = 1.0f, oldscale = priv->scale;

  if (priv->scale_to_fit || phoc_desktop_get_scale_to_fit (desktop)) {
    scalex = output->usable_area.width / (float)view->box.width;
    scaley = output->usable_area.height / (float)view->box.height;
    if (scaley < scalex)
      priv->scale = scaley;
    else
      priv->scale = scalex;

    if (priv->scale < 0.5f)
      priv->scale = 0.5f;

    if (priv->scale > 1.0f || phoc_view_is_fullscreen (view))
      priv->scale = 1.0f;
  } else {
    priv->scale = 1.0;
  }

  if (priv->scale != oldscale)
    phoc_view_arrange (view, NULL, TRUE);
}


static void
on_global_scale_to_fit_changed (PhocView *self, GParamSpec *pspec, gpointer unused)
{
  view_update_scale (self);
}


void
phoc_view_map (PhocView *self, struct wlr_surface *surface)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocViewPrivate *priv = phoc_view_get_instance_private (self);

  g_assert (self->wlr_surface == NULL);
  self->wlr_surface = surface;

  phoc_view_init_subsurfaces (self, self->wlr_surface);
  priv->surface_new_subsurface.notify = phoc_view_handle_surface_new_subsurface;
  wl_signal_add (&self->wlr_surface->events.new_subsurface, &priv->surface_new_subsurface);

  if (self->desktop->maximize) {
    phoc_view_appear_activated (self, true);

    if (phoc_desktop_has_views (self->desktop)) {
      // mapping a new stack may make the old stack disappear, so damage its area
      PhocView *top_view = phoc_desktop_get_view_by_index (self->desktop, 0);
      while (top_view) {
        phoc_view_damage_whole (top_view);
        top_view = top_view->parent;
      }
    }
  }

  if (self->parent && phoc_view_is_always_on_top (self->parent))
    phoc_view_set_always_on_top (self, TRUE);

  phoc_desktop_insert_view (self->desktop, self);
  phoc_view_damage_whole (self);
  phoc_input_update_cursor_focus (input);
  priv->pid = PHOC_VIEW_GET_CLASS (self)->get_pid (self);

  priv->notify_scale_to_fit_id =
    g_signal_connect_swapped (self->desktop,
                              "notify::scale-to-fit",
                              G_CALLBACK (on_global_scale_to_fit_changed),
                              self);

  if (phoc_desktop_get_enable_animations (self->desktop)
      && self->parent == NULL
      && !phoc_view_want_auto_maximize (self)) {
    g_autoptr (PhocTimedAnimation) fade_anim = NULL;
    g_autoptr (PhocPropertyEaser) easer = NULL;
    easer = g_object_new (PHOC_TYPE_PROPERTY_EASER,
                          "target", self,
                          "easing", PHOC_EASING_EASE_OUT_QUAD,
                          NULL);
    phoc_property_easer_set_props (easer, "alpha", 0.0, 1.0, NULL);
    fade_anim = g_object_new (PHOC_TYPE_TIMED_ANIMATION,
                              "animatable", phoc_view_get_output (self),
                              "duration", PHOC_ANIM_DURATION_WINDOW_FADE,
                              "property-easer", easer,
                              "dispose-on-done", TRUE,
                              NULL);
    phoc_timed_animation_play (fade_anim);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_MAPPED]);
}

void
phoc_view_unmap (PhocView *view)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  g_assert (view->wlr_surface != NULL);

  bool was_visible = phoc_desktop_view_is_visible (view->desktop, view);

  phoc_view_damage_whole (view);

  wl_list_remove (&priv->surface_new_subsurface.link);

  PhocViewChild *child, *tmp;
  wl_list_for_each_safe (child, tmp, &priv->child_surfaces, link)
    g_object_unref (child);

  if (phoc_view_is_fullscreen (view)) {
    phoc_output_damage_whole (priv->fullscreen_output);
    priv->fullscreen_output->fullscreen_view = NULL;
    priv->fullscreen_output = NULL;
  }

  phoc_desktop_remove_view (view->desktop, view);

  if (was_visible && view->desktop->maximize && phoc_desktop_has_views (view->desktop)) {
    /* Damage the newly activated stack as well since it may have just become visible */
    PhocView *top_view = phoc_desktop_get_view_by_index (view->desktop, 0);
    while (top_view) {
      phoc_view_damage_whole (top_view);
      top_view = top_view->parent;
    }
  }

  view->wlr_surface = NULL;
  view->box.width = view->box.height = 0;

  if (priv->toplevel_handle) {
    priv->toplevel_handle->data = NULL;
    wlr_foreign_toplevel_handle_v1_destroy (priv->toplevel_handle);
    priv->toplevel_handle = NULL;
  }

  g_clear_signal_handler (&priv->notify_scale_to_fit_id, view->desktop);

  g_object_notify_by_pspec (G_OBJECT (view), props[PROP_IS_MAPPED]);
}


void
phoc_view_set_initial_focus (PhocView *self)
{
  PhocSeat *seat = phoc_server_get_last_active_seat (phoc_server_get_default ());

  /* This also submits any pending activation tokens */
  g_debug ("Initial focus view %p, token %s", self, phoc_view_get_activation_token (self));
  phoc_seat_set_focus_view (seat, self);
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

/**
 * phoc_view_setup:
 * @view: The view to setup
 *
 * Setup view parameters on map. This should be invoked by derived
 * classes past [method@phoc_view_map].
 */
void
phoc_view_setup (PhocView *view)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);
  struct wlr_foreign_toplevel_handle_v1 *toplevel_handle = NULL;

  view_create_foreign_toplevel_handle (view);
  phoc_view_set_initial_focus (view);

  view_center (view, NULL);
  view_update_scale (view);

  view_update_output (view, NULL);

  wlr_foreign_toplevel_handle_v1_set_fullscreen (priv->toplevel_handle,
                                                 phoc_view_is_fullscreen (view));
  wlr_foreign_toplevel_handle_v1_set_maximized (priv->toplevel_handle,
                                                phoc_view_is_maximized (view));
  wlr_foreign_toplevel_handle_v1_set_title (priv->toplevel_handle,
                                            priv->title ?: "");
  wlr_foreign_toplevel_handle_v1_set_app_id (priv->toplevel_handle,
                                             priv->app_id ?: "");
  if (view->parent)
    toplevel_handle = phoc_view_get_toplevel_handle (view->parent);

  wlr_foreign_toplevel_handle_v1_set_parent (priv->toplevel_handle, toplevel_handle);
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
  wl_list_for_each (output, &view->desktop->outputs, link)
    phoc_output_damage_from_view (output, view, true);
}


void
view_update_position (PhocView *view, int x, int y)
{
  if (view->box.x == x && view->box.y == y)
    return;

  struct wlr_box before;
  phoc_view_get_box (view, &before);
  phoc_view_damage_whole (view);
  view->box.x = x;
  view->box.y = y;
  view_update_output (view, &before);
  phoc_view_damage_whole (view);
}

void
view_update_size (PhocView *view, int width, int height)
{
  struct wlr_box before;

  if (view->box.width == width && view->box.height == height)
    return;

  phoc_view_get_box (view, &before);
  phoc_view_damage_whole (view);
  view->box.width = width;
  view->box.height = height;
  if (view->pending_centering ||
      (phoc_view_is_floating (view) && phoc_desktop_get_auto_maximize (view->desktop))) {
    view_center (view, NULL);
    view->pending_centering = false;
  }
  view_update_scale (view);
  view_update_output (view, &before);
  phoc_view_damage_whole (view);
}


void
view_set_title (PhocView *view, const char *title)
{
  PhocViewPrivate *priv = phoc_view_get_instance_private (view);

  g_free (priv->title);
  priv->title = g_strdup (title);

  if (priv->toplevel_handle)
    wlr_foreign_toplevel_handle_v1_set_title (priv->toplevel_handle, title ?: "");
}

void
view_set_parent (PhocView *view, PhocView *parent)
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
    wlr_foreign_toplevel_handle_v1_set_parent (priv->toplevel_handle, toplevel_handle);
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
    phoc_view_maximize (self, NULL);
  else
    phoc_view_restore (self);
}

static void
handle_toplevel_handle_request_activate (struct wl_listener *listener, void *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocViewPrivate *priv = wl_container_of (listener, priv, toplevel_handle_request_activate);
  PhocView *self = PHOC_VIEW_SELF (priv);
  struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    if (event->seat == seat->seat)
      phoc_seat_set_focus_view (seat, self);
  }
}

static void
handle_toplevel_handle_request_fullscreen (struct wl_listener *listener, void *data)
{
  PhocViewPrivate *priv = wl_container_of (listener, priv, toplevel_handle_request_fullscreen);
  PhocView *self = PHOC_VIEW_SELF (priv);
  struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
  PhocOutput *output = event->output ? PHOC_OUTPUT (event->output->data) : NULL;

  phoc_view_set_fullscreen (self, event->fullscreen, output);
}

static void
handle_toplevel_handle_request_close (struct wl_listener *listener, void *data)
{
  PhocViewPrivate *priv = wl_container_of (listener, priv, toplevel_handle_request_close);
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
    wlr_foreign_toplevel_handle_v1_create (view->desktop->foreign_toplevel_manager_v1);
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
phoc_view_set_alpha (PhocView *self, float alpha)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (G_APPROX_VALUE (priv->alpha, alpha, FLT_EPSILON))
    return;

  priv->alpha = alpha;
  phoc_view_damage_whole (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALPHA]);
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
  case PROP_ALPHA:
    phoc_view_set_alpha (self, g_value_get_float (value));
    break;
  case PROP_DECORATED:
    phoc_view_set_decorated (self, g_value_get_boolean (value));
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
  case PROP_ALPHA:
    g_value_set_float (value, phoc_view_get_alpha (self));
    break;
  case PROP_DECORATED:
    g_value_set_boolean (value, phoc_view_is_decorated (self));
    break;
  case PROP_STATE:
    g_value_set_enum (value, priv->state);
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

  /* Unlink from our parent */
  if (self->parent) {
    wl_list_remove (&self->parent_link);
    wl_list_init (&self->parent_link);
  }

  /* Unlink our children */
  PhocView *child, *tmp;
  wl_list_for_each_safe (child, tmp, &self->stack, parent_link) {
    wl_list_remove (&child->parent_link);
    wl_list_init (&child->parent_link);
    child->parent = self->parent;
    if (child->parent)
      wl_list_insert (&child->parent->stack, &child->parent_link);
  }

  if (self->wlr_surface)
    phoc_view_unmap (self);

  // Can happen if fullscreened while unmapped, and hasn't been mapped
  if (phoc_view_is_fullscreen (self))
    priv->fullscreen_output->fullscreen_view = NULL;

  g_clear_slist (&priv->blings, g_object_unref);
  g_clear_pointer (&priv->title, g_free);
  g_clear_pointer (&priv->app_id, g_free);
  g_clear_pointer (&priv->activation_token, g_free);
  g_clear_object (&priv->deco);
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

G_NORETURN
static void
phoc_view_resize_default (PhocView *self, uint32_t width, uint32_t height)
{
  g_assert_not_reached ();
}

G_NORETURN
static void
phoc_view_move_resize_default (PhocView *self, double x, double y, uint32_t width, uint32_t height)
{
  g_assert_not_reached ();
}

G_NORETURN
static bool
phoc_view_want_automaximize_default (PhocView *self)
{
  g_assert_not_reached ();
}

G_NORETURN
static void
phoc_view_set_active_default (PhocView *self, bool active)
{
  g_assert_not_reached ();
}

G_NORETURN
static void
phoc_view_set_fullscreen_default (PhocView *self, bool fullscreen)
{
  g_assert_not_reached ();
}

G_NORETURN
static void
phoc_view_set_maximized_default (PhocView *self, bool maximized)
{
  g_assert_not_reached ();
}

G_NORETURN
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
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhocView:is-mapped:
   *
   * Whether the view is currently mapped
   */
  props[PROP_IS_MAPPED] =
    g_param_spec_boolean ("is-mapped", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhocView:alpha:
   *
   * The view's transparency
   */
  props[PROP_ALPHA] =
    g_param_spec_float ("alpha", "", "",
                        0.0, 1.0, 1.0,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhocView:decorated:
   *
   * Whether the view should have server side window decorations drawn.
   */
  props[PROP_DECORATED] =
    g_param_spec_boolean ("decorated", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhocView:state:
   *
   * The window is maximized, tiled or floating.
   */
  props[PROP_STATE] =
    g_param_spec_enum ("state", "", "",
                       PHOC_TYPE_VIEW_STATE,
                       PHOC_VIEW_STATE_FLOATING,
                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhocView::surface-destroy:
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
  wl_list_init (&self->stack);

  self->desktop = phoc_server_get_desktop (phoc_server_get_default ());

  g_signal_connect (self, "notify::decorated", G_CALLBACK (toggle_decoration), NULL);
  g_signal_connect (self, "notify::state", G_CALLBACK (toggle_decoration), NULL);
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
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());

  for (GList *l = phoc_desktop_get_views (desktop)->head; l; l = l->next) {
    PhocView *view = PHOC_VIEW (l->data);

    if (view->wlr_surface == wlr_surface)
      return view;
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
 * phoc_view_get_output:
 * @view: The view to get the output for
 *
 * If a view spans multiple output it returns the output that the
 * center of the view is on.
 *
 * Returns: (transfer none)(nullable): The output the view is on
 */
PhocOutput *
phoc_view_get_output (PhocView *view)
{
  struct wlr_output *wlr_output;
  struct wlr_box view_box;
  double output_x, output_y;

  phoc_view_get_box (view, &view_box);

  wlr_output_layout_closest_point (view->desktop->layout, NULL,
                                   view->box.x + (double)view_box.width / 2,
                                   view->box.y + (double)view_box.height / 2,
                                   &output_x, &output_y);
  wlr_output = wlr_output_layout_output_at (view->desktop->layout, output_x, output_y);

  if (wlr_output == NULL)
    return NULL;

  return PHOC_OUTPUT (wlr_output->data);
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
 * Returns the `scale-to-fit` if active for this view.
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
phoc_view_set_activation_token (PhocView *self, const char *token, int type)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (g_strcmp0 (priv->activation_token, token) == 0)
    return;

  g_free (priv->activation_token);
  priv->activation_token = g_strdup (token);
  priv->activation_token_type = type;
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
 * phoc_view_flush_activation_token:
 * @self: The view
 *
 * Notifies that the compositor handled processing the activation token
 * and clears it.
 */
void
phoc_view_flush_activation_token (PhocView *self)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  g_return_if_fail (priv->activation_token);

  phoc_phosh_private_notify_startup_id (phoc_desktop_get_phosh_private (desktop),
                                        priv->activation_token,
                                        priv->activation_token_type);
  phoc_view_set_activation_token (self, NULL, -1);
}

/**
 * phoc_view_get_alpha:
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
 * phoc_view_get_scale:
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
 * phoc_view_set_decorated:
 * @self: The view
 * @decorated: Whether the compositor should draw window decorations
 *
 * Sets whether the compositor should draw server side decorations for
 * this window.
 */
void
phoc_view_set_decorated (PhocView *self, gboolean decorated)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  if (decorated == priv->decorated)
    return;

  priv->decorated = decorated;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DECORATED]);
}

/**
 * phoc_view_is_decorated:
 *
 * Gets whether the view should be decorated server side.
 *
 * Return: `TRUE` if the view should be decorated.
 */
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

  PHOC_VIEW_GET_CLASS (self)->move (self, x, y);
}


void
phoc_view_close (PhocView *self)
{
  PHOC_VIEW_GET_CLASS (self)->close (self);
}

struct wlr_surface *
phoc_view_get_wlr_surface_at (PhocView *self, double sx, double sy, double *sub_x, double *sub_y)
{
  g_assert (PHOC_IS_VIEW (self));

  return PHOC_VIEW_GET_CLASS (self)->get_wlr_surface_at (self, sx, sy, sub_x, sub_y);
}

/**
 * phoc_view_want_auto_maximize:
 * @self: The view
 *
 * Check if a view needs to be auto-maximized. In phoc's auto-maximize
 * mode only toplevels should be maximized.
 *
 * Returns: `true` if the view wants to be auto maximized
 */
bool
phoc_view_want_auto_maximize (PhocView *view)
{
  g_assert (PHOC_IS_VIEW (view));

  if (!view->desktop->maximize)
    return false;

  return PHOC_VIEW_GET_CLASS (view)->want_auto_maximize (view);
}

/**
 * phoc_view_get_app_id:
 * @self: The view
 *
 * Get the view's app_id (if any)
 *
 * Returns:(nullable): The app_id
 */
const char *
phoc_view_get_app_id (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->app_id;
}


pid_t
phoc_view_get_pid (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->pid;
}

/**
 * phoc_view_add_bling:
 * @self: The view
 * @bling: The bling to add
 *
 * By adding a [type@Bling] to a view you ensure that it gets rendered
 * just before the view if both the view and the bling are mapped.
 *
 * Thew view will take a reference on the [type@Bling] that will be
 * dropped when the bling is removed or the view is destroyed.
 */
void
phoc_view_add_bling (PhocView *self, PhocBling *bling)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  g_assert (PHOC_IS_BLING (bling));
  priv = phoc_view_get_instance_private (self);

  priv->blings = g_slist_prepend (priv->blings, g_object_ref (bling));
}

/**
 * phoc_view_remove_bling:
 * @self: The view
 * @bling: The bling to remove
 *
 * Removes the given bling from the view.
 */
void
phoc_view_remove_bling (PhocView *self, PhocBling *bling)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  g_assert (PHOC_IS_BLING (bling));
  priv = phoc_view_get_instance_private (self);

  g_return_if_fail (g_slist_find (priv->blings, bling));

  priv->blings = g_slist_remove (priv->blings, bling);
  g_object_unref (bling);
}

/**
 * phoc_view_get_blings:
 * @self: The view
 *
 * Gets the view's current list of blings.
 *
 * Returns: (transfer none)(element-type PhocBling): A list
 */
GSList *
phoc_view_get_blings (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->blings;
}

/**
 * phoc_view_arrange:
 * @self: a view
 * @output:(nullable): the output to arrange the view on
 * @center: Whether to center the view as fallback
 *
 * Arrange a view based on it's current state (floating, tiled or
 * maximized).  If the view is neither tiled nor maximized and
 * `center` is `FALSE` this operation is a noop.
 */
void
phoc_view_arrange (PhocView *self, PhocOutput *output, gboolean center)
{
  g_assert (PHOC_IS_VIEW (self));
  g_assert (output == NULL || PHOC_IS_OUTPUT (output));

  if (phoc_view_is_maximized (self))
    view_arrange_maximized (self, output);
  else if (phoc_view_is_tiled (self))
    view_arrange_tiled (self, output);
  else if (center)
    view_center (self, output);
}


void
phoc_view_add_child (PhocView *self, PhocViewChild *child)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  g_assert (PHOC_IS_VIEW_CHILD (child));

  priv = phoc_view_get_instance_private (child->view);
  wl_list_insert (&priv->child_surfaces, &child->link);
}

/**
 * phoc_view_set_always_on_top:
 * @self: a view
 * @on_top: Whether the view should be rendered on top of other views
 *
 * Specifies whether the view should be rendered on top of other views.
 */
void
phoc_view_set_always_on_top (PhocView *self, gboolean on_top)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  priv->always_on_top = on_top;
}

/**
 * phoc_view_is_always_on_top:
 * @self: a view
 *
 * Whether a view is always rendered on top of all other views
 *
 * Returns: %TRUE if the view is marked as always-on-top
 */
bool
phoc_view_is_always_on_top (PhocView *self)
{
  PhocViewPrivate *priv;

  g_assert (PHOC_IS_VIEW (self));
  priv = phoc_view_get_instance_private (self);

  return priv->always_on_top;
}
