#pragma once

#include <stdbool.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_surface.h>

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _PhocView PhocView;
typedef struct _PhocDesktop PhocDesktop;
typedef struct _PhocOutput PhocOutput;

typedef enum {
	PHOC_XDG_SHELL_VIEW,
#ifdef PHOC_XWAYLAND
	PHOC_XWAYLAND_VIEW,
#endif
} PhocViewType;

typedef enum {
  PHOC_VIEW_TILE_LEFT,
  PHOC_VIEW_TILE_RIGHT,
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

/**
 * PhocView:
 * @type: The type of the toplevel
 *
 * A `PhocView` represents a toplevel like an xdg-toplevel or a xwayland window.
 */
struct _PhocView {
	GObject parent_instance;

	PhocViewType type;
	PhocDesktop *desktop;
	struct wl_list link; // PhocDesktop::views
	struct wl_list parent_link; // PhocView::stack

	struct wlr_box box;
	float alpha;
	float scale;

	bool decorated;
	int border_width;
	int titlebar_height;

	PhocViewState state;
	PhocViewTileDirection tile_direction;
	PhocOutput *fullscreen_output;
	struct wlr_box saved;

	struct {
		bool update_x, update_y;
		double x, y;
		uint32_t width, height;
	} pending_move_resize;
	bool pending_centering;

	PhocView *parent;
	struct wl_list stack; // PhocView::link

	struct wlr_surface *wlr_surface; // set only when the surface is mapped
	struct wl_list child_surfaces; // PhocViewChild::link

	struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
	struct wl_listener toplevel_handle_request_maximize;
	struct wl_listener toplevel_handle_request_activate;
	struct wl_listener toplevel_handle_request_fullscreen;
	struct wl_listener toplevel_handle_request_close;

	struct wl_listener surface_new_subsurface;

	struct {
		struct wl_signal unmap;
		struct wl_signal destroy;
	} events;
};

/**
 * PhocViewClass:
 * @parent_class: The object class structure needs to be the first
 *   element in the widget class structure in order for the class mechanism
 *   to work correctly. This allows a PhocViewClass pointer to be cast to
 *   a GObjectClass pointer.
 * @move: This is called by `PhocView` to move a view to a new position.
 * @resize: This is called by `PhocView` to move resize a view.
 * @move_resize: This is called by `PhocView` to move and resize a view a the same time.
 * @want_auto_maximize: This is called by `PhocView` to determine if a view should
 *   be automatically maximized
 * @set_active: This is called by `PhocView` to make a view appear active.
 * @set_fullscreen: This is called by `PhocView` to fullscreen a view
 * @set_maximized: This is called by `PhocView` to maximize a view
 * @set_tiled: This is called by `PhocView` to tile a view.
 * @close: This is called by `PhocView` to close a view.
 * @for_each_surface: This is used by `PhocView` to iterate over a surface and it's children.
 * @get_geometry: This is called by `PhocView` to get a views geometry.
 */
typedef struct _PhocViewClass
{
  GObjectClass parent_class;

  void (*move)               (PhocView *self, double x, double y);
  void (*resize)             (PhocView *self, uint32_t width, uint32_t height);
  void (*move_resize)        (PhocView *self, double x, double y,
                              uint32_t  width, uint32_t height);
  bool (*want_scaling)       (PhocView *self);
  bool (*want_auto_maximize) (PhocView *self);
  void (*set_active)         (PhocView *self, bool active);
  void (*set_fullscreen)     (PhocView *self, bool fullscreen);
  void (*set_maximized)      (PhocView *self, bool maximized);
  void (*set_tiled)          (PhocView *self, bool tiled);
  void (*close)              (PhocView *self);
  void (*for_each_surface)   (PhocView *self, wlr_surface_iterator_func_t iterator, void *user_data);
  void (*get_geometry)       (PhocView *self, struct wlr_box *box);
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

typedef struct _PhocViewChild PhocViewChild;

struct phoc_view_child_interface {
  void (*destroy)(PhocViewChild *child);
};

/**
 * PhocViewChild:
 * @link: Link to PhocView::child_surfaces
 * @view: The [type@PhocView] this child belongs to
 * @parent: (nullable): The parent of this child if another child
 * @children: (nullable): children of this child
 *
 * A child of a [type@PhocView], e.g. a popup or subsurface
 */
typedef struct _PhocViewChild {
  const struct phoc_view_child_interface *impl;

  PhocView *view;
  PhocViewChild *parent;
  GSList *children;
  struct wlr_surface *wlr_surface;
  struct wl_list link;
  bool mapped;

  struct wl_listener commit;
  struct wl_listener new_subsurface;
} PhocViewChild;

void view_appear_activated(PhocView *view, bool activated);
void view_activate(PhocView *view, bool activate);
void phoc_view_apply_damage (PhocView *view);
void phoc_view_damage_whole (PhocView *view);
gboolean view_is_floating(const PhocView *view);
gboolean view_is_maximized(const PhocView *view);
gboolean view_is_tiled(const PhocView *view);
gboolean view_is_fullscreen(const PhocView *view);
void view_update_position(PhocView *view, int x, int y);
void view_update_size(PhocView *view, int width, int height);
void view_update_decorated(PhocView *view, bool decorated);
void view_initial_focus(PhocView *view);
void phoc_view_map (PhocView *view, struct wlr_surface *surface);
void view_unmap(PhocView *view);
void view_arrange_maximized(PhocView *view, struct wlr_output *output);
void view_arrange_tiled(PhocView *view, struct wlr_output *output);
void view_get_box(const PhocView *view, struct wlr_box *box);
void view_get_geometry(PhocView *view, struct wlr_box *box);
void view_move(PhocView *view, double x, double y);
bool view_move_to_next_output (PhocView *view, enum wlr_direction direction);
void view_resize(PhocView *view, uint32_t width, uint32_t height);
void view_move_resize(PhocView *view, double x, double y,
	uint32_t width, uint32_t height);
void view_auto_maximize(PhocView *view);
void view_tile(PhocView *view, PhocViewTileDirection direction, struct wlr_output *output);
void view_maximize(PhocView *view, struct wlr_output *output);
void view_restore(PhocView *view);
void phoc_view_set_fullscreen(PhocView *view, bool fullscreen, struct wlr_output *output);
void view_close(PhocView *view);
bool view_center(PhocView *view, struct wlr_output *output);
void view_send_frame_done_if_not_visible (PhocView *view);
void view_setup(PhocView *view);
void view_set_title(PhocView *view, const char *title);
void view_set_parent(PhocView *view, PhocView *parent);
void view_set_app_id(PhocView *view, const char *app_id);
void view_create_foreign_toplevel_handle(PhocView *view);
void view_get_deco_box(const PhocView *view, struct wlr_box *box);
void view_for_each_surface(PhocView *view,
	wlr_surface_iterator_func_t iterator, void *user_data);
PhocView *phoc_view_from_wlr_surface (struct wlr_surface *wlr_surface);

bool   phoc_view_is_mapped (PhocView *view);
PhocViewDecoPart view_get_deco_part(PhocView *view, double sx, double sy);

void phoc_view_child_init(PhocViewChild *child,
                          const struct phoc_view_child_interface *impl,
                          PhocView *view,
                          struct wlr_surface *wlr_surface);
void phoc_view_child_destroy (PhocViewChild *child);
void phoc_view_child_apply_damage (PhocViewChild *child);
void phoc_view_child_damage_whole (PhocViewChild *child);

G_END_DECLS
