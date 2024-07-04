#pragma once

#include "phoc-config.h"
#include "gtk-shell.h"
#include "layer-shell-effects.h"
#include "phosh-private.h"
#include "view.h"
#include "xwayland-surface.h"

#include <time.h>
#include <wayland-server-core.h>
#include <wlr/config.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
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
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_activation_v1.h>

#include <gio/gio.h>

#include "settings.h"

#define PHOC_TYPE_DESKTOP (phoc_desktop_get_type())

G_DECLARE_FINAL_TYPE (PhocDesktop, phoc_desktop, PHOC, DESKTOP, GObject);

/* TODO: we keep the struct public for now due to the list links and
   notifiers but we should avoid other member access */
struct _PhocDesktop {
  GObject parent;

  struct wl_list outputs; // PhocOutput::link

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
  struct wlr_tablet_manager_v2 *tablet_v2;
  struct wlr_pointer_constraints_v1 *pointer_constraints;
  struct wlr_presentation *presentation;
  struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager_v1;
  struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
  struct wlr_pointer_gestures_v1 *pointer_gestures;
  struct wlr_output_manager_v1 *output_manager_v1;
  struct wlr_output_power_manager_v1 *output_power_manager_v1;
  struct wlr_xdg_activation_v1 *xdg_activation_v1;
  struct wlr_security_context_manager_v1 *security_context_manager_v1;

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

  gboolean maximize, scale_to_fit;
  GHashTable *input_output_map;
};

PhocDesktop *phoc_desktop_new (void);

GQueue      *phoc_desktop_get_views         (PhocDesktop *self);
void         phoc_desktop_move_view_to_top  (PhocDesktop *self, PhocView *view);
gboolean     phoc_desktop_has_views         (PhocDesktop *self);
PhocView    *phoc_desktop_get_view_by_index (PhocDesktop *self, guint index);
void         phoc_desktop_insert_view       (PhocDesktop *self, PhocView *view);
gboolean     phoc_desktop_remove_view       (PhocDesktop *self, PhocView *view);

void         phoc_desktop_set_auto_maximize (PhocDesktop *self, gboolean on);
gboolean     phoc_desktop_get_auto_maximize (PhocDesktop *self);
void         phoc_desktop_set_scale_to_fit (PhocDesktop *self, gboolean on);
gboolean     phoc_desktop_get_scale_to_fit (PhocDesktop *self);
gboolean     phoc_desktop_get_enable_animations (PhocDesktop *self);
PhocOutput  *phoc_desktop_find_output (PhocDesktop *self,
                                       const char  *make,
                                       const char  *model,
                                       const char  *serial);
PhocOutput *phoc_desktop_find_output_by_name (PhocDesktop *self, const char *name);
PhocOutput *phoc_desktop_get_builtin_output (PhocDesktop *self);
PhocOutput *phoc_desktop_layout_get_output (PhocDesktop *self, double lx, double ly);

struct wlr_surface *phoc_desktop_wlr_surface_at (PhocDesktop *desktop,
                                                 double       lx,
                                                 double       ly,
                                                 double      *sx,
                                                 double      *sy,
                                                 PhocView   **view);
gboolean phoc_desktop_view_is_visible (PhocDesktop *desktop, PhocView *view);
void     phoc_desktop_set_view_always_on_top (PhocDesktop *self, PhocView *view, gboolean on_top);

PhocLayerSurface  *phoc_desktop_layer_surface_at(PhocDesktop *self,
                                                 double lx, double ly,
                                                 double *sx, double *sy);
PhocDraggableLayerSurface *
                     phoc_desktop_get_draggable_layer_surface    (PhocDesktop *self,
                                                                  PhocLayerSurface *layer_surface);
GSList              *phoc_desktop_get_layer_surface_stacks       (PhocDesktop *self);

PhocGtkShell        *phoc_desktop_get_gtk_shell                  (PhocDesktop *self);
PhocPhoshPrivate    *phoc_desktop_get_phosh_private              (PhocDesktop *self);

void                 phoc_desktop_notify_activity                (PhocDesktop *self,
                                                                  PhocSeat    *seat);
gboolean phoc_desktop_is_privileged_protocol (PhocDesktop            *self,
                                              const struct wl_global *global);
