#ifndef ROOTSTON_OUTPUT_H
#define ROOTSTON_OUTPUT_H

#include <gio/gio.h>
#include <glib-object.h>
#include <pixman.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output_damage.h>

#define PHOC_TYPE_OUTPUT (phoc_output_get_type ())

G_DECLARE_FINAL_TYPE (PhocOutput, phoc_output, PHOC, OUTPUT, GObject);

/* These need to know about PhocOutput so we have them after the type definition.
 * This will fix itself once view / phosh are gobjects and most of
 * their members are non-public. */
#include "desktop.h"

/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
struct _PhocOutput {
  GObject                   parent;

  PhocDesktop              *desktop;
  struct wlr_output        *wlr_output;
  struct wl_list            link; // PhocDesktop::outputs

  struct roots_view        *fullscreen_view;
  struct wl_list            layers[4]; // layer_surface::link
  bool                      force_shell_reveal;

  struct timespec           last_frame;
  struct wlr_output_damage *damage;
  GList                    *debug_touch_points;

  struct wlr_box            usable_area;

  struct wl_listener        enable;
  struct wl_listener        mode;
  struct wl_listener        transform;
  struct wl_listener        damage_frame;
  struct wl_listener        damage_destroy;
  struct wl_listener        output_destroy;
};

PhocOutput *phoc_output_new (PhocDesktop       *desktop,
                             struct wlr_output *wlr_output);

typedef void (*roots_surface_iterator_func_t)(PhocOutput *self,
                                              struct wlr_surface *surface, struct wlr_box *box, float rotation,
                                              float scale, void *user_data);

void        phoc_output_xdg_surface_for_each_surface (
  PhocOutput *self,
  struct wlr_xdg_surface *xdg_surface, double ox, double oy,
  roots_surface_iterator_func_t iterator, void *user_data);
void        phoc_output_surface_for_each_surface (PhocOutput *self, struct wlr_surface
                                                  *surface, double ox, double oy,
                                                  roots_surface_iterator_func_t iterator,
                                                  void *user_data);
void        phoc_output_view_for_each_surface (
  PhocOutput *self,
  struct roots_view *view, roots_surface_iterator_func_t iterator,
  void *user_data);
void        phoc_output_drag_icons_for_each_surface (
  PhocOutput *self,
  PhocInput *input, roots_surface_iterator_func_t iterator,
  void *user_data);
void        phoc_output_layer_for_each_surface (
  PhocOutput *self,
  struct wl_list *layer_surfaces, roots_surface_iterator_func_t iterator,
  void *user_data);
#ifdef PHOC_XWAYLAND
struct wlr_xwayland_surface;
void        phoc_output_xwayland_children_for_each_surface (
  PhocOutput *self,
  struct wlr_xwayland_surface *surface,
  roots_surface_iterator_func_t iterator, void *user_data);
#endif
void        phoc_output_for_each_surface (PhocOutput                   *self,
                                          roots_surface_iterator_func_t iterator,
                                          void                         *user_data,
                                          gboolean                      visible_only);

void        handle_output_manager_apply (struct wl_listener *listener, void *data);
void        handle_output_manager_test (struct wl_listener *listener, void *data);
void        phoc_output_handle_output_power_manager_set_mode (struct wl_listener *listener, void *data);

struct roots_view;
typedef struct _PhocDragIcon PhocDragIcon;
void        phoc_output_damage_whole (PhocOutput *output);
void        phoc_output_damage_whole_view (PhocOutput *self, struct roots_view   *view);
void        phoc_output_damage_from_view (PhocOutput *self, struct roots_view
                                          *view);
void        phoc_output_damage_whole_drag_icon (PhocOutput   *self,
                                                PhocDragIcon *icon);
void        phoc_output_damage_from_local_surface (PhocOutput *self, struct wlr_surface *surface, double
                                                   ox, double oy);
void        phoc_output_damage_whole_local_surface (PhocOutput *self, struct wlr_surface *surface,
                                                    double ox, double oy);

void        phoc_output_scale_box (PhocOutput *self, struct wlr_box *box, float scale);
void        phoc_output_get_decoration_box (PhocOutput *self, struct roots_view *view,
                                            struct wlr_box *box);
gboolean    phoc_output_is_builtin (PhocOutput *output);

#endif
