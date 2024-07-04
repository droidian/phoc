#pragma once

#include "view-child-private.h"

#include <stdbool.h>
#include <wlr/config.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PhocBling PhocBling;
typedef struct _PhocDesktop PhocDesktop;
typedef struct _PhocOutput PhocOutput;

typedef enum {
  PHOC_VIEW_TILE_NONE  = 0,
  PHOC_VIEW_TILE_LEFT  = 1 << 0,
  PHOC_VIEW_TILE_RIGHT = 1 << 1,
} PhocViewTileDirection;

typedef enum {
  PHOC_VIEW_STATE_FLOATING,
  PHOC_VIEW_STATE_MAXIMIZED,
  PHOC_VIEW_STATE_TILED,
} PhocViewState;

typedef enum _PhocViewDecoPart {
  PHOC_VIEW_DECO_PART_NONE          = 0,
  PHOC_VIEW_DECO_PART_TOP_BORDER    = 1 << 0,
  PHOC_VIEW_DECO_PART_BOTTOM_BORDER = 1 << 1,
  PHOC_VIEW_DECO_PART_LEFT_BORDER   = 1 << 2,
  PHOC_VIEW_DECO_PART_RIGHT_BORDER  = 1 << 3,
  PHOC_VIEW_DECO_PART_TITLEBAR      = 1 << 4,
} PhocViewDecoPart;

typedef enum {
  PHOC_VIEW_CORNER_NORTH_WEST,
  PHOC_VIEW_CORNER_NORTH_EAST,
  PHOC_VIEW_CORNER_SOUTH_EAST,
  PHOC_VIEW_CORNER_SOUTH_WEST,
} PhocViewCorner;

/**
 * PhocView:
 * @parent: The view's parent
 * @stack: List of of views direct children
 * @parent_link: The list link into stack
 *
 * A `PhocView` represents a toplevel like an xdg-toplevel or a xwayland window.
 */
/* TODO: we keep the struct public for now due to the list links but we should
   avoid other member access */
typedef struct _PhocView PhocView;
struct _PhocView {
  GObject parent_instance;

  PhocDesktop *desktop;

  struct wlr_box box;
  struct wlr_box saved;

  struct {
    bool update_x, update_y;
    double x, y;
    uint32_t width, height;
  } pending_move_resize;
  bool pending_centering;

  PhocView       *parent;
  struct wl_list  stack;
  struct wl_list  parent_link;

  struct wlr_surface *wlr_surface; // set only when the surface is mapped
};

/**
 * PhocViewClass:
 * @parent_class: The object class structure needs to be the first
 *   element in the widget class structure in order for the class mechanism
 *   to work correctly. This allows a PhocViewClass pointer to be cast to
 *   a GObjectClass pointer.
 * @move: This is called by `PhocView` to move a view to a new position.
 *     The implementation is optional.
 * @resize: This is called by `PhocView` to move resize a view.
 * @move_resize: This is called by `PhocView` to move and resize a view a the same time.
 * @want_auto_maximize: This is called by `PhocView` to determine if a view should
 *   be automatically maximized
 * @set_active: This is called by `PhocView` to make a view appear active.
 * @set_fullscreen: This is called by `PhocView` to fullscreen a view
 * @set_maximized: This is called by `PhocView` to maximize a view
 * @set_tiled: This is called by `PhocView` to tile a view.
 *     The implementation is optional.
 * @close: This is called by `PhocView` to close a view.
 * @for_each_surface: This is used by `PhocView` to iterate over a surface and it's children.
 *     The implementation is optional.
 * @get_geometry: This is called by `PhocView` to get a views geometry.
 *     The implementation is optional.
 * @get_wlr_surface_at: Get the wlr_surface at the give coordinates.
 *     The implementation is optional.
 */
typedef struct _PhocViewClass
{
  GObjectClass parent_class;

  void (*move)               (PhocView *self, double x, double y);
  void (*resize)             (PhocView *self, uint32_t width, uint32_t height);
  void (*move_resize)        (PhocView *self, double x, double y, uint32_t  width, uint32_t height);
  bool (*want_scaling)       (PhocView *self);
  bool (*want_auto_maximize) (PhocView *self);
  void (*set_active)         (PhocView *self, bool active);
  void (*set_fullscreen)     (PhocView *self, bool fullscreen);
  void (*set_maximized)      (PhocView *self, bool maximized);
  void (*set_tiled)          (PhocView *self, bool tiled);
  void (*close)              (PhocView *self);
  void (*for_each_surface)   (PhocView *self, wlr_surface_iterator_func_t iterator, void *user_data);
  void (*get_geometry)       (PhocView *self, struct wlr_box *box);
  struct wlr_surface *
       (*get_wlr_surface_at) (PhocView *self, double sx, double sy, double *sub_x, double *sub_y);
  pid_t (*get_pid)           (PhocView *self);
} PhocViewClass;


#define PHOC_TYPE_VIEW (phoc_view_get_type ())

GType phoc_view_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PhocView, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PhocViewClass, g_type_class_unref)
static inline PhocView * PHOC_VIEW (gpointer ptr) {
  return G_TYPE_CHECK_INSTANCE_CAST (ptr, phoc_view_get_type (), PhocView); }
static inline PhocViewClass * PHOC_VIEW_CLASS (gpointer ptr) {
  return G_TYPE_CHECK_CLASS_CAST (ptr, phoc_view_get_type (), PhocViewClass); }
static inline gboolean PHOC_IS_VIEW (gpointer ptr) {
  return G_TYPE_CHECK_INSTANCE_TYPE (ptr, phoc_view_get_type ()); }
static inline gboolean PHOC_IS_VIEW_CLASS (gpointer ptr) {
  return G_TYPE_CHECK_CLASS_TYPE (ptr, phoc_view_get_type ()); }
static inline PhocViewClass * PHOC_VIEW_GET_CLASS (gpointer ptr) {
  return G_TYPE_INSTANCE_GET_CLASS (ptr, phoc_view_get_type (), PhocViewClass); }

void                  phoc_view_appear_activated (PhocView *view, bool activated);
void                  phoc_view_activate (PhocView *self, bool activate);
void                  phoc_view_damage_whole (PhocView *view);
gboolean              phoc_view_is_floating (PhocView *view);
gboolean              phoc_view_is_maximized (PhocView *view);
gboolean              phoc_view_is_tiled (PhocView *view);
gboolean              phoc_view_is_fullscreen (PhocView *self);
void                  phoc_view_update_decorated (PhocView *view, bool decorated);
void                  phoc_view_arrange (PhocView *self, PhocOutput *output, gboolean center);
void                  phoc_view_get_box (PhocView *view, struct wlr_box *box);
void                  phoc_view_get_geometry (PhocView *self, struct wlr_box *box);
void                  phoc_view_move (PhocView *self, double x, double y);
bool                  phoc_view_move_to_next_output (PhocView *view, enum wlr_direction direction);
void                  phoc_view_move_to_corner (PhocView *self, PhocViewCorner corner);
void                  phoc_view_move_resize (PhocView *view,
                                             double    x,
                                             double    y,
                                             uint32_t  width,
                                             uint32_t  height);
void                  phoc_view_auto_maximize (PhocView *view);
void                  phoc_view_tile (PhocView             *view,
                                      PhocViewTileDirection direction,
                                      PhocOutput           *output);
PhocViewTileDirection phoc_view_get_tile_direction (PhocView *view);
void                  phoc_view_maximize (PhocView *view, PhocOutput *output);
void                  phoc_view_restore (PhocView *view);
void                  phoc_view_set_fullscreen (PhocView   *view,
                                                bool        fullscreen,
                                                PhocOutput *output);
void                  phoc_view_close (PhocView *self);
void                  phoc_view_set_app_id (PhocView *view, const char *app_id);
const char           *phoc_view_get_app_id (PhocView *self);
void                  phoc_view_for_each_surface (PhocView                   *self,
                                                  wlr_surface_iterator_func_t iterator,
                                                  gpointer                    user_data);
struct wlr_surface   *phoc_view_get_wlr_surface_at (PhocView *self,
                                                    double    sx,
                                                    double    sy,
                                                    double   *sub_x,
                                                    double   *sub_y);
PhocView             *phoc_view_from_wlr_surface (struct wlr_surface *wlr_surface);
PhocOutput           *phoc_view_get_output (PhocView *view);

pid_t                 phoc_view_get_pid (PhocView *self);
bool                  phoc_view_is_mapped (PhocView *view);
PhocViewDecoPart      phoc_view_get_deco_part (PhocView *view, double sx, double sy);
void                  phoc_view_set_scale_to_fit (PhocView *self, gboolean enable);
gboolean              phoc_view_get_scale_to_fit (PhocView *self);
void                  phoc_view_set_activation_token (PhocView *self, const char *token, int type);
const char           *phoc_view_get_activation_token (PhocView *self);
void                  phoc_view_flush_activation_token (PhocView *self);
float                 phoc_view_get_alpha (PhocView *self);
float                 phoc_view_get_scale (PhocView *self);
gboolean              phoc_view_is_decorated (PhocView *self);
void                  phoc_view_set_always_on_top (PhocView *self, gboolean on_top);
bool                  phoc_view_is_always_on_top (PhocView *self);
PhocOutput           *phoc_view_get_fullscreen_output (PhocView *self);
bool                  phoc_view_want_auto_maximize (PhocView *self);
void                  phoc_view_set_decorated (PhocView *self,
                                               gboolean  decorated);
gboolean              phoc_view_get_maximized_box (PhocView       *self,
                                                   PhocOutput     *output,
                                                   struct wlr_box *box);
gboolean              phoc_view_get_tiled_box (PhocView             *self,
                                               PhocViewTileDirection dir,
                                               PhocOutput           *output,
                                               struct wlr_box       *box);
void                  phoc_view_add_bling (PhocView *self, PhocBling *bling);
void                  phoc_view_remove_bling (PhocView *self, PhocBling *bling);
GSList               *phoc_view_get_blings (PhocView *self);
void                  phoc_view_add_child (PhocView *self, PhocViewChild *child);

G_END_DECLS
