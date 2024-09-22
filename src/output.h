#pragma once

#include "animatable.h"
#include "drag-icon.h"
#include "render.h"
#include "view.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

G_BEGIN_DECLS

#define PHOC_TYPE_OUTPUT (phoc_output_get_type ())

G_DECLARE_FINAL_TYPE (PhocOutput, phoc_output, PHOC, OUTPUT, GObject);

typedef struct _PhocDesktop PhocDesktop;
typedef struct _PhocInput PhocInput;
typedef struct _PhocLayerSurface PhocLayerSurface;

/**
 * PhocOutputScaleFilter:
 */
typedef enum _PhocOutputScaleFilter {
  PHOC_OUTPUT_SCALE_FILTER_AUTO = 1,
  PHOC_OUTPUT_SCALE_FILTER_BILINEAR,
  PHOC_OUTPUT_SCALE_FILTER_NEAREST,
} PhocOutputScaleFilter;

/**
 * PhocOutput:
 *
 * The output region of a compositor (typically a monitor).
 *
 * See wlroot's #wlr_output.
 */
/* TODO: we keep the struct public for now due to the list links and
   notifiers but we should avoid other member access */
struct _PhocOutput {
  GObject                   parent;

  PhocDesktop              *desktop;
  struct wlr_output        *wlr_output;
  struct wl_list            link; // PhocDesktop::outputs

  PhocView                 *fullscreen_view;
  struct wl_list            layer_surfaces; // PhocLayerSurface::link

  GList                    *debug_touch_points;

  struct wlr_box            usable_area;
  int                       lx, ly;

  struct wl_listener        commit;
  struct wl_listener        output_destroy;

  /* TODO: Should be private, move bits out of renderer */
  struct wlr_damage_ring    damage_ring;
};

PhocOutput *phoc_output_new (PhocDesktop       *desktop,
                             struct wlr_output *wlr_output,
                             GError           **error);
/**
 * PhocSurfaceIterator:
 * @self: The output
 * @surface: The surface to iterate over
 * @box: The part of the surface that overlaps with the output
 * @scale: The `scale-to-fit` scale
 * @user_data: User data passed to the iterator
 *
 * The iterator function that is invoked by the different iterators
 * like [method@Output.xdg_surface_for_each_surface] or
 * [method@Output.layer_surface_for_each_surface] if the iterated
 * surface overlaps with the output.
 */
typedef void (*PhocSurfaceIterator)(PhocOutput         *self,
                                    struct wlr_surface *surface,
                                    struct wlr_box     *box,
                                    float               scale,
                                    void               *user_data);
/* Surface iterators */
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
GQueue     *phoc_output_get_layer_surfaces_for_layer (PhocOutput                     *self,
                                                      enum zwlr_layer_shell_v1_layer  layer);
void        phoc_output_set_layer_dirty (PhocOutput *self, enum zwlr_layer_shell_v1_layer  layer);

/* signal handlers */
void        phoc_handle_output_manager_apply (struct wl_listener *listener, void *data);
void        phoc_handle_output_manager_test (struct wl_listener *listener, void *data);
void        phoc_output_handle_output_power_manager_set_mode (struct wl_listener *listener,
                                                              void *data);
void        phoc_output_handle_gamma_control_set_gamma (struct wl_listener *listener, void *data);

/* methods */
struct wlr_output *
            phoc_output_get_wlr_output (PhocOutput *output);
void        phoc_output_damage_whole (PhocOutput *output);
void        phoc_output_damage_from_view (PhocOutput *self, PhocView *view, bool whole);
void        phoc_output_damage_whole_drag_icon (PhocOutput   *self,
                                                PhocDragIcon *icon);
void        phoc_output_damage_from_surface (PhocOutput *self, struct wlr_surface *surface,
                                             double ox, double oy);
void        phoc_output_damage_whole_surface (PhocOutput *self, struct wlr_surface *surface,
                                              double ox, double oy);

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
void       phoc_output_transform_damage      (PhocOutput *self, pixman_region32_t *damage);
void       phoc_output_transform_box         (PhocOutput *self, struct wlr_box *box);

enum wlr_scale_filter_mode
           phoc_output_get_texture_filter_mode (PhocOutput *self);

G_END_DECLS
