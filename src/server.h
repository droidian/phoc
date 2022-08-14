#pragma once

#include "render.h"

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/config.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include "settings.h"
#include "desktop.h"
#include "input.h"

G_BEGIN_DECLS

#define PHOC_TYPE_SERVER (phoc_server_get_type())

G_DECLARE_FINAL_TYPE (PhocServer, phoc_server, PHOC, SERVER, GObject);

/**
 * PhocServerFlags:
 *
 * PHOC_SHELL_FLAG_SHELL_MODE: Expect a shell to attach
 */
typedef enum _PhocServerFlags {
  PHOC_SERVER_FLAG_NONE = 0,
  PHOC_SERVER_FLAG_SHELL_MODE = 1 << 0,
} PhocServerFlags;

typedef enum _PhocServerDebugFlags {
  PHOC_SERVER_DEBUG_FLAG_NONE = 0,
  PHOC_SERVER_DEBUG_FLAG_AUTO_MAXIMIZE =   1 << 0,
  PHOC_SERVER_DEBUG_FLAG_DAMAGE_TRACKING = 1 << 1,
  PHOC_SERVER_DEBUG_FLAG_NO_QUIT =         1 << 2,
  PHOC_SERVER_DEBUG_FLAG_TOUCH_POINTS =    1 << 3,
} PhocServerDebugFlags;

/**
 * PhocServer:
 *
 * The server singleton.
 *
 * Maintains the compositors state.
 */
/* TODO: we keep the struct public due to heaps of direct access
   which will be replaced by getters and setters over time */
struct _PhocServer {
  GObject parent;

  /* Phoc resources */
  PhocConfig *config;
  PhocDesktop *desktop;
  PhocInput *input;
  PhocServerFlags flags;
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
  struct wlr_compositor *compositor;
  struct wlr_backend    *backend;
  PhocRenderer          *renderer;

  /* Workarounds */
  enum wl_shm_format preferred_pixel_format;

  /* Global resources */
  struct wlr_data_device_manager *data_device_manager;
};

PhocServer  *phoc_server_get_default (void);
gboolean     phoc_server_setup (PhocServer *server, const char *config_path,
                                const char *exec, GMainLoop *mainloop,
                                PhocServerFlags flags,
                                PhocServerDebugFlags debug_flags);
gint          phoc_server_get_session_exit_status (PhocServer *self);
PhocRenderer *phoc_server_get_renderer (PhocServer *self);

G_END_DECLS
