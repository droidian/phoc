#pragma once

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/config.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_data_device.h>
#ifdef PHOC_XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xproto.h>
#endif
#include "settings.h"
#include "desktop.h"
#include "input.h"

G_BEGIN_DECLS

#define PHOC_TYPE_SERVER (phoc_server_get_type())

G_DECLARE_FINAL_TYPE (PhocServer, phoc_server, PHOC, SERVER, GObject);

typedef enum _PhocServerDebugFlags {
  PHOC_SERVER_DEBUG_FLAG_NONE = 0,
  PHOC_SERVER_DEBUG_FLAG_DAMAGE_TRACKING = 1 << 0,
  PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS = 1 << 1,
  PHOC_SERVER_DEBUG_FLAG_NO_QUIT = 1 << 2,
} PhocServerDebugFlags;

/* TODO: we keep the struct public due to heaps of direct access
   which will be replaced by getters and setters over time */
struct _PhocServer {
  GObject parent;

  /* Phoc resources */
  struct roots_config *config;
  PhocDesktop *desktop;
  struct roots_input *input;
  PhocServerDebugFlags debug_flags;
  gboolean inited;

  /* The session */
  gchar *session;
  gint exit_status;
  GMainLoop *mainloop;

  /* Wayland resources */
  struct wl_display *wl_display;
  guint wl_source;

  /* WLR tools */
  struct wlr_backend *backend;
  struct wlr_renderer *renderer;

  /* Workarounds */
  enum wl_shm_format preferred_pixel_format;

  /* Global resources */
  struct wlr_data_device_manager *data_device_manager;
};

PhocServer *phoc_server_get_default (void);
gboolean phoc_server_setup (PhocServer *server, const char *config_path,
			    const char *exec, GMainLoop *mainloop,
			    PhocServerDebugFlags debug_flags);
gint phoc_server_get_session_exit_status (PhocServer *self);

G_END_DECLS
