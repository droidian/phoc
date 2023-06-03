#pragma once

#include "animatable.h"
#include "render.h"
#include "view.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

G_BEGIN_DECLS

#define PHOC_TYPE_OUTPUT (phoc_output_get_type ())

G_DECLARE_FINAL_TYPE (PhocOutput, phoc_output, PHOC, OUTPUT, GObject);

typedef struct _PhocDesktop PhocDesktop;
typedef struct _PhocInput PhocInput;
typedef struct _PhocLayerSurface PhocLayerSurface;

/**
 * PhocOutput:
 *
 * The output region of a compositor (typically a monitor).
 *
 * See wlroot's #wlr_output.
 */
/* TODO: we keep the struct public due to the list links and
   notifiers but we should avoid other member access */
struct _PhocOutput {
  GObject                   parent;

  PhocDesktop              *desktop;
  struct wlr_output        *wlr_output;
  struct wl_list            link; // PhocDesktop::outputs

  PhocView                 *fullscreen_view;
  struct wl_list            layer_surfaces; // PhocLayerSurface::link

  struct wlr_output_damage *damage;
  GList                    *debug_touch_points;

  struct wlr_box            usable_area;

  struct wl_listener        enable;
  struct wl_listener        mode;
  struct wl_listener        commit;
  struct wl_listener        damage_frame;
  struct wl_listener        damage_destroy;
  struct wl_listener        output_destroy;
};

PhocOutput *phoc_output_new (PhocDesktop       *desktop,
                             struct wlr_output *wlr_output,
                             GError           **error);
/* Surface iterators */
typedef void (*PhocSurfaceIterator)(PhocOutput         *self,
                                    struct wlr_surface *surface,
                                    struct wlr_box     *box,
                                    float               rotation,
                                    float               scale,
                                    void               *user_data);
void        phoc_output_xdg_surface_for_each_surface (PhocOutput *self,
                                                      struct wlr_xdg_surface *xdg_surface,
                                                      double ox,
                                                      double oy,
                                                      PhocSurfaceIterator iterator,
                                                      void *user_data);
void        phoc_output_surface_for_each_surface     (PhocOutput *self,
                                                      struct wlr_surface *surface,
                                                      double ox,
                                                      double oy,
                                                      PhocSurfaceIterator iterator,
                                                      void *user_data);
void        phoc_output_view_for_each_surface        (PhocOutput *self,
                                                      PhocView *view,
                                                      PhocSurfaceIterator iterator,
                                                      void *user_data);
void        phoc_output_drag_icons_for_each_surface  (PhocOutput *self,
                                                      PhocInput *input,
                                                      PhocSurfaceIterator iterator,
                                                      void *user_data);
void        phoc_output_layer_surface_for_each_surface (PhocOutput          *self,
                                                        PhocLayerSurface    *layer_surface,
                                                        PhocSurfaceIterator  iterator,
                                                        void                *user_data);

#ifdef PHOC_XWAYLAND
struct wlr_xwayland_surface;
void        phoc_output_xwayland_children_for_each_surface (PhocOutput *self,
                                                            struct wlr_xwayland_surface *surface,
                                                            PhocSurfaceIterator iterator,
                                                            void *user_data);
#endif
void        phoc_output_for_each_surface             (PhocOutput *self,
                                                      PhocSurfaceIterator iterator,
                                                      void *user_data,
                                                      gboolean visible_only);
GList *     phoc_output_get_layer_surfaces_for_layer (PhocOutput                     *self,
                                                      enum zwlr_layer_shell_v1_layer  layer);

/* signal handlers */
void        handle_output_manager_apply (struct wl_listener *listener, void *data);
void        handle_output_manager_test (struct wl_listener *listener, void *data);
void        phoc_output_handle_output_power_manager_set_mode (struct wl_listener *listener,
                                                              void *data);
/* methods */
typedef struct _PhocDragIcon PhocDragIcon;
void        phoc_output_damage_whole (PhocOutput *output);
void        phoc_output_damage_from_view (PhocOutput *self, PhocView *view, bool whole);
void        phoc_output_damage_whole_drag_icon (PhocOutput   *self,
                                                PhocDragIcon *icon);
void        phoc_output_damage_from_local_surface (PhocOutput *self, struct wlr_surface *surface, double
                                                   ox, double oy);
void        phoc_output_damage_whole_local_surface (PhocOutput *self, struct wlr_surface *surface,
                                                    double ox, double oy);

void        phoc_output_scale_box (PhocOutput *self, struct wlr_box *box, float scale);
void        phoc_output_get_decoration_box (PhocOutput *self, PhocView *view,
                                            struct wlr_box *box);
void        phoc_output_update_shell_reveal (PhocOutput *self);
void        phoc_output_force_shell_reveal (PhocOutput *self, gboolean force);
gboolean    phoc_output_is_builtin (PhocOutput *output);
gboolean    phoc_output_is_match (PhocOutput *self,
                                  const char *make,
                                  const char *model,
                                  const char *serial);
gboolean    phoc_output_has_fullscreen_view (PhocOutput *self);
gboolean    phoc_output_has_layer (PhocOutput *self, enum zwlr_layer_shell_v1_layer layer);
gboolean    phoc_output_has_shell_revealed (PhocOutput *self);

guint       phoc_output_add_frame_callback   (PhocOutput        *self,
                                              PhocAnimatable    *animatable,
                                              PhocFrameCallback  callback,
                                              gpointer           user_data,
                                              GDestroyNotify     notify);
void       phoc_output_remove_frame_callback (PhocOutput        *self,
                                              guint              id);
void       phoc_output_remove_frame_callbacks_by_animatable (PhocOutput     *self,
                                                             PhocAnimatable *animatable);
bool       phoc_output_has_frame_callbacks   (PhocOutput        *self);

void       phoc_output_lower_shield          (PhocOutput *self);
void       phoc_output_raise_shield          (PhocOutput *self);
float      phoc_output_get_scale             (PhocOutput *self);
const char *phoc_output_get_name             (PhocOutput *self);

G_END_DECLS
