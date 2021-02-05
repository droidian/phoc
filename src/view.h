#ifndef ROOTSTON_VIEW_H
#define ROOTSTON_VIEW_H

#include "desktop.h"

#include <stdbool.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "output.h"

struct roots_view;

struct roots_view_interface {
	void (*activate)(struct roots_view *view, bool active);
	void (*move)(struct roots_view *view, double x, double y);
	void (*resize)(struct roots_view *view, uint32_t width, uint32_t height);
	void (*move_resize)(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height);
	bool (*want_scaling)(struct roots_view *view);
	bool (*want_auto_maximize)(struct roots_view *view);
	void (*maximize)(struct roots_view *view, bool maximized);
	void (*set_fullscreen)(struct roots_view *view, bool fullscreen);
	void (*close)(struct roots_view *view);
	void (*for_each_surface)(struct roots_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data);
	void (*get_geometry)(struct roots_view *view, struct wlr_box *box);
	void (*destroy)(struct roots_view *view);
};

enum roots_view_type {
	ROOTS_XDG_SHELL_VIEW,
#ifdef PHOC_XWAYLAND
	ROOTS_XWAYLAND_VIEW,
#endif
};

typedef enum {
  PHOC_VIEW_TILE_LEFT,
  PHOC_VIEW_TILE_RIGHT,
} PhocViewTileDirection;

typedef enum {
  PHOC_VIEW_STATE_NORMAL,
  PHOC_VIEW_STATE_MAXIMIZED,
  PHOC_VIEW_STATE_TILED,
} PhocViewState;

struct roots_view {
	enum roots_view_type type;
	const struct roots_view_interface *impl;
	PhocDesktop *desktop;
	struct wl_list link; // roots_desktop::views
	struct wl_list parent_link; // roots_view::stack

	struct wlr_box box;
	float alpha;
	float scale;

	bool decorated;
	int border_width;
	int titlebar_height;

	char *title;
	char *app_id;

	PhocViewState state;
	PhocViewTileDirection tile_direction;
	PhocOutput *fullscreen_output;
	struct {
		double x, y;
		uint32_t width, height;
	} saved;

	struct {
		bool update_x, update_y;
		double x, y;
		uint32_t width, height;
	} pending_move_resize;

	struct roots_view *parent;
	struct wl_list stack; // roots_view::link

	struct wlr_surface *wlr_surface; // set only when the surface is mapped
	struct wl_list child_surfaces; // roots_view_child::link

	struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
	struct wl_listener toplevel_handle_request_maximize;
	struct wl_listener toplevel_handle_request_activate;
	struct wl_listener toplevel_handle_request_fullscreen;
	struct wl_listener toplevel_handle_request_close;

	struct wl_listener new_subsurface;

	struct {
		struct wl_signal unmap;
		struct wl_signal destroy;
	} events;
};

struct roots_xdg_toplevel_decoration;

struct roots_xdg_surface {
	struct roots_view view;

	struct wlr_xdg_surface *xdg_surface;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener set_parent;

	struct wl_listener surface_commit;

	uint32_t pending_move_resize_configure_serial;

	struct roots_xdg_toplevel_decoration *xdg_toplevel_decoration;
};

#ifdef PHOC_XWAYLAND
struct roots_xwayland_surface {
	struct roots_view view;

	struct wlr_xwayland_surface *xwayland_surface;

	struct wl_listener destroy;
	struct wl_listener request_configure;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener set_title;
	struct wl_listener set_class;

	struct wl_listener surface_commit;
};
#endif

struct roots_view_child;

struct roots_view_child_interface {
	void (*destroy)(struct roots_view_child *child);
};

struct roots_view_child {
	struct roots_view *view;
	const struct roots_view_child_interface *impl;
	struct wlr_surface *wlr_surface;
	struct wl_list link;

	struct wl_listener commit;
	struct wl_listener new_subsurface;
};

struct roots_subsurface {
	struct roots_view_child view_child;
	struct wlr_subsurface *wlr_subsurface;
	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
};

struct roots_xdg_popup {
	struct roots_view_child view_child;
	struct wlr_xdg_popup *wlr_popup;
	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener new_popup;
};

struct roots_xdg_toplevel_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
	struct roots_xdg_surface *surface;
	struct wl_listener destroy;
	struct wl_listener request_mode;
	struct wl_listener surface_commit;
};

void view_init(struct roots_view *view, const struct roots_view_interface *impl,
	enum roots_view_type type, PhocDesktop *desktop);
void view_destroy(struct roots_view *view);
void view_activate(struct roots_view *view, bool activate);
void view_apply_damage(struct roots_view *view);
void view_damage_whole(struct roots_view *view);
gboolean view_is_maximized(const struct roots_view *view);
gboolean view_is_tiled(const struct roots_view *view);
void view_update_position(struct roots_view *view, int x, int y);
void view_update_size(struct roots_view *view, int width, int height);
void view_update_decorated(struct roots_view *view, bool decorated);
void view_initial_focus(struct roots_view *view);
void view_map(struct roots_view *view, struct wlr_surface *surface);
void view_unmap(struct roots_view *view);
void view_arrange_maximized(struct roots_view *view);
void view_arrange_tiled(struct roots_view *view);
void view_get_box(const struct roots_view *view, struct wlr_box *box);
void view_get_geometry(struct roots_view *view, struct wlr_box *box);
void view_move(struct roots_view *view, double x, double y);
bool view_move_to_next_output (struct roots_view *view, enum wlr_direction direction);
void view_resize(struct roots_view *view, uint32_t width, uint32_t height);
void view_move_resize(struct roots_view *view, double x, double y,
	uint32_t width, uint32_t height);
void view_auto_maximize(struct roots_view *view);
void view_tile(struct roots_view *view, PhocViewTileDirection direction);
void view_maximize(struct roots_view *view);
void view_restore(struct roots_view *view);
void view_set_fullscreen(struct roots_view *view, bool fullscreen,
	struct wlr_output *output);
void view_close(struct roots_view *view);
bool view_center(struct roots_view *view);
void view_setup(struct roots_view *view);
void view_teardown(struct roots_view *view);
void view_set_title(struct roots_view *view, const char *title);
void view_set_parent(struct roots_view *view, struct roots_view *parent);
void view_set_app_id(struct roots_view *view, const char *app_id);
void view_create_foreign_toplevel_handle(struct roots_view *view);
void view_get_deco_box(const struct roots_view *view, struct wlr_box *box);
void view_for_each_surface(struct roots_view *view,
	wlr_surface_iterator_func_t iterator, void *user_data);
struct roots_view *roots_view_from_wlr_surface (struct wlr_surface *surface);

struct roots_xdg_surface *roots_xdg_surface_from_view(struct roots_view *view);
struct roots_xwayland_surface *roots_xwayland_surface_from_view(
	struct roots_view *view);

enum roots_deco_part {
	ROOTS_DECO_PART_NONE = 0,
	ROOTS_DECO_PART_TOP_BORDER = (1 << 0),
	ROOTS_DECO_PART_BOTTOM_BORDER = (1 << 1),
	ROOTS_DECO_PART_LEFT_BORDER = (1 << 2),
	ROOTS_DECO_PART_RIGHT_BORDER = (1 << 3),
	ROOTS_DECO_PART_TITLEBAR = (1 << 4),
};

enum roots_deco_part view_get_deco_part(struct roots_view *view, double sx, double sy);

void view_child_init(struct roots_view_child *child,
	const struct roots_view_child_interface *impl, struct roots_view *view,
	struct wlr_surface *wlr_surface);
void view_child_destroy(struct roots_view_child *child);

struct roots_subsurface *subsurface_create(struct roots_view *view,
	struct wlr_subsurface *wlr_subsurface);

#endif
