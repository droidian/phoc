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
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/version.h>
#include "cursor.h"
#include "layers.h"
#include "output.h"
#include "seat.h"
#include "server.h"
#include "utils.h"
#include "view.h"
#include "virtual.h"
#include "xcursor.h"
#include "xdg-activation-v1.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "gesture-swipe.h"
#include "layer-shell-effects.h"


#include "xdg-surface.h"

/**
 * PhocDesktop:
 *
 * Desktop singleton
 */

enum {
  PROP_0,
  PROP_CONFIG,
  PROP_SCALE_TO_FIT,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE(PhocDesktop, phoc_desktop, G_TYPE_OBJECT);


static void
phoc_desktop_set_property (GObject     *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  PhocDesktop *self = PHOC_DESKTOP (object);

  switch (property_id) {
  case PROP_CONFIG:
    self->config = g_value_get_pointer (value);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIG]);
    break;
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
  case PROP_CONFIG:
    g_value_set_pointer (value, self->config);
    break;
  case PROP_SCALE_TO_FIT:
    g_value_set_boolean (value, phoc_desktop_get_scale_to_fit (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static bool view_at(PhocView *view, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	if (!phoc_view_is_mapped (view)) {
		return false;
	}

	double view_sx = lx / view->scale - view->box.x;
	double view_sy = ly / view->scale - view->box.y;

	double _sx, _sy;
	struct wlr_surface *_surface = NULL;
	switch (view->type) {
	case PHOC_XDG_SHELL_VIEW:;
		PhocXdgSurface *xdg_surface = phoc_xdg_surface_from_view(view);
		_surface = wlr_xdg_surface_surface_at(xdg_surface->xdg_surface,
			view_sx, view_sy, &_sx, &_sy);
		break;
#ifdef PHOC_XWAYLAND
	case PHOC_XWAYLAND_VIEW:
		_surface = wlr_surface_surface_at(view->wlr_surface,
			view_sx, view_sy, &_sx, &_sy);
		break;
#endif
	default:
		g_error("Invalid view type %d", view->type);
	}
	if (_surface != NULL) {
		if (sx)
			*sx = _sx;
		if (sy)
			*sy = _sy;
		*surface = _surface;
		return true;
	}

	if (view_get_deco_part(view, view_sx, view_sy)) {
		if (sx)
			*sx = view_sx;
		if (sy)
			*sy = view_sy;
		*surface = NULL;
		return true;
	}

	return false;
}

static PhocView *desktop_view_at(PhocDesktop *desktop,
		double lx, double ly, struct wlr_surface **surface,
		double *sx, double *sy) {
	PhocView *view;
	wl_list_for_each(view, &desktop->views, link) {
		if (phoc_desktop_view_is_visible(desktop, view) && view_at(view, lx, ly, surface, sx, sy)) {
			return view;
		}
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
  PhocLayerSurface *layer_surface;

  wl_list_for_each_reverse(layer_surface, &output->layer_surfaces, link) {
    if (!layer_surface->mapped)
      continue;

    if (layer_surface->layer != layer)
      continue;

    if (layer_surface->layer_surface->current.exclusive_zone <= 0)
      continue;

    double _sx = ox - layer_surface->geo.x;
    double _sy = oy - layer_surface->geo.y;

    struct wlr_surface *sub = wlr_layer_surface_v1_surface_at(
      layer_surface->layer_surface, _sx, _sy, sx, sy);

    if (sub)
      return sub;
  }

  wl_list_for_each(layer_surface, &output->layer_surfaces, link) {
    if (!layer_surface->mapped)
      continue;

    if (layer_surface->layer != layer)
      continue;

    if (layer_surface->layer_surface->current.exclusive_zone > 0)
      continue;

    double _sx = ox - layer_surface->geo.x;
    double _sy = oy - layer_surface->geo.y;

    struct wlr_surface *sub = wlr_layer_surface_v1_surface_at(
      layer_surface->layer_surface, _sx, _sy, sx, sy);

    if (sub)
      return sub;
  }

  return NULL;
}

/**
 * phoc_desktop_surface_at:
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
struct wlr_surface *phoc_desktop_surface_at(PhocDesktop *desktop,
		double lx, double ly, double *sx, double *sy,
		PhocView **view) {
	struct wlr_surface *surface = NULL;
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(desktop->layout, lx, ly);
	PhocOutput *phoc_output = NULL;
	double ox = lx, oy = ly;
	if (view) {
		*view = NULL;
	}

	if (wlr_output) {
		phoc_output = PHOC_OUTPUT (wlr_output->data);
		wlr_output_layout_output_coords(desktop->layout, wlr_output, &ox, &oy);

		if ((surface = layer_surface_at(phoc_output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
						ox, oy, sx, sy))) {
			return surface;
		}

		PhocOutput *output = wlr_output->data;
		if (output != NULL && output->fullscreen_view != NULL) {

			if (phoc_output_has_shell_revealed (output)) {
				if ((surface = layer_surface_at(phoc_output, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
								ox, oy, sx, sy))) {
					return surface;
				}
			}

			if (view_at(output->fullscreen_view, lx, ly, &surface, sx, sy)) {
				if (view) {
					*view = output->fullscreen_view;
				}
				return surface;
			} else {
				return NULL;
			}
		}

		if ((surface = layer_surface_at(phoc_output, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
						ox, oy, sx, sy))) {
			return surface;
		}
	}

	PhocView *_view;
	if ((_view = desktop_view_at(desktop, lx, ly, &surface, sx, sy))) {
		if (view) {
			*view = _view;
		}
		return surface;
	}

	if (wlr_output) {
		if ((surface = layer_surface_at(phoc_output, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
						ox, oy, sx, sy))) {
			return surface;
		}
		if ((surface = layer_surface_at(phoc_output, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
						ox, oy, sx, sy))) {
			return surface;
		}
	}
	return NULL;
}

gboolean
phoc_desktop_view_is_visible (PhocDesktop *desktop, PhocView *view)
{
  if (!phoc_view_is_mapped (view)) {
    return false;
  }

  g_assert_false (wl_list_empty (&desktop->views));

  if (wl_list_length (&desktop->outputs) != 1) {
    // current heuristics work well only for single output
    return true;
  }

  if (!desktop->maximize) {
    return true;
  }

  PhocView *top_view = wl_container_of (desktop->views.next, view, link);

#ifdef PHOC_XWAYLAND
  // XWayland parent relations can be complicated and aren't described by PhocView
  // relationships very well at the moment, so just make all XWayland windows visible
  // when some XWayland window is active for now
  if (view->type == PHOC_XWAYLAND_VIEW && top_view->type == PHOC_XWAYLAND_VIEW) {
    return true;
  }
#endif

  PhocView *v = top_view;
  while (v) {
    if (v == view) {
      return true;
    }
    if (view_is_maximized (v)) {
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
  struct wlr_output *center_output;
  struct wlr_box *center_output_box;
  double center_x, center_y;
  PhocView *view;
  PhocOutput *output;

  self = wl_container_of (listener, self, layout_change);
  center_output = wlr_output_layout_get_center_output (self->layout);
  if (center_output == NULL)
    return;

  center_output_box = wlr_output_layout_get_box (self->layout, center_output);
  center_x = center_output_box->x + center_output_box->width / 2;
  center_y = center_output_box->y + center_output_box->height / 2;

  /* Make sure all views are on an existing output */
  wl_list_for_each (view, &self->views, link) {
    struct wlr_box box;
    view_get_box (view, &box);

    if (wlr_output_layout_intersects (self->layout, NULL, &box))
      continue;
    view_move (view, center_x - box.width / 2, center_y - box.height / 2);
  }

  /* Damage all outputs since the move above damaged old layout space */
  wl_list_for_each(output, &self->outputs, link)
    phoc_output_damage_whole(output);
}

static void input_inhibit_activate(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop = wl_container_of(
			listener, desktop, input_inhibit_activate);
	PhocServer *server = phoc_server_get_default ();

	for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
		PhocSeat *seat = PHOC_SEAT (elem->data);

		g_assert (PHOC_IS_SEAT (seat));
		phoc_seat_set_exclusive_client(seat,
				desktop->input_inhibit->active_client);
	}
}

static void input_inhibit_deactivate(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop = wl_container_of(
			listener, desktop, input_inhibit_deactivate);
	PhocServer *server = phoc_server_get_default ();

	for (GSList *elem = phoc_input_get_seats (server->input); elem; elem = elem->next) {
		PhocSeat *seat = PHOC_SEAT (elem->data);

		g_assert (PHOC_IS_SEAT (seat));
		phoc_seat_set_exclusive_client(seat, NULL);
	}
}

static void handle_constraint_destroy(struct wl_listener *listener,
		void *data) {
	PhocPointerConstraint *constraint =
		wl_container_of(listener, constraint, destroy);
	struct wlr_pointer_constraint_v1 *wlr_constraint = data;
	PhocSeat *seat = wlr_constraint->seat->data;
	PhocCursor *cursor = phoc_seat_get_cursor (seat);

	wl_list_remove(&constraint->destroy.link);

	if (cursor->active_constraint == wlr_constraint) {
		wl_list_remove(&cursor->constraint_commit.link);
		wl_list_init(&cursor->constraint_commit.link);
		cursor->active_constraint = NULL;

		if (wlr_constraint->current.committed &
				WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT &&
				cursor->pointer_view) {
			PhocView *view = cursor->pointer_view->view;
			double lx = view->box.x + wlr_constraint->current.cursor_hint.x;
			double ly = view->box.y + wlr_constraint->current.cursor_hint.y;

			wlr_cursor_warp(cursor->cursor, NULL, lx, ly);
		}
	}

	free(constraint);
}

static void handle_pointer_constraint(struct wl_listener *listener,
		void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct wlr_pointer_constraint_v1 *wlr_constraint = data;
	PhocSeat *seat = wlr_constraint->seat->data;
	PhocCursor *cursor = phoc_seat_get_cursor (seat);

	PhocPointerConstraint *constraint =
		calloc(1, sizeof(PhocPointerConstraint));
	constraint->constraint = wlr_constraint;

	constraint->destroy.notify = handle_constraint_destroy;
	wl_signal_add(&wlr_constraint->events.destroy, &constraint->destroy);

	double sx, sy;
	struct wlr_surface *surface = phoc_desktop_surface_at(
		server->desktop,
		cursor->cursor->x, cursor->cursor->y, &sx, &sy, NULL);

	if (surface == wlr_constraint->surface) {
		assert(!cursor->active_constraint);
		phoc_cursor_constrain(cursor, wlr_constraint, sx, sy);
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

#ifdef PHOC_XWAYLAND
static const char *atom_map[XWAYLAND_ATOM_LAST] = {
	"_NET_WM_WINDOW_TYPE_NORMAL",
	"_NET_WM_WINDOW_TYPE_DIALOG"
};

static
void handle_xwayland_ready(struct wl_listener *listener, void *data) {
  PhocDesktop *desktop = wl_container_of (
        listener, desktop, xwayland_ready);
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
}

static
void handle_xwayland_remove_startup_id(struct wl_listener *listener, void *data) {
  PhocDesktop *desktop = wl_container_of (
        listener, desktop, xwayland_remove_startup_id);
  struct wlr_xwayland_remove_startup_info_event *ev = data;

  g_assert (PHOC_IS_DESKTOP (desktop));
  g_assert (ev->id);

  phoc_phosh_private_notify_startup_id (desktop->phosh,
                                        ev->id,
                                        PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_X11);
}
#endif /* PHOC_XWAYLAND */

static void
handle_output_destroy (PhocOutput *destroyed_output)
{
	PhocDesktop *self = destroyed_output->desktop;
	PhocOutput *output;
	char *input_name;
	GHashTableIter iter;
	g_hash_table_iter_init (&iter, self->input_output_map);
	while (g_hash_table_iter_next (&iter, (gpointer) &input_name,
				       (gpointer) &output)){
		if (destroyed_output == output){
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

  g_signal_connect (output, "output-destroyed",
                    G_CALLBACK (handle_output_destroy),
                    NULL);
}


static void
phoc_desktop_setup_xwayland (PhocDesktop *self)
{
#ifdef PHOC_XWAYLAND
  const char *cursor_default = PHOC_XCURSOR_DEFAULT;
  PhocConfig *config = self->config;
  PhocServer *server = phoc_server_get_default ();

  self->xcursor_manager = wlr_xcursor_manager_create(NULL, PHOC_XCURSOR_SIZE);
  g_return_if_fail (self->xcursor_manager);

  if (config->xwayland) {
    self->xwayland = wlr_xwayland_create(server->wl_display,
					 server->compositor, config->xwayland_lazy);
    if (!self->xwayland) {
      g_critical ("Failed to initialize Xwayland");
      g_unsetenv ("DISPLAY");
      return;
    }

    wl_signal_add(&self->xwayland->events.new_surface,
		  &self->xwayland_surface);
    self->xwayland_surface.notify = handle_xwayland_surface;

    wl_signal_add(&self->xwayland->events.ready,
		  &self->xwayland_ready);
    self->xwayland_ready.notify = handle_xwayland_ready;

    wl_signal_add(&self->xwayland->events.remove_startup_info,
		  &self->xwayland_remove_startup_id);
    self->xwayland_remove_startup_id.notify = handle_xwayland_remove_startup_id;

    g_setenv ("DISPLAY", self->xwayland->display_name, true);

    if (!wlr_xcursor_manager_load(self->xcursor_manager, 1))
      g_critical ("Cannot load XWayland XCursor theme");

    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
								  self->xcursor_manager, cursor_default, 1);
    if (xcursor != NULL) {
      struct wlr_xcursor_image *image = xcursor->images[0];
      wlr_xwayland_set_cursor(self->xwayland, image->buffer,
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
  PhocServer *server = phoc_server_get_default ();

  G_OBJECT_CLASS (phoc_desktop_parent_class)->constructed (object);

  wl_list_init(&self->views);
  wl_list_init(&self->outputs);

  self->new_output.notify = handle_new_output;
  wl_signal_add(&server->backend->events.new_output, &self->new_output);

  self->layout = wlr_output_layout_create();
  wlr_xdg_output_manager_v1_create(server->wl_display, self->layout);
  self->layout_change.notify = handle_layout_change;
  wl_signal_add(&self->layout->events.change, &self->layout_change);

  self->xdg_shell = wlr_xdg_shell_create(server->wl_display);
  wl_signal_add(&self->xdg_shell->events.new_surface,
		&self->xdg_shell_surface);
  self->xdg_shell_surface.notify = handle_xdg_shell_surface;

  self->layer_shell = wlr_layer_shell_v1_create(server->wl_display);
  wl_signal_add(&self->layer_shell->events.new_surface,
		&self->layer_shell_surface);
  self->layer_shell_surface.notify = handle_layer_shell_surface;
  self->layer_shell_effects = phoc_layer_shell_effects_new ();

  self->tablet_v2 = wlr_tablet_v2_create(server->wl_display);

  char cursor_size_fmt[16];
  snprintf(cursor_size_fmt, sizeof(cursor_size_fmt),
	   "%d", PHOC_XCURSOR_SIZE);
  g_setenv("XCURSOR_SIZE", cursor_size_fmt, 1);

  phoc_desktop_setup_xwayland (self);

  self->gamma_control_manager_v1 = wlr_gamma_control_manager_v1_create(server->wl_display);
  self->export_dmabuf_manager_v1 =
    wlr_export_dmabuf_manager_v1_create(server->wl_display);
  self->server_decoration_manager =
    wlr_server_decoration_manager_create(server->wl_display);
  wlr_server_decoration_manager_set_default_mode(self->server_decoration_manager,
						 WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT);
  self->idle = wlr_idle_create(server->wl_display);
  self->primary_selection_device_manager =
    wlr_primary_selection_v1_device_manager_create(server->wl_display);
  self->input_inhibit =
    wlr_input_inhibit_manager_create(server->wl_display);
  self->input_inhibit_activate.notify = input_inhibit_activate;
  wl_signal_add(&self->input_inhibit->events.activate,
		&self->input_inhibit_activate);
  self->input_inhibit_deactivate.notify = input_inhibit_deactivate;
  wl_signal_add(&self->input_inhibit->events.deactivate,
		&self->input_inhibit_deactivate);

  self->input_method =
    wlr_input_method_manager_v2_create(server->wl_display);

  self->text_input = wlr_text_input_manager_v3_create(server->wl_display);

  self->gtk_shell = phoc_gtk_shell_create(self, server->wl_display);
  self->phosh = phoc_phosh_private_new ();

  self->xdg_activation_v1 = wlr_xdg_activation_v1_create (server->wl_display);
  self->xdg_activation_v1_request_activate.notify = phoc_xdg_activation_v1_handle_request_activate;
  wl_signal_add (&self->xdg_activation_v1->events.request_activate,
                 &self->xdg_activation_v1_request_activate);

  self->virtual_keyboard = wlr_virtual_keyboard_manager_v1_create(
								  server->wl_display);
  wl_signal_add(&self->virtual_keyboard->events.new_virtual_keyboard,
		&self->virtual_keyboard_new);
  self->virtual_keyboard_new.notify = phoc_handle_virtual_keyboard;

  self->virtual_pointer = wlr_virtual_pointer_manager_v1_create(server->wl_display);
  wl_signal_add(&self->virtual_pointer->events.new_virtual_pointer,
		&self->virtual_pointer_new);
  self->virtual_pointer_new.notify = phoc_handle_virtual_pointer;

  self->screencopy = wlr_screencopy_manager_v1_create(server->wl_display);

  self->xdg_decoration_manager =
    wlr_xdg_decoration_manager_v1_create(server->wl_display);
  wl_signal_add(&self->xdg_decoration_manager->events.new_toplevel_decoration,
		&self->xdg_toplevel_decoration);
  self->xdg_toplevel_decoration.notify = handle_xdg_toplevel_decoration;
  wlr_viewporter_create(server->wl_display);

  struct wlr_xdg_foreign_registry *foreign_registry =
                wlr_xdg_foreign_registry_create(server->wl_display);
  wlr_xdg_foreign_v1_create(server->wl_display, foreign_registry);
  wlr_xdg_foreign_v2_create(server->wl_display, foreign_registry);

  self->pointer_constraints =
    wlr_pointer_constraints_v1_create(server->wl_display);
  self->pointer_constraint.notify = handle_pointer_constraint;
  wl_signal_add(&self->pointer_constraints->events.new_constraint,
		&self->pointer_constraint);

  self->presentation =
    wlr_presentation_create(server->wl_display, server->backend);
  self->foreign_toplevel_manager_v1 =
    wlr_foreign_toplevel_manager_v1_create(server->wl_display);
  self->relative_pointer_manager =
    wlr_relative_pointer_manager_v1_create(server->wl_display);
  self->pointer_gestures =
    wlr_pointer_gestures_v1_create(server->wl_display);

  self->output_manager_v1 =
    wlr_output_manager_v1_create(server->wl_display);
  self->output_manager_apply.notify = handle_output_manager_apply;
  wl_signal_add(&self->output_manager_v1->events.apply,
		&self->output_manager_apply);
  self->output_manager_test.notify = handle_output_manager_test;
  wl_signal_add(&self->output_manager_v1->events.test,
		&self->output_manager_test);

  self->output_power_manager_v1 =
    wlr_output_power_manager_v1_create(server->wl_display);
  self->output_power_manager_set_mode.notify =
    phoc_output_handle_output_power_manager_set_mode;
  wl_signal_add(&self->output_power_manager_v1->events.set_mode,
		&self->output_power_manager_set_mode);

  wlr_data_control_manager_v1_create(server->wl_display);

  self->settings = g_settings_new ("sm.puri.phoc");
  g_signal_connect_swapped(self->settings, "changed::auto-maximize",
			   G_CALLBACK (auto_maximize_changed_cb), self);
  auto_maximize_changed_cb(self, "auto-maximize", self->settings);

  g_settings_bind (self->settings, "scale-to-fit", self, "scale-to-fit", G_SETTINGS_BIND_DEFAULT);
}


static void
phoc_desktop_finalize (GObject *object)
{
  PhocDesktop *self = PHOC_DESKTOP (object);

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

  g_clear_object (&self->phosh);
  g_clear_pointer (&self->gtk_shell, phoc_gtk_shell_destroy);
  g_clear_object (&self->layer_shell_effects);
  g_clear_pointer (&self->layout, wlr_output_layout_destroy);

  g_hash_table_remove_all (self->input_output_map);
  g_hash_table_unref (self->input_output_map);

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

  props[PROP_CONFIG] =
    g_param_spec_pointer (
      "config",
      "Config",
      "The config object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

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
  g_autoptr (PhocGesture) touchpad_swipe = NULL;

  self->input_output_map = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  NULL);
}


PhocDesktop *
phoc_desktop_new (PhocConfig *config)
{
  return g_object_new (PHOC_TYPE_DESKTOP, "config", config, NULL);
}


/**
 * phoc_desktop_toggle_output_blank:
 *
 * Blank or unblank all outputs depending on the current state.
 * Brings all outputs into consistent state (either all blanked
 * or all enabled).
 */
void
phoc_desktop_toggle_output_blank (PhocDesktop *self)
{
  PhocOutput *output;

  gboolean enable = true;
  wl_list_for_each(output, &self->outputs, link) {
    if (output->wlr_output->enabled) {
      enable = false;
      break;
    }
  }

  wl_list_for_each(output, &self->outputs, link) {
    if (!wlr_output_layout_get (self->layout, output->wlr_output))
      continue;
    bool prior = output->wlr_output->enabled;
    wlr_output_enable (output->wlr_output, enable);
    if (!wlr_output_commit (output->wlr_output)) {
      /* Reset the enabled state if committing failed. */
      g_warning ("Could not set output enabled state to %d", enable);
      wlr_output_enable (output->wlr_output, prior);
      continue;
    }
    if (enable)
      phoc_output_damage_whole(output);
  }
}

/**
 * phoc_desktop_set_auto_maximize:
 *
 * Turn auto maximization of toplevels on (%TRUE) or off (%FALSE)
 */
void
phoc_desktop_set_auto_maximize (PhocDesktop *self, gboolean enable)
{
  PhocView *view;
  PhocServer *server = phoc_server_get_default();

  if (G_UNLIKELY (server->debug_flags & PHOC_SERVER_DEBUG_FLAG_AUTO_MAXIMIZE)) {
    if (enable == FALSE)
      g_info ("Not disabling auto-maximize due to `auto-maximize` debug flag");
    enable = TRUE;
  }

  g_debug ("auto-maximize: %d", enable);
  self->maximize = enable;

  /* Disabling auto-maximize leaves all views in their current position */
  if (!enable) {
    wl_list_for_each (view, &self->views, link)
      view_appear_activated (view, phoc_input_view_has_focus (phoc_server_get_default()->input, view));
    return;
  }

  wl_list_for_each (view, &self->views, link) {
    view_auto_maximize (view);
    view_appear_activated (view, true);
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
PhocLayerSurface
*phoc_desktop_layer_surface_at(PhocDesktop *self, double lx, double ly, double *sx, double *sy)
{
  struct wlr_surface *wlr_surface;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  PhocLayerSurface *layer_surface;
  double sx_, sy_;

  g_assert (PHOC_IS_DESKTOP (self));

  wlr_surface = phoc_desktop_surface_at (self, lx, ly, &sx_, &sy_, NULL);

  if (!wlr_surface)
    return NULL;

  if (!wlr_surface_is_layer_surface (wlr_surface))
    return NULL;

  wlr_layer_surface = wlr_layer_surface_v1_from_wlr_surface (wlr_surface);
  layer_surface = wlr_layer_surface->data;

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
 * phoc_desktop_get_draggable_layer_surface:
 * @self: The `PhocDesktop` to look the surface up for
 * @layer_surface: The layer surface to look up
 *
 * Returns a draggable layer surface if `layer_surface` is configured as
 * such. `NULL` otherwise.
 *
 * Returns: (nullable): The `PhocDraggableLayerSurface`
 */
PhocDraggableLayerSurface *
phoc_desktop_get_draggable_layer_surface (PhocDesktop *self, PhocLayerSurface *layer_surface)
{
  g_assert (PHOC_IS_DESKTOP (self));
  g_assert (layer_surface);

  return phoc_layer_shell_effects_get_draggable_layer_surface_from_layer_surface (
    self->layer_shell_effects, layer_surface);
}
