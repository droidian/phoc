/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on wlroots layer-shell example which is BSD licensed.
 */

#define G_LOG_DOMAIN "phoc-example"

#include "egl-common.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "phoc-layer-shell-effects-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <linux/input-event-codes.h>
#include <GLES2/gl2.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <wlr/render/egl.h>

#include <glib.h>

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_seat *seat;
static struct wl_shm *shm;
static struct wl_pointer *pointer;
static struct wl_keyboard *keyboard;
static struct wl_touch *touch;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zwlr_layer_surface_v1 *layer_surface;
static struct zphoc_layer_shell_effects_v1 *layer_shell_effects;
static struct zphoc_draggable_layer_surface_v1 *drag_surface;
static struct wl_output *wl_output;

struct wl_surface *wl_surface;
struct wl_egl_window *egl_window;
struct wlr_egl_surface *egl_surface;
struct wl_callback *frame_callback;

static uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
static uint32_t anchor;
static uint32_t width = 256, height = 256;
static uint32_t handle;
static int32_t  unfolded_margin;
static int32_t  folded_margin;
static uint32_t exclusive;
static double   threshold = 1.0;
static bool     run_display = true;
static int      cur_x = -1, cur_y = -1;
static int      buttons;

struct wl_cursor_image *cursor_image;
struct wl_surface *cursor_surface, *input_surface;

static struct {
  struct timespec last_frame;
  float           color[3];
} demo;

typedef enum {
  folded = 0,
  dragged,
  unfolded,
} DragState;
static DragState drag_state;

static void draw (void);

static void
touch_handle_down (void *data, struct wl_touch *wl_touch_,
                   uint32_t serial, uint32_t time, struct wl_surface *surface,
                   int32_t id, wl_fixed_t x, wl_fixed_t y)
{
  g_debug("%s", __func__);
}

static void
touch_handle_up (void *data, struct wl_touch *wl_touch_,
                 uint32_t serial, uint32_t time, int32_t id)
{
  g_debug("%s", __func__);
}

static void
touch_handle_motion (void *data, struct wl_touch *wl_touch_,
                     uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
}

static void
touch_handle_frame (void *data, struct wl_touch *wl_touch_)
{
}

static void
touch_handle_cancel (void *data, struct wl_touch *wl_touch_)
{
  g_debug("%s", __func__);
}

static void
touch_handle_shape (void *data, struct wl_touch *wl_touch_,
                    int32_t id, wl_fixed_t major, wl_fixed_t minor)
{
}

static void
touch_handle_orientation (void *data, struct wl_touch *wl_touch_,
                          int32_t id, wl_fixed_t orientation)
{
}

static struct wl_touch_listener touch_listener = {
  .down = touch_handle_down,
  .up = touch_handle_up,
  .motion = touch_handle_motion,
  .frame = touch_handle_frame,
  .cancel = touch_handle_cancel,
  .shape = touch_handle_shape,
  .orientation = touch_handle_orientation,
};

static void
surface_frame_callback (void *data, struct wl_callback *cb, uint32_t time)
{
  wl_callback_destroy (cb);
  frame_callback = NULL;
  draw ();
}

static struct wl_callback_listener frame_listener = {
  .done = surface_frame_callback
};

static void
draw (void)
{
  eglMakeCurrent (egl_display, egl_surface, egl_surface, egl_context);
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  switch (drag_state) {
  case folded:
    demo.color[0] = 1.0;
    demo.color[1] = 0.0;
    demo.color[2] = 0.0;
    break;
  case dragged:
    demo.color[0] = 0.0;
    demo.color[1] = 1.0;
    demo.color[2] = 0.0;
    break;
  case unfolded:
    demo.color[0] = 0.0;
    demo.color[1] = 0.0;
    demo.color[2] = 1.0;
    break;
  default:
    g_assert_not_reached ();
  }

  glViewport (0, 0, width, height);
  if (false && buttons) {
    glClearColor (1, 1, 1, 1.0);
  } else {
    glClearColor (demo.color[0], demo.color[1], demo.color[2], 1.0);
  }
  glClear (GL_COLOR_BUFFER_BIT);

  if (cur_x != -1 && cur_y != -1) {
    glEnable (GL_SCISSOR_TEST);
    glScissor (cur_x, height - cur_y, 5, 5);
    glClearColor (0, 0, 0, 1);
    glClear (GL_COLOR_BUFFER_BIT);
    glDisable (GL_SCISSOR_TEST);
  }

  frame_callback = wl_surface_frame (wl_surface);
  wl_callback_add_listener (frame_callback, &frame_listener, NULL);

  eglSwapBuffers (egl_display, egl_surface);

  demo.last_frame = ts;
}

static void
layer_surface_configure (void *data,
                         struct zwlr_layer_surface_v1 *surface,
                         uint32_t serial, uint32_t w, uint32_t h)
{
  width = w;
  height = h;
  if (egl_window) {
    wl_egl_window_resize (egl_window, width, height, 0, 0);
  }
  zwlr_layer_surface_v1_ack_configure (surface, serial);
}

static void
layer_surface_closed (void                         *data,
                      struct zwlr_layer_surface_v1 *surface)
{
  eglDestroySurface (egl_display, egl_surface);
  wl_egl_window_destroy (egl_window);
  zwlr_layer_surface_v1_destroy (surface);
  wl_surface_destroy (wl_surface);
  run_display = false;
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  .configure = layer_surface_configure,
  .closed = layer_surface_closed,
};

static void
wl_pointer_enter (void *data, struct wl_pointer *wl_pointer,
                  uint32_t serial, struct wl_surface *surface,
                  wl_fixed_t surface_x, wl_fixed_t surface_y)
{
  struct wl_cursor_image *image = cursor_image;

  wl_surface_attach (cursor_surface,
                     wl_cursor_image_get_buffer (image), 0, 0);
  wl_surface_damage (cursor_surface, 1, 0,
                     image->width, image->height);
  wl_surface_commit (cursor_surface);
  wl_pointer_set_cursor (wl_pointer, serial, cursor_surface,
                         image->hotspot_x, image->hotspot_y);
  input_surface = surface;
}

static void
wl_pointer_leave (void *data, struct wl_pointer *wl_pointer,
                  uint32_t serial, struct wl_surface *surface)
{
  cur_x = cur_y = -1;
  buttons = 0;
}

static void
wl_pointer_motion (void *data, struct wl_pointer *wl_pointer,
                   uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
  cur_x = wl_fixed_to_int (surface_x);
  cur_y = wl_fixed_to_int (surface_y);
}

static void
wl_pointer_button (void *data, struct wl_pointer *wl_pointer,
                   uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
  if (input_surface == wl_surface) {
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
      buttons++;
    } else {
      buttons--;
    }
  } else {
    g_assert (false && "Unknown surface");
  }
}

static void
wl_pointer_axis (void *data, struct wl_pointer *wl_pointer,
                 uint32_t time, uint32_t axis, wl_fixed_t value)
{
  // Who cares
}

static void
wl_pointer_frame (void *data, struct wl_pointer *wl_pointer)
{
  // Who cares
}

static void
wl_pointer_axis_source (void *data, struct wl_pointer *wl_pointer,
                        uint32_t axis_source)
{
  // Who cares
}

static void
wl_pointer_axis_stop (void *data, struct wl_pointer *wl_pointer,
                      uint32_t time, uint32_t axis)
{
  // Who cares
}

static void
wl_pointer_axis_discrete (void *data, struct wl_pointer *wl_pointer,
                          uint32_t axis, int32_t discrete)
{
  // Who cares
}

struct wl_pointer_listener pointer_listener = {
  .enter = wl_pointer_enter,
  .leave = wl_pointer_leave,
  .motion = wl_pointer_motion,
  .button = wl_pointer_button,
  .axis = wl_pointer_axis,
  .frame = wl_pointer_frame,
  .axis_source = wl_pointer_axis_source,
  .axis_stop = wl_pointer_axis_stop,
  .axis_discrete = wl_pointer_axis_discrete,
};

static void
wl_keyboard_keymap (void *data, struct wl_keyboard *wl_keyboard,
                    uint32_t format, int32_t fd, uint32_t size)
{
  // Who cares
}

static void
wl_keyboard_enter (void *data, struct wl_keyboard *wl_keyboard,
                   uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
  g_debug ("Keyboard enter");
}

static void
wl_keyboard_leave (void *data, struct wl_keyboard *wl_keyboard,
                   uint32_t serial, struct wl_surface *surface)
{
  g_debug ("Keyboard leave");
}

static void
wl_keyboard_key (void *data, struct wl_keyboard *wl_keyboard,
                 uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
  g_debug ("Key event: %d %d", key, state);
}

static void
wl_keyboard_modifiers (void *data, struct wl_keyboard *wl_keyboard,
                       uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
                       uint32_t mods_locked, uint32_t group)
{
  // Who cares
}

static void
wl_keyboard_repeat_info (void *data, struct wl_keyboard *wl_keyboard,
                         int32_t rate, int32_t delay)
{
  // Who cares
}

static struct wl_keyboard_listener keyboard_listener = {
  .keymap = wl_keyboard_keymap,
  .enter = wl_keyboard_enter,
  .leave = wl_keyboard_leave,
  .key = wl_keyboard_key,
  .modifiers = wl_keyboard_modifiers,
  .repeat_info = wl_keyboard_repeat_info,
};

static void
seat_handle_capabilities (void *data, struct wl_seat *wl_seat,
                          enum wl_seat_capability caps)
{
  if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
    pointer = wl_seat_get_pointer (wl_seat);
    wl_pointer_add_listener (pointer, &pointer_listener, NULL);
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
    keyboard = wl_seat_get_keyboard (wl_seat);
    wl_keyboard_add_listener (keyboard, &keyboard_listener, NULL);
  }
  if ((caps & WL_SEAT_CAPABILITY_TOUCH) && touch == NULL) {
    touch = wl_seat_get_touch (wl_seat);
    wl_touch_add_listener (touch, &touch_listener, NULL);
  }
}

static void
seat_handle_name (void *data, struct wl_seat *wl_seat,
                  const char *name)
{
  // Who cares
}

const struct wl_seat_listener seat_listener = {
  .capabilities = seat_handle_capabilities,
  .name = seat_handle_name,
};


static void
drag_surface_handle_drag_end (void                                    *data,
                              struct zphoc_draggable_layer_surface_v1 *drag_surface_,
                              uint32_t                                 state)
{
  g_assert (drag_surface_ == drag_surface);

  if (state == 0)
    drag_state = folded;
  else
    drag_state = unfolded;
}


static void
drag_surface_handle_dragged (void                                    *data,
                             struct zphoc_draggable_layer_surface_v1 *drag_surface_,
                             int32_t                                  margin)
{
  g_assert (drag_surface_ == drag_surface);

  g_debug ("Surface margin: %d", margin);
  drag_state = dragged;
}


const struct zphoc_draggable_layer_surface_v1_listener drag_surface_listener = {
  .drag_end = drag_surface_handle_drag_end,
  .dragged = drag_surface_handle_dragged,
};


static void
handle_global (void *data, struct wl_registry *registry,
               uint32_t name, const char *interface, uint32_t version)
{
  if (strcmp (interface, wl_compositor_interface.name) == 0) {
    compositor = wl_registry_bind (registry, name,
                                   &wl_compositor_interface, 1);
  } else if (strcmp (interface, wl_shm_interface.name) == 0) {
    shm = wl_registry_bind (registry, name,
                            &wl_shm_interface, 1);
  } else if (strcmp (interface, "wl_output") == 0) {
    if (!wl_output) {
      wl_output = wl_registry_bind (registry, name,
                                    &wl_output_interface, 1);
    }
  } else if (strcmp (interface, wl_seat_interface.name) == 0) {
    seat = wl_registry_bind (registry, name,
                             &wl_seat_interface, 1);
    wl_seat_add_listener (seat, &seat_listener, NULL);
  } else if (strcmp (interface, zwlr_layer_shell_v1_interface.name) == 0) {
    layer_shell = wl_registry_bind (
      registry, name, &zwlr_layer_shell_v1_interface, 1);
  } else if (strcmp (interface, zphoc_layer_shell_effects_v1_interface.name) == 0) {
    layer_shell_effects = wl_registry_bind (registry, name,
                                            &zphoc_layer_shell_effects_v1_interface, version);
  }
}

static void
handle_global_remove (void *data, struct wl_registry *registry,
                      uint32_t name)
{
  // who cares
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = handle_global_remove,
};

int
main (int argc, char **argv)
{
  char *namespace = "phoc-lse";
  bool found;
  int c;

  while ((c = getopt (argc, argv, "u:f:e:w:h:H:l:a:t:")) != -1) {
    switch (c) {
    case 'u':
      unfolded_margin = atoi (optarg);
      break;
    case 'f':
      folded_margin = atoi (optarg);
      break;
    case 'e':
      exclusive = atoi (optarg);
      break;
    case 'w':
      width = atoi (optarg);
      break;
    case 'h':
      height = atoi (optarg);
      break;
    case 'H':
      handle = atoi (optarg);
      break;
    case 'l': {
      struct {
        char                          *name;
        enum zwlr_layer_shell_v1_layer value;
      } layers[] = {
        { "background", ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND },
        { "bottom", ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM },
        { "top", ZWLR_LAYER_SHELL_V1_LAYER_TOP },
        { "overlay", ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY },
      };
      found = false;
      for (size_t i = 0; i < sizeof (layers) / sizeof (layers[0]); ++i) {
        if (strcmp (optarg, layers[i].name) == 0) {
          layer = layers[i].value;
          found = true;
          break;
        }
      }
      if (!found) {
        g_critical ("invalid layer %s", optarg);
        return 1;
      }
      break;
    }
    case 'a': {
      struct {
        char    *name;
        uint32_t value;
      } anchors[] = {
        { "top", ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP },
        { "bottom", ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM },
        { "left", ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT },
        { "right", ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT },
      };
      found = false;
      for (size_t i = 0; i < sizeof (anchors) / sizeof (anchors[0]); ++i) {
        if (strcmp (optarg, anchors[i].name) == 0) {
          anchor |= anchors[i].value;
          found = true;
          break;
        }
      }
      if (!found) {
        g_critical ("invalid anchor %s", optarg);
        return 1;
      }
      break;
    }
    case 't':
      threshold = atof (optarg);
      break;
    default:
      break;
    }
  }

  display = wl_display_connect (NULL);
  if (display == NULL) {
    g_critical ("Failed to create display");
    return 1;
  }

  struct wl_registry *registry = wl_display_get_registry (display);

  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display);

  if (compositor == NULL) {
    g_critical ("wl_compositor not available");
    return 1;
  }
  if (shm == NULL) {
    g_critical ("wl_shm not available");
    return 1;
  }
  if (layer_shell == NULL) {
    g_critical ("layer_shell not available");
    return 1;
  }
  if (layer_shell_effects == NULL) {
    g_critical ("layer_shell_effects not available");
    return 1;
  }

  struct wl_cursor_theme *cursor_theme =
    wl_cursor_theme_load (NULL, 16, shm);

  g_assert (cursor_theme);
  struct wl_cursor *cursor =
    wl_cursor_theme_get_cursor (cursor_theme, "crosshair");

  if (cursor == NULL)
    cursor = wl_cursor_theme_get_cursor (cursor_theme, "left_ptr");
  g_assert (cursor);
  cursor_image = cursor->images[0];

  cursor = wl_cursor_theme_get_cursor (cursor_theme, "tcross");
  if (cursor == NULL)
    cursor = wl_cursor_theme_get_cursor (cursor_theme, "left_ptr");
  g_assert (cursor);

  cursor_surface = wl_compositor_create_surface (compositor);
  g_assert (cursor_surface);

  egl_init (display);

  wl_surface = wl_compositor_create_surface (compositor);
  g_assert (wl_surface);

  layer_surface = zwlr_layer_shell_v1_get_layer_surface (layer_shell,
                                                         wl_surface, wl_output, layer, namespace);
  g_assert (layer_surface);
  zwlr_layer_surface_v1_set_size (layer_surface, width, height);
  zwlr_layer_surface_v1_set_anchor (layer_surface, anchor);
  zwlr_layer_surface_v1_add_listener (layer_surface,
                                      &layer_surface_listener, layer_surface);
  wl_surface_commit (wl_surface);
  wl_display_roundtrip (display);

  drag_surface = zphoc_layer_shell_effects_v1_get_draggable_layer_surface (layer_shell_effects,
                                                                           layer_surface);
  g_assert (drag_surface);
  zphoc_draggable_layer_surface_v1_add_listener (drag_surface, &drag_surface_listener, NULL);
  zphoc_draggable_layer_surface_v1_set_margins (drag_surface, folded_margin, unfolded_margin);
  zphoc_draggable_layer_surface_v1_set_threshold (drag_surface, wl_fixed_from_double (threshold));
  zphoc_draggable_layer_surface_v1_set_exclusive (drag_surface, exclusive);
  zphoc_draggable_layer_surface_v1_set_state (drag_surface,
                                              ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED);
  if (handle) {
    zphoc_draggable_layer_surface_v1_set_drag_mode (drag_surface,
                                                    ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_MODE_HANDLE);
    zphoc_draggable_layer_surface_v1_set_drag_handle (drag_surface, handle);
  }
  wl_surface_commit (wl_surface);


  egl_window = wl_egl_window_create (wl_surface, width, height);
  g_assert (egl_window);
  egl_surface = eglCreatePlatformWindowSurfaceEXT (egl_display, egl_config, egl_window, NULL);
  g_assert (egl_surface != EGL_NO_SURFACE);

  wl_display_roundtrip (display);
  draw ();

  while (wl_display_dispatch (display) != -1 && run_display) {
    // This space intentionally left blank
  }

  wl_cursor_theme_destroy (cursor_theme);
  return 0;
}
