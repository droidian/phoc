#define G_LOG_DOMAIN "phoc-desktop"

#include "phoc-config.h"

#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "cursor.h"
#include "device-state.h"
#include "idle-inhibit.h"
#include "layer-shell.h"
#include "output.h"
#include "seat.h"
#include "server.h"
#include "color-rect.h"
#include "timed-animation.h"
#include "utils.h"
#include "view.h"
#include "virtual.h"
#include "xdg-activation-v1.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "gesture-swipe.h"
#include "layer-shell-effects.h"
#include "xdg-surface.h"
#include "xdg-toplevel-decoration.h"
#include "xwayland-surface.h"

/* Maximum protocol versions we support */
#define PHOC_FRACTIONAL_SCALE_VERSION 1
#define PHOC_XDG_SHELL_VERSION 5
#define PHOC_LAYER_SHELL_VERSION 3

#define PHOC_ANIM_ALWAYS_ON_TOP_DURATION  300
#define PHOC_ANIM_ALWAYS_ON_TOP_COLOR_ON  (PhocColor){0.5f, 0.0f, 0.3f, 0.5f}
#define PHOC_ANIM_ALWAYS_ON_TOP_COLOR_OFF (PhocColor){0.3f, 0.5f, 0.3f, 0.5f}
#define PHOC_ANIM_ALWAYS_ON_TOP_WIDTH     10

/**
 * PhocDesktop:
 *
 * Desktop singleton
 */

enum {
  PROP_0,
  PROP_SCALE_TO_FIT,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


typedef struct _PhocDesktopPrivate {
  GQueue                *views;

  PhocIdleInhibit       *idle_inhibit;

  gboolean               enable_animations;

  GSettings             *settings;
  GSettings             *interface_settings;

  /* Protocols from wlroots */
  struct wlr_data_control_manager_v1 *data_control_manager_v1;
  struct wlr_idle_notifier_v1 *idle_notifier_v1;
  struct wlr_screencopy_manager_v1 *screencopy_manager_v1;
  struct wl_listener gamma_control_set_gamma;

  /* Protocols without upstreamable implementations */
  PhocPhoshPrivate      *phosh;
  PhocGtkShell          *gtk_shell;

  /* Protocols that should go upstream */
  PhocLayerShellEffects *layer_shell_effects;
  PhocDeviceState       *device_state;
} PhocDesktopPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhocDesktop, phoc_desktop, G_TYPE_OBJECT);


static void
phoc_desktop_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  PhocDesktop *self = PHOC_DESKTOP (object);

  switch (property_id) {
  case PROP_SCALE_TO_FIT:
    phoc_desktop_set_scale_to_fit (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_desktop_get_property (GObject    *object,
                           guint       property_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  PhocDesktop *self = PHOC_DESKTOP (object);

  switch (property_id) {
  case PROP_SCALE_TO_FIT:
    g_value_set_boolean (value, phoc_desktop_get_scale_to_fit (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static bool
view_at (PhocView *view, double lx, double ly, struct wlr_surface **surface, double *sx, double *sy)
{
  double _sx, _sy;
  struct wlr_surface *_surface;

  if (!phoc_view_is_mapped (view))
    return false;

  double view_sx = lx / phoc_view_get_scale (view) - view->box.x;
  double view_sy = ly / phoc_view_get_scale (view) - view->box.y;

  _surface = phoc_view_get_wlr_surface_at (view, view_sx, view_sy, &_sx, &_sy);
  if (_surface != NULL) {
    if (sx)
      *sx = _sx;
    if (sy)
      *sy = _sy;
    *surface = _surface;
    return true;
  }

  if (phoc_view_get_deco_part (view, view_sx, view_sy)) {
    if (sx)
      *sx = view_sx;
    if (sy)
      *sy = view_sy;
    *surface = NULL;
    return true;
  }

  return false;
}

static PhocView *
desktop_view_at (PhocDesktop         *self,
                 double               lx,
                 double               ly,
                 struct wlr_surface **surface,
                 double              *sx,
                 double              *sy)
{
  for (GList *l = phoc_desktop_get_views (self)->head; l; l = l->next) {
    PhocView *view = PHOC_VIEW (l->data);

    if (phoc_desktop_view_is_visible (self, view) && view_at (view, lx, ly, surface, sx, sy))
      return view;
  }
  return NULL;
}

static struct wlr_surface *
layer_surface_at (PhocOutput                     *output,
                  enum zwlr_layer_shell_v1_layer  layer,
                  double                          ox,
                  double                          oy,
                  double                         *sx,
                  double                         *sy)
{
  GQueue *layer_surfaces = phoc_output_get_layer_surfaces_for_layer (output, layer);

  for (GList *l = layer_surfaces->tail; l; l = l->prev) {
    PhocLayerSurface *layer_surface = PHOC_LAYER_SURFACE (l->data);
    struct wlr_surface *sub;

    if (!phoc_layer_surface_get_mapped (layer_surface))
      continue;

    double _sx = ox - layer_surface->geo.x;
    double _sy = oy - layer_surface->geo.y;

    sub = wlr_layer_surface_v1_surface_at (layer_surface->layer_surface, _sx, _sy, sx, sy);
    if (sub)
      return sub;
  }

  return NULL;
}

/**
 * phoc_desktop_wlr_surface_at:
 * @desktop: The `PhocDesktop` to look the surface up for
 * @lx: X coordinate the surface to look up at in layout coordinates
 * @ly: Y coordinate the surface to look up at in layout coordinates
 * @sx: (out) (not nullable): Surface-local x coordinate
 * @sy: (out) (not nullable): Surface-local y coordinate
 * @view: (out) (optional): The corresponding [class@Phoc.View]
 *
 * Looks up the surface at `lx,ly` and returns the topmost surface at
 * that position (if any) and the surface-local coordinates of `sx,sy`
 * on that surface.
 *
 * Returns: (nullable): The `struct wlr_surface`
 */
struct wlr_surface *
phoc_desktop_wlr_surface_at (PhocDesktop *desktop,
                             double       lx,
                             double       ly,
                             double      *sx,
                             double      *sy,
                             PhocView   **view)
{
  struct wlr_surface *surface = NULL;
  PhocOutput *output = phoc_desktop_layout_get_output (desktop, lx, ly);
  double ox = lx, oy = ly;
  if (view)
    *view = NULL;

  if (output)
    wlr_output_layout_output_coords (desktop->layout, output->wlr_output, &ox, &oy);

  /* Layers above regular views */
  if (output) {
    surface = layer_surface_at (output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ox, oy, sx, sy);
    if (surface)
      return surface;

    if (output->fullscreen_view) {
      if (phoc_output_has_shell_revealed (output)) {
        surface = layer_surface_at (output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, ox, oy, sx, sy);
        if (surface)
          return surface;
      }

      if (view_at (output->fullscreen_view, lx, ly, &surface, sx, sy)) {
        if (view)
          *view = output->fullscreen_view;
        return surface;
      } else {
        return NULL;
      }
    }

    surface = layer_surface_at (output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, ox, oy, sx, sy);
    if (surface)
      return surface;
  }

  PhocView *_view = desktop_view_at (desktop, lx, ly, &surface, sx, sy);
  if (_view) {
    if (view)
      *view = _view;

    return surface;
  }

  /* Layers below regular views */
  if (output) {
    surface = layer_surface_at (output, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ox, oy, sx, sy);
    if (surface)
      return surface;

    surface = layer_surface_at (output, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, ox, oy, sx, sy);
    if (surface)
      return surface;
  }

  return NULL;
}

gboolean
phoc_desktop_view_is_visible (PhocDesktop *self, PhocView *view)
{
  PhocDesktopPrivate *priv;
  PhocView *top_view;

  g_assert (PHOC_IS_DESKTOP (self));
  g_assert (PHOC_IS_VIEW (view));

  priv = phoc_desktop_get_instance_private (self);

  if (!phoc_view_is_mapped (view)) {
    return false;
  }

  g_assert_true (priv->views->head);

  if (wl_list_length (&self->outputs) != 1) {
    // current heuristics work well only for single output
    return true;
  }

  if (!self->maximize) {
    return true;
  }

  top_view = phoc_desktop_get_view_by_index (self, 0);
#ifdef PHOC_XWAYLAND
  // XWayland parent relations can be complicated and aren't described by PhocView
  // relationships very well at the moment, so just make all XWayland windows visible
  // when some XWayland window is active for now
  if (PHOC_IS_XWAYLAND_SURFACE (view) && PHOC_IS_XWAYLAND_SURFACE (top_view)) {
    return true;
  }
#endif

  PhocView *v = top_view;
  while (v) {
    if (v == view) {
      return true;
    }
    if (phoc_view_is_maximized (v)) {
      return false;
    }
    v = v->parent;
  }

  return false;
}

static void
handle_layout_change (struct wl_listener *listener, void *data)
{
  PhocDesktop *self;
  PhocDesktopPrivate *priv;
  struct wlr_output *center_output;
  struct wlr_box center_output_box;
  double center_x, center_y;
  PhocOutput *output;

  self = wl_container_of (listener, self, layout_change);
  center_output = wlr_output_layout_get_center_output (self->layout);
  if (center_output == NULL)
    return;

  priv = phoc_desktop_get_instance_private (self);
  wlr_output_layout_get_box (self->layout, center_output, &center_output_box);
  center_x = center_output_box.x + center_output_box.width / 2;
  center_y = center_output_box.y + center_output_box.height / 2;

  /* Make sure all views are on an existing output */
  for (GList *l = priv->views->head; l; l = l->next) {
    PhocView *view = PHOC_VIEW (l->data);
    struct wlr_box box;

    phoc_view_get_box (view, &box);
    if (wlr_output_layout_intersects (self->layout, NULL, &box))
      continue;

    phoc_view_move (view, center_x - box.width / 2, center_y - box.height / 2);
  }

  /* Damage all outputs since the move above damaged old layout space */
  wl_list_for_each(output, &self->outputs, link)
    phoc_output_damage_whole(output);
}


static void
input_inhibit_activate (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = wl_container_of(listener, desktop, input_inhibit_activate);
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    phoc_seat_set_exclusive_client (seat, desktop->input_inhibit->active_client);
  }
}


static void
input_inhibit_deactivate (struct wl_listener *listener, void *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    phoc_seat_set_exclusive_client (seat, NULL);
  }
}


static void
handle_constraint_destroy (struct wl_listener *listener, void *data)
{
  PhocPointerConstraint *constraint = wl_container_of (listener, constraint, destroy);
  struct wlr_pointer_constraint_v1 *wlr_constraint = data;
  PhocSeat *seat = PHOC_SEAT (wlr_constraint->seat->data);
  PhocCursor *cursor = phoc_seat_get_cursor (seat);

  wl_list_remove (&constraint->destroy.link);

  if (cursor->active_constraint == wlr_constraint) {
    wl_list_remove (&cursor->constraint_commit.link);
    wl_list_init (&cursor->constraint_commit.link);
    cursor->active_constraint = NULL;

    if (wlr_constraint->current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT &&
        cursor->pointer_view) {
      PhocView *view = cursor->pointer_view->view;
      double lx = view->box.x + wlr_constraint->current.cursor_hint.x;
      double ly = view->box.y + wlr_constraint->current.cursor_hint.y;

      wlr_cursor_warp (cursor->cursor, NULL, lx, ly);
    }
  }

  g_free (constraint);
}


static void
handle_pointer_constraint (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  struct wlr_pointer_constraint_v1 *wlr_constraint = data;
  PhocSeat *seat = PHOC_SEAT (wlr_constraint->seat->data);
  PhocCursor *cursor = phoc_seat_get_cursor (seat);
  PhocPointerConstraint *constraint;
  struct wlr_surface *surface;
  double sx, sy;

  constraint = g_new0 (PhocPointerConstraint, 1);
  constraint->destroy.notify = handle_constraint_destroy;
  wl_signal_add (&wlr_constraint->events.destroy, &constraint->destroy);

  surface = phoc_desktop_wlr_surface_at (desktop,
                                         cursor->cursor->x,
                                         cursor->cursor->y,
                                         &sx, &sy,
                                         NULL);

  if (surface == wlr_constraint->surface) {
    g_assert (!cursor->active_constraint);
    phoc_cursor_constrain (cursor, wlr_constraint, sx, sy);
  }
}


static void
auto_maximize_changed_cb (PhocDesktop *self,
                          const gchar *key,
                          GSettings   *settings)
{
  gboolean max = g_settings_get_boolean (settings, key);

  g_return_if_fail (PHOC_IS_DESKTOP (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  phoc_desktop_set_auto_maximize (self, max);
}


static void
on_enable_animations_changed (PhocDesktop *self,
                              const gchar *key,
                              GSettings   *settings)
{
  PhocDesktopPrivate *priv;

  g_return_if_fail (PHOC_IS_DESKTOP (self));
  g_return_if_fail (G_IS_SETTINGS (settings));
  priv = phoc_desktop_get_instance_private (self);

  priv->enable_animations = g_settings_get_boolean (settings, key);
}



#ifdef PHOC_XWAYLAND
static const char *atom_map[XWAYLAND_ATOM_LAST] = {
        "_NET_WM_WINDOW_TYPE_NORMAL",
        "_NET_WM_WINDOW_TYPE_DIALOG"
};

static void
handle_xwayland_ready (struct wl_listener *listener,
                       void               *data)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocDesktop *desktop = wl_container_of (listener, desktop, xwayland_ready);
  xcb_connection_t *xcb_conn = xcb_connect (NULL, NULL);

  int err = xcb_connection_has_error (xcb_conn);
  if (err) {
    g_warning ("XCB connect failed: %d", err);
    return;
  }

  xcb_intern_atom_cookie_t cookies[XWAYLAND_ATOM_LAST];

  for (size_t i = 0; i < XWAYLAND_ATOM_LAST; i++)
    cookies[i] = xcb_intern_atom (xcb_conn, 0, strlen (atom_map[i]), atom_map[i]);

  for (size_t i = 0; i < XWAYLAND_ATOM_LAST; i++) {
    xcb_generic_error_t *error = NULL;
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply (xcb_conn, cookies[i], &error);

    if (error) {
      g_warning ("could not resolve atom %s, X11 error code %d",
                 atom_map[i], error->error_code);
      free (error);
    }

    if (reply)
      desktop->xwayland_atoms[i] = reply->atom;

    free (reply);
  }

  xcb_disconnect (xcb_conn);

#ifdef PHOC_XWAYLAND
  if (desktop->xwayland != NULL) {
    PhocSeat *xwayland_seat = phoc_input_get_seat (input, PHOC_CONFIG_DEFAULT_SEAT_NAME);
    wlr_xwayland_set_seat (desktop->xwayland, xwayland_seat->seat);
  }
#endif

}


static void
handle_xwayland_remove_startup_id (struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop = wl_container_of (listener, desktop, xwayland_remove_startup_id);
  struct wlr_xwayland_remove_startup_info_event *ev = data;

  g_assert (PHOC_IS_DESKTOP (desktop));
  g_assert (ev->id);

  phoc_phosh_private_notify_startup_id (phoc_desktop_get_phosh_private (desktop),
                                        ev->id,
                                        PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_X11);
}


static void
handle_xwayland_surface (struct wl_listener *listener, void *data)
{
  struct wlr_xwayland_surface *surface = data;
  g_debug ("new xwayland surface: title=%s, class=%s, instance=%s",
           surface->title, surface->class, surface->instance);
  wlr_xwayland_surface_ping(surface);

  /* Ref is dropped on surface destroy */
  phoc_xwayland_surface_new (surface);
}

#endif /* PHOC_XWAYLAND */

static void
on_output_destroyed (PhocDesktop *self, PhocOutput *destroyed_output)
{
  PhocOutput *output;
  char *input_name;
  GHashTableIter iter;

  g_assert (PHOC_IS_DESKTOP (self));
  g_assert (PHOC_IS_OUTPUT (destroyed_output));

  wlr_output_layout_remove (self->layout, phoc_output_get_wlr_output (destroyed_output));

  g_hash_table_iter_init (&iter, self->input_output_map);
  while (g_hash_table_iter_next (&iter, (gpointer) &input_name,
                                 (gpointer) &output)) {
    if (destroyed_output == output) {
      g_debug ("Removing mapping for input device '%s' to output '%s'",
               input_name, output->wlr_output->name);
      g_hash_table_remove (self->input_output_map, input_name);
      break;
    }
  }
  g_object_unref (destroyed_output);
}

static void
handle_new_output (struct wl_listener *listener, void *data)
{
  g_autoptr (GError) error = NULL;
  PhocDesktop *self = wl_container_of (listener, self, new_output);
  PhocOutput *output = phoc_output_new (self, (struct wlr_output *)data, &error);

  if (output == NULL) {
    g_critical ("Failed to init new output: %s", error->message);
    return;
  }

  g_signal_connect_swapped (output, "output-destroyed",
                            G_CALLBACK (on_output_destroyed),
                            self);
}


static void
phoc_desktop_setup_xwayland (PhocDesktop *self)
{
#ifdef PHOC_XWAYLAND
  const char *cursor_default = PHOC_XCURSOR_DEFAULT;
  PhocServer *server = phoc_server_get_default ();
  PhocConfig *config = phoc_server_get_config (server);

  self->xcursor_manager = wlr_xcursor_manager_create (NULL, PHOC_XCURSOR_SIZE);
  g_return_if_fail (self->xcursor_manager);

  if (config->xwayland) {
    struct wl_display *wl_display = phoc_server_get_wl_display (server);
    struct wlr_compositor *wlr_compositor = phoc_server_get_compositor (server);

    self->xwayland = wlr_xwayland_create (wl_display, wlr_compositor, config->xwayland_lazy);
    if (!self->xwayland) {
      g_critical ("Failed to initialize Xwayland");
      g_unsetenv ("DISPLAY");
      return;
    }

    wl_signal_add (&self->xwayland->events.new_surface, &self->xwayland_surface);
    self->xwayland_surface.notify = handle_xwayland_surface;

    wl_signal_add (&self->xwayland->events.ready, &self->xwayland_ready);
    self->xwayland_ready.notify = handle_xwayland_ready;

    wl_signal_add (&self->xwayland->events.remove_startup_info, &self->xwayland_remove_startup_id);
    self->xwayland_remove_startup_id.notify = handle_xwayland_remove_startup_id;

    g_setenv ("DISPLAY", self->xwayland->display_name, true);

    if (!wlr_xcursor_manager_load (self->xcursor_manager, 1))
      g_critical ("Cannot load XWayland XCursor theme");

    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor (self->xcursor_manager,
                                                                   cursor_default,
                                                                   1);
    if (xcursor != NULL) {
      struct wlr_xcursor_image *image = xcursor->images[0];
      wlr_xwayland_set_cursor (self->xwayland, image->buffer,
                               image->width * 4, image->width, image->height, image->hotspot_x,
                               image->hotspot_y);
    }
  }
#endif
}


static void
phoc_desktop_constructed (GObject *object)
{
  PhocDesktop *self = PHOC_DESKTOP (object);
  PhocDesktopPrivate *priv = phoc_desktop_get_instance_private (self);
  PhocServer *server = phoc_server_get_default ();
  struct wl_display *wl_display = phoc_server_get_wl_display (server);
  struct wlr_backend *wlr_backend = phoc_server_get_backend (server);

  G_OBJECT_CLASS (phoc_desktop_parent_class)->constructed (object);

  wl_list_init (&self->outputs);

  self->new_output.notify = handle_new_output;
  wl_signal_add (&wlr_backend->events.new_output, &self->new_output);

  self->layout = wlr_output_layout_create ();
  wlr_xdg_output_manager_v1_create (wl_display, self->layout);
  self->layout_change.notify = handle_layout_change;
  wl_signal_add (&self->layout->events.change, &self->layout_change);

  self->xdg_shell = wlr_xdg_shell_create(wl_display, PHOC_XDG_SHELL_VERSION);
  wl_signal_add(&self->xdg_shell->events.new_surface, &self->xdg_shell_surface);
  self->xdg_shell_surface.notify = phoc_handle_xdg_shell_surface;

  self->layer_shell = wlr_layer_shell_v1_create (wl_display, PHOC_LAYER_SHELL_VERSION);
  wl_signal_add(&self->layer_shell->events.new_surface, &self->layer_shell_surface);
  self->layer_shell_surface.notify = phoc_handle_layer_shell_surface;
  priv->layer_shell_effects = phoc_layer_shell_effects_new ();

  self->tablet_v2 = wlr_tablet_v2_create (wl_display);

  char cursor_size_fmt[16];
  snprintf (cursor_size_fmt, sizeof (cursor_size_fmt), "%d", PHOC_XCURSOR_SIZE);
  g_setenv ("XCURSOR_SIZE", cursor_size_fmt, 1);

  phoc_desktop_setup_xwayland (self);

  self->security_context_manager_v1 = wlr_security_context_manager_v1_create (wl_display);

  self->gamma_control_manager_v1 = wlr_gamma_control_manager_v1_create (wl_display);
  priv->gamma_control_set_gamma.notify = phoc_output_handle_gamma_control_set_gamma;
  wl_signal_add (&self->gamma_control_manager_v1->events.set_gamma, &priv->gamma_control_set_gamma);

  self->export_dmabuf_manager_v1 = wlr_export_dmabuf_manager_v1_create (wl_display);
  self->server_decoration_manager = wlr_server_decoration_manager_create (wl_display);
  wlr_server_decoration_manager_set_default_mode (self->server_decoration_manager,
                                                  WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT);
  self->primary_selection_device_manager =
    wlr_primary_selection_v1_device_manager_create (wl_display);

  self->input_inhibit = wlr_input_inhibit_manager_create (wl_display);
  self->input_inhibit_activate.notify = input_inhibit_activate;
  wl_signal_add (&self->input_inhibit->events.activate, &self->input_inhibit_activate);
  self->input_inhibit_deactivate.notify = input_inhibit_deactivate;
  wl_signal_add (&self->input_inhibit->events.deactivate, &self->input_inhibit_deactivate);

  self->input_method = wlr_input_method_manager_v2_create (wl_display);
  self->text_input = wlr_text_input_manager_v3_create (wl_display);

  priv->idle_notifier_v1 = wlr_idle_notifier_v1_create (wl_display);
  priv->idle_inhibit = phoc_idle_inhibit_create ();

  priv->gtk_shell = phoc_gtk_shell_create (self, wl_display);
  priv->phosh = phoc_phosh_private_new ();

  self->xdg_activation_v1 = wlr_xdg_activation_v1_create (wl_display);
  self->xdg_activation_v1_request_activate.notify = phoc_xdg_activation_v1_handle_request_activate;
  wl_signal_add (&self->xdg_activation_v1->events.request_activate,
                 &self->xdg_activation_v1_request_activate);

  self->virtual_keyboard = wlr_virtual_keyboard_manager_v1_create (wl_display);
  wl_signal_add (&self->virtual_keyboard->events.new_virtual_keyboard,
                 &self->virtual_keyboard_new);
  self->virtual_keyboard_new.notify = phoc_handle_virtual_keyboard;

  self->virtual_pointer = wlr_virtual_pointer_manager_v1_create (wl_display);
  wl_signal_add (&self->virtual_pointer->events.new_virtual_pointer, &self->virtual_pointer_new);
  self->virtual_pointer_new.notify = phoc_handle_virtual_pointer;

  priv->screencopy_manager_v1 = wlr_screencopy_manager_v1_create (wl_display);

  self->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create (wl_display);
  wl_signal_add (&self->xdg_decoration_manager->events.new_toplevel_decoration,
                 &self->xdg_toplevel_decoration);

  self->xdg_toplevel_decoration.notify = phoc_handle_xdg_toplevel_decoration;
  wlr_viewporter_create (wl_display);
  wlr_single_pixel_buffer_manager_v1_create (wl_display);
  wlr_fractional_scale_manager_v1_create (wl_display, PHOC_FRACTIONAL_SCALE_VERSION);

  struct wlr_xdg_foreign_registry *foreign_registry = wlr_xdg_foreign_registry_create (wl_display);
  wlr_xdg_foreign_v1_create (wl_display, foreign_registry);
  wlr_xdg_foreign_v2_create (wl_display, foreign_registry);

  self->pointer_constraints = wlr_pointer_constraints_v1_create (wl_display);
  self->pointer_constraint.notify = handle_pointer_constraint;
  wl_signal_add (&self->pointer_constraints->events.new_constraint, &self->pointer_constraint);

  self->presentation = wlr_presentation_create (wl_display, wlr_backend);
  self->foreign_toplevel_manager_v1 = wlr_foreign_toplevel_manager_v1_create (wl_display);
  self->relative_pointer_manager = wlr_relative_pointer_manager_v1_create (wl_display);
  self->pointer_gestures = wlr_pointer_gestures_v1_create (wl_display);

  self->output_manager_v1 = wlr_output_manager_v1_create (wl_display);
  self->output_manager_apply.notify = phoc_handle_output_manager_apply;
  wl_signal_add (&self->output_manager_v1->events.apply, &self->output_manager_apply);
  self->output_manager_test.notify = phoc_handle_output_manager_test;
  wl_signal_add (&self->output_manager_v1->events.test, &self->output_manager_test);

  self->output_power_manager_v1 = wlr_output_power_manager_v1_create (wl_display);
  self->output_power_manager_set_mode.notify = phoc_output_handle_output_power_manager_set_mode;
  wl_signal_add (&self->output_power_manager_v1->events.set_mode,
                 &self->output_power_manager_set_mode);

  priv->data_control_manager_v1 = wlr_data_control_manager_v1_create (wl_display);

  /* sm.puri.phoc settings */
  priv->settings = g_settings_new ("sm.puri.phoc");
  g_signal_connect_swapped (priv->settings, "changed::auto-maximize",
                            G_CALLBACK (auto_maximize_changed_cb), self);
  auto_maximize_changed_cb (self, "auto-maximize", priv->settings);
  g_settings_bind (priv->settings, "scale-to-fit", self, "scale-to-fit", G_SETTINGS_BIND_DEFAULT);

  /* org.gnome.desktop.interface settings */
  priv->interface_settings = g_settings_new ("org.gnome.desktop.interface");
  if (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_DISABLE_ANIMATIONS)) {
    priv->enable_animations = FALSE;
  } else {
    g_signal_connect_swapped (priv->interface_settings, "changed::enable-animations",
                              G_CALLBACK (on_enable_animations_changed), self);
    on_enable_animations_changed (self, "enable-animations", priv->interface_settings);
  }
}


static void
phoc_desktop_finalize (GObject *object)
{
  PhocDesktop *self = PHOC_DESKTOP (object);
  PhocDesktopPrivate *priv = phoc_desktop_get_instance_private (self);

  g_clear_pointer (&priv->views, g_queue_free);

  /* TODO: currently destroys the backend before the desktop */
  //wl_list_remove (&self->new_output.link);
  wl_list_remove (&self->layout_change.link);
  wl_list_remove (&self->xdg_shell_surface.link);
  wl_list_remove (&self->layer_shell_surface.link);
  wl_list_remove (&self->xdg_toplevel_decoration.link);
  wl_list_remove (&self->input_inhibit_activate.link);
  wl_list_remove (&self->input_inhibit_deactivate.link);
  wl_list_remove (&self->virtual_keyboard_new.link);
  wl_list_remove (&self->virtual_pointer_new.link);
  wl_list_remove (&self->pointer_constraint.link);
  wl_list_remove (&self->output_manager_apply.link);
  wl_list_remove (&self->output_manager_test.link);
  wl_list_remove (&self->output_power_manager_set_mode.link);
  wl_list_remove (&self->xdg_activation_v1_request_activate.link);

#ifdef PHOC_XWAYLAND
  /* Disconnect XWayland listener before shutting it down */
  if (self->xwayland) {
    wl_list_remove (&self->xwayland_surface.link);
    wl_list_remove (&self->xwayland_ready.link);
    wl_list_remove (&self->xwayland_remove_startup_id.link);
  }

  g_clear_pointer (&self->xcursor_manager, wlr_xcursor_manager_destroy);
  // We need to shutdown Xwayland before disconnecting all clients, otherwise
  // wlroots will restart it automatically.
  g_clear_pointer (&self->xwayland, wlr_xwayland_destroy);
#endif

  g_clear_pointer (&priv->idle_inhibit, phoc_idle_inhibit_destroy);
  g_clear_object (&priv->phosh);
  g_clear_pointer (&priv->gtk_shell, phoc_gtk_shell_destroy);
  g_clear_object (&priv->layer_shell_effects);
  g_clear_object (&priv->device_state);
  g_clear_pointer (&self->layout, wlr_output_layout_destroy);

  g_hash_table_remove_all (self->input_output_map);
  g_hash_table_unref (self->input_output_map);

  g_clear_object (&priv->interface_settings);
  g_clear_object (&priv->settings);

  G_OBJECT_CLASS (phoc_desktop_parent_class)->finalize (object);
}


static void
phoc_desktop_class_init (PhocDesktopClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->set_property = phoc_desktop_set_property;
  object_class->get_property = phoc_desktop_get_property;

  object_class->constructed = phoc_desktop_constructed;
  object_class->finalize = phoc_desktop_finalize;

  /**
   * PhocDesktop:scale-to-fit:
   *
   * If %TRUE all surfaces will be scaled down to fit the screen.
   */
  props[PROP_SCALE_TO_FIT] =
    g_param_spec_boolean ("scale-to-fit", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_desktop_init (PhocDesktop *self)
{
  PhocDesktopPrivate *priv;

  priv = phoc_desktop_get_instance_private (self);
  priv->views = g_queue_new ();
  priv->enable_animations = TRUE;

  self->input_output_map = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  NULL);
}


PhocDesktop *
phoc_desktop_new (void)
{
  return g_object_new (PHOC_TYPE_DESKTOP, NULL);
}

/**
 * phoc_desktop_set_auto_maximize:
 *
 * Turn auto maximization of toplevels on (%TRUE) or off (%FALSE)
 */
void
phoc_desktop_set_auto_maximize (PhocDesktop *self, gboolean enable)
{
  PhocDesktopPrivate *priv = phoc_desktop_get_instance_private (self);
  PhocServer *server = phoc_server_get_default();

  if (G_UNLIKELY (phoc_server_check_debug_flags (server, PHOC_SERVER_DEBUG_FLAG_AUTO_MAXIMIZE))) {
    if (enable == FALSE)
      g_info ("Not disabling auto-maximize due to `auto-maximize` debug flag");
    enable = TRUE;
  }

  g_debug ("auto-maximize: %d", enable);
  self->maximize = enable;

  /* Disabling auto-maximize leaves all views in their current position */
  if (!enable) {
    PhocInput *input = phoc_server_get_input (server);

    for (GList *l = priv->views->head; l; l = l->next) {
      PhocView *view = PHOC_VIEW (l->data);

      phoc_view_appear_activated (view, phoc_input_view_has_focus (input, view));
    }
    return;
  }

  for (GList *l = priv->views->head; l; l = l->next) {
    PhocView *view = PHOC_VIEW (l->data);

    phoc_view_auto_maximize (view);
    phoc_view_appear_activated (view, true);
  }
}

gboolean
phoc_desktop_get_auto_maximize (PhocDesktop *self)
{
  return self->maximize;
}

/**
 * phoc_desktop_set_scale_to_fit:
 *
 * Turn auto scaling of all oversized toplevels on (%TRUE) or off (%FALSE)
 */
void
phoc_desktop_set_scale_to_fit (PhocDesktop *self, gboolean enable)
{
  g_return_if_fail (PHOC_IS_DESKTOP (self));

  if (self->scale_to_fit == enable)
    return;

  g_debug ("scale to fit: %d", enable);
  self->scale_to_fit = enable;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SCALE_TO_FIT]);
}

gboolean
phoc_desktop_get_scale_to_fit (PhocDesktop *self)
{
  return self->scale_to_fit;
}

/**
 * phoc_desktop_get_enable_animations:
 * @self: The desktop
 *
 * Checks whether the user wants animations to be enabled.
 *
 * Returns: Whether animations are enabled
 */
gboolean
phoc_desktop_get_enable_animations (PhocDesktop *self)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  return priv->enable_animations;
}

/**
 * phoc_desktop_find_output:
 * @self: The desktop
 * @make: The output's make / vendor
 * @model: The output's model / product
 * @serial: The output's serial number
 *
 * Find an output by make, model and serial
 *
 * Returns: (transfer none) (nullable): The matching output or
 *  %NULL if no output matches.
 */
PhocOutput *
phoc_desktop_find_output (PhocDesktop *self,
                          const char  *make,
                          const char  *model,
                          const char  *serial)
{
  PhocOutput *output;

  g_assert (PHOC_IS_DESKTOP (self));

  wl_list_for_each (output, &self->outputs, link) {
    if (phoc_output_is_match (output, make, model, serial))
      return output;
  }

  return NULL;
}

/**
 * phoc_desktop_find_output_by_name:
 * @self: The desktop
 * @name: The output's name

 * Find an output by it's name.
 *
 * Returns: (transfer none) (nullable): The matching output or
 *  %NULL if no output matches.
 */
PhocOutput *
phoc_desktop_find_output_by_name (PhocDesktop *self, const char *name)
{
  PhocOutput *output;

  g_assert (PHOC_IS_DESKTOP (self));

  if (!name)
    return NULL;

  wl_list_for_each (output, &self->outputs, link) {
    if (g_strcmp0 (phoc_output_get_name (output), name) == 0)
      return output;
  }

  return NULL;
}


/**
 * phoc_desktop_layer_surface_at:
 * @self: The `PhocDesktop` to look the surface up for
 * @lx: X coordinate the layer surface to look up at in layout coordinates
 * @ly: Y coordinate the layer surface to look up at in layout coordinates
 * @sx: (out) (nullable): Surface relative x coordinate
 * @sy: (out) (nullable): Surface relative y coordinate
 *
 * Looks up the surface at `lx,ly` and returns the topmost surface at that position
 * if it is a layersurface, `NULL` otherwise.
 *
 * Returns: (transfer none) (nullable): The `PhocLayerSurface`
 */
PhocLayerSurface *
phoc_desktop_layer_surface_at (PhocDesktop *self, double lx, double ly, double *sx, double *sy)
{
  struct wlr_surface *wlr_surface;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  PhocLayerSurface *layer_surface;
  double sx_, sy_;

  g_assert (PHOC_IS_DESKTOP (self));

  wlr_surface = phoc_desktop_wlr_surface_at (self, lx, ly, &sx_, &sy_, NULL);

  if (!wlr_surface)
    return NULL;

  wlr_layer_surface = wlr_layer_surface_v1_try_from_wlr_surface (wlr_surface);
  if (wlr_layer_surface == NULL)
    return NULL;

  layer_surface = PHOC_LAYER_SURFACE (wlr_layer_surface->data);
  if (sx)
    *sx = sx_;

  if (sy)
    *sy = sy_;

  return layer_surface;
}


/**
 * phoc_desktop_get_builtin_output:
 *
 * Get the built-in output. This assumes there's only one
 * and returns the first.
 *
 * Returns: (transfer none) (nullable): The built-in output.
 *  %NULL if there's no built-in output.
 */
PhocOutput *
phoc_desktop_get_builtin_output (PhocDesktop *self)
{
  PhocOutput *output;

  g_assert (PHOC_IS_DESKTOP (self));

  wl_list_for_each (output, &self->outputs, link) {
    if (phoc_output_is_builtin (output))
      return output;
  }

  return NULL;
}

/**
 * phoc_desktop_layout_get_output:
 * @self: The desktop
 * @lx: The x coordinate in layout coordinates
 * @ly: The y coordinate in layout coordinates
 *
 * Get the output at layout coordinates.
 *
 * Returns: (transfer none) (nullable): The output
 */
PhocOutput *
phoc_desktop_layout_get_output (PhocDesktop *self, double lx, double ly)
{
  struct wlr_output *wlr_output;

  g_assert (PHOC_IS_DESKTOP (self));

  wlr_output = wlr_output_layout_output_at (self->layout, lx, ly);
  if (wlr_output == NULL)
    return NULL;

  return PHOC_OUTPUT (wlr_output->data);
}

/**
 * phoc_desktop_get_draggable_layer_surface:
 * @self: The `PhocDesktop` to look the surface up for
 * @layer_surface: The layer surface to look up
 *
 * Returns a draggable layer surface if `layer_surface` is configured as
 * such. `NULL` otherwise.
 *
 * Returns: (transfer none)(nullable): The `PhocDraggableLayerSurface`
 */
PhocDraggableLayerSurface *
phoc_desktop_get_draggable_layer_surface (PhocDesktop *self, PhocLayerSurface *layer_surface)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  g_assert (layer_surface);

  priv = phoc_desktop_get_instance_private (self);
  return phoc_layer_shell_effects_get_draggable_layer_surface_from_layer_surface (
    priv->layer_shell_effects, layer_surface);
}

/**
 * phoc_desktop_get_layer_surface_stacks:
 * @self: The desktop
 *
 * Get the list of currently known stacks
 *
 * Returns:(transfer none)(element-type PhocStackedLayerSurface): The layer surface stacks
 */
GSList *
phoc_desktop_get_layer_surface_stacks (PhocDesktop *self)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));

  priv = phoc_desktop_get_instance_private (self);
  return phoc_layer_shell_effects_get_layer_surface_stacks (priv->layer_shell_effects);
}

/**
 * phoc_desktop_get_gtk_shell:
 * @self: The `PhocDesktop`
 *
 * Gets a handler of the gtk_shell Wayland protocol implementations
 *
 * Returns: (transfer none): gtk_shell protocol implementation handler
 */
PhocGtkShell *
phoc_desktop_get_gtk_shell (PhocDesktop *self)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));

  priv = phoc_desktop_get_instance_private (self);

  return priv->gtk_shell;
}

/**
 * phoc_desktop_get_phosh_private:
 * @self: The `PhocDesktop`
 *
 * Gets a handler of the phosh-private Wayland protocol implementations
 *
 * Returns: (transfer none): phosh_private protocol implementation handler
 */
PhocPhoshPrivate *
phoc_desktop_get_phosh_private (PhocDesktop *self)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));

  priv = phoc_desktop_get_instance_private (self);

  return priv->phosh;
}

void
phoc_desktop_notify_activity (PhocDesktop *self, PhocSeat *seat)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  wlr_idle_notifier_v1_notify_activity (priv->idle_notifier_v1, seat->seat);
}

gboolean
phoc_desktop_is_privileged_protocol (PhocDesktop *self, const struct wl_global *global)
{
  gboolean is_priv;
  PhocDesktopPrivate *priv;

  g_return_val_if_fail (PHOC_IS_DESKTOP (self), TRUE);
  priv = phoc_desktop_get_instance_private (self);

  is_priv = (
    global == phoc_phosh_private_get_global (priv->phosh) ||
    global == phoc_layer_shell_effects_get_global (priv->layer_shell_effects) ||
    global == priv->data_control_manager_v1->global ||
    global == priv->screencopy_manager_v1->global ||
    global == self->export_dmabuf_manager_v1->global ||
    global == self->foreign_toplevel_manager_v1->global ||
    global == self->gamma_control_manager_v1->global ||
    global == self->input_inhibit->global ||
    global == self->input_method->global ||
    global == self->layer_shell->global ||
    global == self->output_manager_v1->global ||
    global == self->output_power_manager_v1->global ||
    global == self->security_context_manager_v1->global ||
    global == self->virtual_keyboard->global ||
    global == self->virtual_pointer->global
    );

  return is_priv;
}

/**
 * phoc_desktop_get_views:
 * @self: the desktop
 *
 * Get the current views. Don't manipulate the queue directly. This is
 * only meant for reading.
 *
 * Returns:(transfer none): The views.
 */
GQueue *
phoc_desktop_get_views (PhocDesktop *self)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  return priv->views;
}

/**
 * phoc_desktop_move_view_to_top:
 * @self: the desktop
 * @view: a view
 *
 * Move the given view to the front of the view stack meaning that it
 * will be rendered on top of other views (but below move-to-top
 * views if `view` isn't a `move-to-top-view` itself).
 */
void
phoc_desktop_move_view_to_top (PhocDesktop *self, PhocView *view)
{
  GList *view_link;
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  view_link = g_queue_find (priv->views, view);
  g_assert (view_link);

  g_queue_unlink (priv->views, view_link);

  if (G_UNLIKELY (phoc_view_is_always_on_top (view))) {
    g_queue_push_head_link (priv->views, view_link);
  } else {
    GList *l = NULL;

    for (l = phoc_desktop_get_views (self)->head; l; l = l->next) {
      if (!phoc_view_is_always_on_top (PHOC_VIEW (l->data)))
        break;
    }

    g_queue_insert_before_link (priv->views, l, view_link);
  }

  phoc_view_damage_whole (view);
}

/**
 * phoc_desktop_has_views:
 * @self: the desktop
 *
 * Check whether the desktop has any views.
 *
 * Returns: %TRUE if there's at least on view, otherwise %FALSE
 */
gboolean
phoc_desktop_has_views (PhocDesktop *self)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  return !!priv->views->head;
}

/**
 * phoc_desktop_get_view_by_index:
 * @self: the desktop
 * @index: the index to get the view for
 *
 * Gets the view at the given position in the queue. If the view is
 * not part of that desktop %NULL is returned.
 *
 * Returns:(transfer none)(nullable): the looked up view
 */
PhocView *
phoc_desktop_get_view_by_index (PhocDesktop *self, guint index)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  return g_queue_peek_nth (priv->views, index);
}

/**
 * phoc_desktop_insert_view:
 * @self: the desktop
 * @view: the view to insert
 *
 * Insert the view into the queue of views. New views are inserted
 * at the front so they appear on top of other views.
 */
void
phoc_desktop_insert_view (PhocDesktop *self, PhocView *view)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  g_queue_push_head (priv->views, view);
  phoc_desktop_move_view_to_top (self, view);
}

/**
 * phoc_desktop_remove_view:
 * @self: the desktop
 * @view: The view to remove
 *
 * Removes a view from the queue of views.
 *
 * Returns: %TRUE if the view was found, otherwise %FALSE
 */
gboolean
phoc_desktop_remove_view (PhocDesktop *self, PhocView *view)
{
  PhocDesktopPrivate *priv;

  g_assert (PHOC_IS_DESKTOP (self));
  priv = phoc_desktop_get_instance_private (self);

  return g_queue_remove (priv->views, view);
}


static void
on_always_on_top_animation_done (PhocTimedAnimation *anim, PhocColorRect *rect)
{
  PhocView *view;

  g_assert (PHOC_IS_TIMED_ANIMATION (anim));
  g_assert (PHOC_IS_COLOR_RECT (rect));

  view = g_object_get_data (G_OBJECT (rect), "view");
  g_assert (PHOC_IS_VIEW (view));

  phoc_view_remove_bling (view, PHOC_BLING (rect));
}


/**
 * phoc_desktop_set_view_always_on_top:
 * @self: The desktop singleton
 * @view: The view to operate on
 * @on_top: Whether to render this view on top of other views
 *
 * If `on_top` is `TRUE` marks the `view` (and it's children) as being
 * placed on top of other views. If `on_top` if `FALSE` the `view` will
 * be placed normally.
 */
void
phoc_desktop_set_view_always_on_top (PhocDesktop *self, PhocView *view, gboolean on_top)
{
  PhocView *child;

  g_assert (PHOC_IS_DESKTOP (self));
  g_assert (PHOC_IS_VIEW (view));

  if (phoc_desktop_get_enable_animations (self)) {
    g_autoptr (PhocTimedAnimation) fade_anim = NULL;
    g_autoptr (PhocPropertyEaser) easer = NULL;
    g_autoptr (PhocColorRect) rect = NULL;
    PhocColor color;
    struct wlr_box rect_box, geom_box;

    color = on_top ? PHOC_ANIM_ALWAYS_ON_TOP_COLOR_ON : PHOC_ANIM_ALWAYS_ON_TOP_COLOR_OFF;

    /* Grow the rect around the view a bit */
    phoc_view_get_box (view, &rect_box);
    phoc_view_get_geometry (view, &geom_box);
    rect_box.x -= PHOC_ANIM_ALWAYS_ON_TOP_WIDTH - geom_box.x;
    rect_box.y -= PHOC_ANIM_ALWAYS_ON_TOP_WIDTH - geom_box.y;
    rect_box.width += 2 * PHOC_ANIM_ALWAYS_ON_TOP_WIDTH;
    rect_box.height += 2 * PHOC_ANIM_ALWAYS_ON_TOP_WIDTH;
    rect = phoc_color_rect_new ((PhocBox *)&rect_box, &color);

    /* Make sure we end up in the render tree */
    phoc_view_add_bling (view, PHOC_BLING (rect));

    easer = g_object_new (PHOC_TYPE_PROPERTY_EASER,
                          "target", rect,
                          "easing", PHOC_EASING_EASE_OUT_QUAD,
                          NULL);
    phoc_property_easer_set_props (easer, "alpha", 1.0, 0.0, NULL);
    fade_anim = g_object_new (PHOC_TYPE_TIMED_ANIMATION,
                              "animatable", phoc_view_get_output (view),
                              "duration", PHOC_ANIM_ALWAYS_ON_TOP_DURATION,
                              "property-easer", easer,
                              "dispose-on-done", TRUE,
                              NULL);
    phoc_bling_map (PHOC_BLING (rect));
    g_object_set_data (G_OBJECT (rect), "view", view);
    g_signal_connect (fade_anim,
                      "done",
                      G_CALLBACK (on_always_on_top_animation_done),
                      rect);
    phoc_timed_animation_play (fade_anim);
  }

  phoc_view_set_always_on_top (view, on_top);
  phoc_desktop_move_view_to_top (self, view);

  /* Raise children recursively */
  wl_list_for_each_reverse (child, &view->stack, parent_link)
    phoc_desktop_set_view_always_on_top (self, child, on_top);
}
