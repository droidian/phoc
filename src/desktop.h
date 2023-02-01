#pragma once

#include "phoc-config.h"
#include "gtk-shell.h"
#include "layer-shell-effects.h"
#include "phosh-private.h"
#include "view.h"

#include <time.h>
#include <wayland-server-core.h>
#include <wlr/config.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_activation_v1.h>

#include <gio/gio.h>

#include "settings.h"

#ifdef PHOC_XWAYLAND
#include "xwayland.h"
#endif

#define PHOC_TYPE_DESKTOP (phoc_desktop_get_type())

G_DECLARE_FINAL_TYPE (PhocDesktop, phoc_desktop, PHOC, DESKTOP, GObject);

struct _PhocDesktop {
	GObject parent;

	struct wl_list views; // PhocView::link

	struct wl_list outputs; // PhocOutput::link

	PhocConfig *config;

	struct wlr_output_layout *layout;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_gamma_control_manager_v1 *gamma_control_manager_v1;
	struct wlr_export_dmabuf_manager_v1 *export_dmabuf_manager_v1;
	struct wlr_server_decoration_manager *server_decoration_manager;
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wlr_primary_selection_v1_device_manager *primary_selection_device_manager;
	struct wlr_idle *idle;
	struct wlr_input_inhibit_manager *input_inhibit;
	struct wlr_layer_shell_v1 *layer_shell;
	struct wlr_input_method_manager_v2 *input_method;
	struct wlr_text_input_manager_v3 *text_input;
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard;
	struct wlr_virtual_pointer_manager_v1 *virtual_pointer;
	struct wlr_screencopy_manager_v1 *screencopy;
	struct wlr_tablet_manager_v2 *tablet_v2;
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wlr_presentation *presentation;
	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager_v1;
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
	struct wlr_pointer_gestures_v1 *pointer_gestures;
	struct wlr_output_manager_v1 *output_manager_v1;
	struct wlr_output_power_manager_v1 *output_power_manager_v1;
	struct wlr_xdg_activation_v1 *xdg_activation_v1;

	struct wl_listener new_output;
	struct wl_listener layout_change;
	struct wl_listener xdg_shell_surface;
	struct wl_listener layer_shell_surface;
	struct wl_listener xdg_toplevel_decoration;
	struct wl_listener input_inhibit_activate;
	struct wl_listener input_inhibit_deactivate;
	struct wl_listener virtual_keyboard_new;
	struct wl_listener virtual_pointer_new;
	struct wl_listener pointer_constraint;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;
	struct wl_listener output_power_manager_set_mode;
	struct wl_listener xdg_activation_v1_request_activate;

#ifdef PHOC_XWAYLAND
	struct wlr_xcursor_manager *xcursor_manager;
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_surface;
	struct wl_listener xwayland_ready;
	struct wl_listener xwayland_remove_startup_id;
	xcb_atom_t xwayland_atoms[XWAYLAND_ATOM_LAST];
#endif

	GSettings *settings;
	gboolean maximize, scale_to_fit;
	GHashTable *input_output_map;

	/* Protocols without upstreamable implementations */
	PhocPhoshPrivate *phosh;
	PhocGtkShell *gtk_shell;
        /* Protocols that should go upstream */
	PhocLayerShellEffects *layer_shell_effects;
};

PhocDesktop *phoc_desktop_new (PhocConfig *config);
void         phoc_desktop_toggle_output_blank (PhocDesktop *self);
void         phoc_desktop_set_auto_maximize (PhocDesktop *self, gboolean on);
gboolean     phoc_desktop_get_auto_maximize (PhocDesktop *self);
void         phoc_desktop_set_scale_to_fit (PhocDesktop *self, gboolean on);
gboolean     phoc_desktop_get_scale_to_fit (PhocDesktop *self);
PhocOutput  *phoc_desktop_find_output (PhocDesktop *self,
                                       const char  *make,
                                       const char  *model,
                                       const char  *serial);
PhocOutput *phoc_desktop_get_builtin_output (PhocDesktop *self);

struct wlr_surface *phoc_desktop_surface_at(PhocDesktop *desktop,
		double lx, double ly, double *sx, double *sy,
		PhocView **view);
gboolean phoc_desktop_view_is_visible (PhocDesktop *desktop, PhocView *view);

PhocLayerSurface  *phoc_desktop_layer_surface_at(PhocDesktop *self,
                                                 double lx, double ly,
                                                 double *sx, double *sy);
PhocDraggableLayerSurface *
phoc_desktop_get_draggable_layer_surface (PhocDesktop *self, PhocLayerSurface *layer_surface);

void handle_xdg_shell_surface(struct wl_listener *listener, void *data);
void handle_xdg_toplevel_decoration(struct wl_listener *listener, void *data);
void handle_layer_shell_surface(struct wl_listener *listener, void *data);
void handle_xwayland_surface(struct wl_listener *listener, void *data);
