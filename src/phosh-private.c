/*
 * Copyright (C) 2019,2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-phosh-private"

#include "phoc-config.h"
#include "phoc-enums.h"
#include "phosh-private.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/config.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/util/log.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_matrix.h>
#include <phosh-private-protocol.h>
#include <wlr-screencopy-unstable-v1-protocol.h>
#include "server.h"
#include "desktop.h"
#include "render.h"
#include "utils.h"
#include "seat.h"
#include "switch.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <drm_fourcc.h>

/* help older (0.8.2) libxkbcommon */
#ifndef XKB_KEY_XF86RotationLockToggle
# define XKB_KEY_XF86RotationLockToggle 0x1008FFB7
#endif

/**
 * PhocPhoshPrivate:
 *
 * Private protocol to interface with phosh
 */

enum {
  PROP_0,
  PROP_SHELL_STATE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhocPhoshPrivate {
  GObject parent;

  guint32 version;
  struct wl_resource* resource;
  struct wl_global *global;
  GList *keyboard_events;
  guint last_action_id;
  GList *startup_trackers;
  PhocPhoshPrivateShellState state;
};
G_DEFINE_TYPE (PhocPhoshPrivate, phoc_phosh_private, G_TYPE_OBJECT)

typedef struct {
  GHashTable *subscribed_accelerators;
  struct wl_resource *resource;
  PhocPhoshPrivate *phosh;
} PhocPhoshPrivateKeyboardEventData;

typedef struct {
  struct wl_resource *resource, *toplevel;
  struct phosh_private *phosh;

  enum wl_shm_format format;
  uint32_t width;
  uint32_t height;
  uint32_t stride;

  struct wlr_buffer *buffer;

  PhocView *view;
} PhocPhoshPrivateScreencopyFrame;

typedef struct {
  struct wl_resource *resource;
  PhocPhoshPrivate   *phosh;
} PhocPhoshPrivateStartupTracker;

static PhocPhoshPrivate *phoc_phosh_private_from_resource (struct wl_resource *resource);
static PhocPhoshPrivateKeyboardEventData *phoc_phosh_private_keyboard_event_from_resource (struct wl_resource *resource);
static PhocPhoshPrivateScreencopyFrame *phoc_phosh_private_screencopy_frame_from_resource(struct wl_resource *resource);
static PhocPhoshPrivateStartupTracker *phoc_phosh_private_startup_tracker_from_resource(struct wl_resource *resource);

#define PHOSH_PRIVATE_VERSION 7


static void
phoc_phosh_private_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhocPhoshPrivate *self = PHOC_PHOSH_PRIVATE (object);

  switch (property_id) {
  case PROP_SHELL_STATE:
    g_value_set_enum (value, self->state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
handle_rotate_display (struct wl_client   *client,
                       struct wl_resource *resource,
                       struct wl_resource *surface_resource,
                       uint32_t            degrees)
{
  wl_resource_post_error (resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                          "Use wlr-output-management protocol instead");
}


static void
handle_get_xdg_switcher (struct wl_client   *client,
                         struct wl_resource *phosh_private_resource,
                         uint32_t            id)
{
  int version = wl_resource_get_version (phosh_private_resource);
  struct wl_resource *resource  = wl_resource_create (client, &phosh_private_xdg_switcher_interface,
                                                      version, id);

  wl_resource_post_error (resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                          "Use wlr-toplevel-management protocol instead");
}

static void
phoc_phosh_private_keyboard_event_destroy (PhocPhoshPrivateKeyboardEventData *kbevent)
{
  PhocPhoshPrivate *phosh;

  if (kbevent == NULL)
    return;

  g_debug ("Destroying private_keyboard_event %p (res %p)", kbevent, kbevent->resource);
  phosh = kbevent->phosh;
  g_hash_table_remove_all (kbevent->subscribed_accelerators);
  g_hash_table_unref (kbevent->subscribed_accelerators);
  wl_resource_set_user_data (kbevent->resource, NULL);
  phosh->keyboard_events = g_list_remove (phosh->keyboard_events, kbevent);
  g_free (kbevent);
}

static void
phoc_phosh_private_keyboard_event_handle_resource_destroy (struct wl_resource *resource)
{
  PhocPhoshPrivateKeyboardEventData *kbevent = phoc_phosh_private_keyboard_event_from_resource (resource);

  phoc_phosh_private_keyboard_event_destroy (kbevent);
}

static bool
phoc_phosh_private_keyboard_event_accelerator_is_registered (PhocKeyCombo                      *combo,
                                                             PhocPhoshPrivateKeyboardEventData *kbevent)
{
  gint64 key = ((gint64) combo->modifiers << 32) | combo->keysym;
  gpointer ret = g_hash_table_lookup (kbevent->subscribed_accelerators, &key);
  g_debug ("Accelerator is registered: Lookup -> %p", ret);
  return (ret != NULL);
}

static bool
phoc_phosh_private_accelerator_already_subscribed (PhocKeyCombo *combo)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocPhoshPrivate *phosh = phoc_desktop_get_phosh_private (desktop);
  GList *l;
  PhocPhoshPrivateKeyboardEventData *kbevent;

  for (l = phosh->keyboard_events; l != NULL; l = l->next) {
    kbevent = (PhocPhoshPrivateKeyboardEventData *)l->data;
    if (phoc_phosh_private_keyboard_event_accelerator_is_registered (combo, kbevent))
      return true;
  }

  return false;
}


static bool
keysym_is_subscribeable (PhocKeyCombo *combo)
{
  /* Allow to bind all keys with modifiers that aren't just shift/caps */
  if (combo->modifiers >= WLR_MODIFIER_CTRL)
    return true;

  /* keys on multi media keyboards */
  if (combo->keysym >= XKB_KEY_XF86MonBrightnessUp && combo->keysym <= XKB_KEY_XF86RotationLockToggle)
    return true;

  /* more of these but from a block of mostly deprecated symbols so we add them
   * explicitly */
  switch (combo->keysym) {
  case XKB_KEY_XF86Screensaver:
    return true;
  default:
    break;
  }

  if (combo->keysym == XKB_KEY_Super_L || combo->keysym == XKB_KEY_Super_R)
    return true;

  /* misc functions */
  if (combo->keysym >= XKB_KEY_Select && combo->keysym <= XKB_KEY_Num_Lock)
    return true;

  return false;
}


static void
phoc_phosh_private_keyboard_event_grab_accelerator_request (struct wl_client   *wl_client,
                                                            struct wl_resource *resource,
                                                            const char         *accelerator)
{
  guint new_action_id;
  gint64 *new_key;

  PhocPhoshPrivateKeyboardEventData *kbevent = phoc_phosh_private_keyboard_event_from_resource (resource);
  g_autofree PhocKeyCombo *combo = phoc_parse_accelerator (accelerator);

  if (kbevent == NULL)
    return;

  if (combo == NULL) {
    g_debug ("Failed to parse accelerator %s", accelerator);

    phosh_private_keyboard_event_send_grab_failed_event (resource,
                                                         accelerator,
                                                         PHOSH_PRIVATE_KEYBOARD_EVENT_ERROR_INVALID_KEYSYM);
    return;
  }

  if (phoc_phosh_private_accelerator_already_subscribed (combo)) {
    g_debug ("Accelerator %s already subscribed to!", accelerator);

    phosh_private_keyboard_event_send_grab_failed_event (resource,
                                                         accelerator,
                                                         PHOSH_PRIVATE_KEYBOARD_EVENT_ERROR_ALREADY_SUBSCRIBED);
    return;
  }

/*
  if (!keysym_is_subscribeable (combo)) {
    g_debug ("Requested keysym %s is not subscribeable!", accelerator);

    phosh_private_keyboard_event_send_grab_failed_event (resource,
                                                         accelerator,
                                                         PHOSH_PRIVATE_KEYBOARD_EVENT_ERROR_INVALID_KEYSYM);
    return;
  }
*/

  new_action_id = kbevent->phosh->last_action_id++;

  /* detect wrap-around and make sure we fail from here on out */
  if (new_action_id == 0) {
    g_debug ("Action ID wrap-around detected while trying to subscribe %s", accelerator);
    phosh_private_keyboard_event_send_grab_failed_event (resource,
                                                         accelerator,
                                                         PHOSH_PRIVATE_KEYBOARD_EVENT_ERROR_MISC_ERROR);
    kbevent->phosh->last_action_id--;
    return;
  }

  new_key = (gint64 *) g_malloc (sizeof (gint64));
  *new_key = ((gint64) combo->modifiers << 32) | combo->keysym;

  /* subscribed accelerators of kbevent */
  g_hash_table_insert (kbevent->subscribed_accelerators,
                       new_key, GUINT_TO_POINTER (new_action_id));

  phosh_private_keyboard_event_send_grab_success_event (resource,
                                                        accelerator,
                                                        new_action_id);

  g_debug ("Registered accelerator %s (sym %d mod %d) on phosh_private_keyboard_event %p (client %p)",
           accelerator, combo->keysym, combo->modifiers, kbevent, wl_client);

}


static void
phoc_phosh_private_keyboard_event_ungrab_accelerator_request (struct wl_client *client,
                                                              struct wl_resource *resource,
                                                              uint32_t action_id)
{
  GHashTableIter iter;
  gpointer key, value, found = NULL;
  PhocPhoshPrivateKeyboardEventData *kbevent = phoc_phosh_private_keyboard_event_from_resource (resource);

  g_debug ("Ungrabbing accelerator %d", action_id);
  g_hash_table_iter_init (&iter, kbevent->subscribed_accelerators);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (GPOINTER_TO_INT (value) == action_id) {
      found = key;
      break;
    }
  }

  if (found) {
    g_hash_table_remove (kbevent->subscribed_accelerators, key);
    phosh_private_keyboard_event_send_ungrab_success_event (resource,
                                                            action_id);

  } else {
    phosh_private_keyboard_event_send_ungrab_failed_event (resource,
                                                           action_id,
                                                           PHOSH_PRIVATE_KEYBOARD_EVENT_ERROR_INVALID_ARGUMENT);
  }
}


static void
phoc_phosh_private_keyboard_event_handle_destroy (struct wl_client   *client,
                                                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}


static const struct phosh_private_keyboard_event_interface phoc_phosh_private_keyboard_event_impl = {
  .grab_accelerator_request = phoc_phosh_private_keyboard_event_grab_accelerator_request,
  .ungrab_accelerator_request = phoc_phosh_private_keyboard_event_ungrab_accelerator_request,
  .destroy = phoc_phosh_private_keyboard_event_handle_destroy
};


static void
handle_get_keyboard_event (struct wl_client   *client,
                           struct wl_resource *phosh_private_resource,
                           uint32_t            id)
{
  PhocPhoshPrivateKeyboardEventData *kbevent = g_new0 (PhocPhoshPrivateKeyboardEventData, 1);

  if (kbevent == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  int version = wl_resource_get_version (phosh_private_resource);
  kbevent->resource = wl_resource_create (client, &phosh_private_keyboard_event_interface, version, id);
  if (kbevent->resource == NULL) {
    g_free (kbevent);
    wl_client_post_no_memory (client);
    return;
  }

  kbevent->subscribed_accelerators = g_hash_table_new_full (g_int64_hash,
                                                            g_int64_equal,
                                                            g_free, NULL);
  if (kbevent->subscribed_accelerators == NULL) {
    wl_resource_destroy (kbevent->resource);
    g_free (kbevent);
    wl_client_post_no_memory (client);
    return;
  }

  PhocPhoshPrivate *phosh_private = phoc_phosh_private_from_resource (phosh_private_resource);

  phosh_private->keyboard_events = g_list_append (phosh_private->keyboard_events, kbevent);

  g_debug ("new phosh_private_keyboard_event %p (res %p)", kbevent, kbevent->resource);
  wl_resource_set_implementation (kbevent->resource,
                                  &phoc_phosh_private_keyboard_event_impl,
                                  kbevent,
                                  phoc_phosh_private_keyboard_event_handle_resource_destroy);

  kbevent->phosh = phosh_private;
}


static void
phosh_private_screencopy_frame_handle_resource_destroy (struct wl_resource *resource)
{
  PhocPhoshPrivateScreencopyFrame *frame = phoc_phosh_private_screencopy_frame_from_resource (resource);

  g_debug ("Destroying private_screencopy_frame %p (res %p)", frame, frame->resource);
  if (frame->view)
    g_signal_handlers_disconnect_by_data (frame->view, frame);

  free (frame);
}


static void
on_surface_destroy (PhocView *view, PhocPhoshPrivateScreencopyFrame *frame)
{
  g_assert (PHOC_IS_VIEW (view));

  g_signal_handlers_disconnect_by_data (frame->view, frame);
  frame->view = NULL;
}


static void
thumbnail_frame_handle_copy (struct wl_client   *wl_client,
                             struct wl_resource *frame_resource,
                             struct wl_resource *buffer_resource)
{
  PhocServer *server = phoc_server_get_default ();
  PhocRenderer *self = phoc_server_get_renderer (server);
  PhocPhoshPrivateScreencopyFrame *frame;
  struct wlr_shm_attributes attribs;

  frame = phoc_phosh_private_screencopy_frame_from_resource (frame_resource);
  g_return_if_fail (frame);

  if (frame->buffer != NULL) {
    wl_resource_post_error (frame->resource,
                            ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED,
                            "frame already used");
    return;
  }

  if (!frame->view) {
    zwlr_screencopy_frame_v1_send_failed (frame->resource);
    return;
  }

  frame->buffer = wlr_buffer_try_from_resource (buffer_resource);
  if (frame->buffer == NULL) {
    wl_resource_post_error (frame->resource,
                            ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
                            "unsupported buffer type");
    return;
  }

  if (!wlr_buffer_get_shm (frame->buffer, &attribs)) {
    wl_resource_post_error (frame->resource,
                            ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
                            "unsupported buffer type");
    goto unlock_buffer;
  }

  if (attribs.width != frame->width ||
      attribs.height != frame->height || attribs.stride != frame->stride) {
    wl_resource_post_error (frame->resource,
                            ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
                            "invalid buffer attributes");
    goto unlock_buffer;
  }

  PhocView *view = frame->view;
  g_signal_handlers_disconnect_by_data (frame->view, frame);
  frame->view = NULL;

  if (!phoc_renderer_render_view_to_buffer (self, view, frame->buffer)) {
    zwlr_screencopy_frame_v1_send_failed (frame->resource);
    goto unlock_buffer;
  }

  zwlr_screencopy_frame_v1_send_flags (frame->resource, 0);

  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  uint32_t tv_sec_hi = (sizeof(now.tv_sec) > 4) ? now.tv_sec >> 32 : 0;
  uint32_t tv_sec_lo = now.tv_sec & 0xFFFFFFFF;
  zwlr_screencopy_frame_v1_send_ready (frame->resource, tv_sec_hi, tv_sec_lo, now.tv_nsec);

unlock_buffer:
  wlr_buffer_unlock (frame->buffer);
}

static void
thumbnail_frame_handle_copy_with_damage (struct wl_client   *wl_client,
                                         struct wl_resource *frame_resource,
                                         struct wl_resource *buffer_resource)
{
  // XXX: unimplemented
  (void)wl_client;
  (void)buffer_resource;
  zwlr_screencopy_frame_v1_send_failed (frame_resource);
}

static void
thumbnail_frame_handle_destroy (struct wl_client   *wl_client,
                                struct wl_resource *frame_resource)
{
  wl_resource_destroy (frame_resource);
}

static const struct zwlr_screencopy_frame_v1_interface phoc_phosh_private_screencopy_frame_impl = {
  .copy = thumbnail_frame_handle_copy,
  .destroy = thumbnail_frame_handle_destroy,
  .copy_with_damage = thumbnail_frame_handle_copy_with_damage,
};

static uint32_t
get_preferred_read_format (void)
{
  const char *exts_str = (const char *)glGetString(GL_EXTENSIONS);
  if (exts_str != NULL && strstr(exts_str, "GL_EXT_read_format_bgra"))
    return DRM_FORMAT_ARGB8888;

  return DRM_FORMAT_ABGR8888;
}

static void
handle_get_thumbnail (struct wl_client *client,
                      struct wl_resource *phosh_private_resource,
                      uint32_t id,
                      struct wl_resource *toplevel,
                      uint32_t max_width,
                      uint32_t max_height)
{
  static uint32_t preferred_read_format = 0;
  PhocPhoshPrivateScreencopyFrame *frame = g_new0 (PhocPhoshPrivateScreencopyFrame, 1);

  if (frame == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  if (!preferred_read_format)
    preferred_read_format = get_preferred_read_format();

  int version = wl_resource_get_version (phosh_private_resource);
  frame->resource = wl_resource_create (client, &zwlr_screencopy_frame_v1_interface, version, id);
  if (frame->resource == NULL) {
    free (frame);
    wl_client_post_no_memory (client);
    return;
  }

  g_debug ("new phosh_private_screencopy_frame %p (res %p)", frame, frame->resource);
  wl_resource_set_implementation (frame->resource,
                                  &phoc_phosh_private_screencopy_frame_impl,
                                  frame,
                                  phosh_private_screencopy_frame_handle_resource_destroy);

  struct wlr_foreign_toplevel_handle_v1 *toplevel_handle = wl_resource_get_user_data (toplevel);
  if (!toplevel_handle) {
    zwlr_screencopy_frame_v1_send_failed (frame->resource);
    return;
  }

  PhocView *view = toplevel_handle->data;
  if (!view) {
    zwlr_screencopy_frame_v1_send_failed (frame->resource);
    return;
  }

  frame->toplevel = toplevel;
  frame->view = view;
  g_signal_connect (view, "surface-destroy", G_CALLBACK (on_surface_destroy), frame);

  // We hold to the current surface size even though it may change before
  // the frame is actually rendered. wlr-screencopy doesn't give much
  // flexibility there, but since the worst thing that may happen in such
  // case is a rescaled thumbnail with wrong aspect ratio we take the liberty
  // to ignore it, at least for now.
  struct wlr_box box;
  phoc_view_get_box (view, &box);

  frame->format = preferred_read_format;
  frame->width = box.width * view->wlr_surface->current.scale;
  frame->height = box.height * view->wlr_surface->current.scale;

  double scale = 1.0;
  if (max_width && frame->width > max_width) {
    scale = max_width / (double)frame->width;
  }
  if (max_height && frame->height > max_height) {
    scale = fmin (scale, max_height / (double)frame->height);
  }
  frame->width *= scale;
  frame->height *= scale;

  frame->width = frame->width ?: 1;
  frame->height = frame->height ?: 1;

  frame->stride = 4 * frame->width;

  zwlr_screencopy_frame_v1_send_buffer (frame->resource, frame->format,
                                        frame->width, frame->height, frame->stride);
}


static void
phoc_phosh_private_startup_tracker_handle_resource_destroy (struct wl_resource *resource)
{
  PhocPhoshPrivateStartupTracker *tracker = phoc_phosh_private_startup_tracker_from_resource (resource);
  PhocPhoshPrivate *phosh;

  if (tracker == NULL)
    return;

  g_debug ("Destroying startup_tracker %p (res %p)", tracker, tracker->resource);
  phosh = tracker->phosh;
  wl_resource_set_user_data (tracker->resource, NULL);
  phosh->startup_trackers = g_list_remove (phosh->startup_trackers, tracker);
  g_free (tracker);
}


static void
phoc_phosh_private_startup_tracker_handle_destroy (struct wl_client   *client,
                                                   struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}


static const struct phosh_private_startup_tracker_interface phoc_phosh_private_startup_tracker_impl = {
  .destroy = phoc_phosh_private_startup_tracker_handle_destroy
};


static void
handle_get_startup_tracker (struct wl_client   *client,
                            struct wl_resource *phosh_private_resource,
                            uint32_t            id)
{
  PhocPhoshPrivateStartupTracker *tracker = g_new0 (PhocPhoshPrivateStartupTracker, 1);
  PhocPhoshPrivate *phosh_private;

  if (tracker == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  int version = wl_resource_get_version (phosh_private_resource);

  tracker->resource = wl_resource_create (client, &phosh_private_startup_tracker_interface, version, id);
  if (tracker->resource == NULL) {
    g_free (tracker);
    wl_client_post_no_memory (client);
    return;
  }

  phosh_private = phoc_phosh_private_from_resource (phosh_private_resource);
  phosh_private->startup_trackers = g_list_append (phosh_private->startup_trackers, tracker);
  tracker->phosh = phosh_private;

  g_debug ("New phosh_private_startup_tracker %p (res %p)", tracker, tracker->resource);
  wl_resource_set_implementation (tracker->resource,
                                  &phoc_phosh_private_startup_tracker_impl,
                                  tracker,
                                  phoc_phosh_private_startup_tracker_handle_resource_destroy);
}


static void
handle_set_shell_state (struct wl_client               *client,
                        struct wl_resource             *phosh_private_resource,
                        enum phosh_private_shell_state  state)
{
  PhocPhoshPrivate *self = wl_resource_get_user_data (phosh_private_resource);

  g_assert (PHOC_IS_PHOSH_PRIVATE (self));

  g_debug ("Shell state set to %d", state);

  if (self->state == (PhocPhoshPrivateShellState)state)
    return;

  self->state = (PhocPhoshPrivateShellState)state;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHELL_STATE]);
}


static void
phosh_handle_resource_destroy (struct wl_resource *resource)
{
  PhocPhoshPrivate *phosh = wl_resource_get_user_data (resource);

  g_debug ("Destroying phosh %p (res %p)", phosh, resource);
  phosh->resource = NULL;

  g_list_free (phosh->keyboard_events);
  phosh->keyboard_events = NULL;

  phosh->state = PHOC_PHOSH_PRIVATE_SHELL_STATE_UNKNOWN;
  g_object_notify_by_pspec (G_OBJECT (phosh), props[PROP_SHELL_STATE]);
}


static const struct phosh_private_interface phosh_private_impl = {
  handle_rotate_display,       /* unused */
  handle_get_xdg_switcher,     /* unused */
  handle_get_thumbnail,        /* request */
  handle_get_keyboard_event,   /* interface */
  handle_get_startup_tracker,  /* interface */
  handle_set_shell_state,      /* request */
};


bool
phoc_phosh_private_forward_switch_event (guint switch_type, guint switch_state)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocPhoshPrivate *phosh_private = phoc_desktop_get_phosh_private (desktop);

  g_debug ("Forwarding event type %d, state %d", switch_type, switch_state);
  if (phosh_private && phosh_private->resource && switch_type > 0) {
      phosh_private_send_switch_event (phosh_private->resource,
                                       switch_type,
                                       switch_state);

      return true;
  }

  return false;
}


static void
emit_state_changes (PhocPhoshPrivate *phosh_private)
{
  PhocInput *input = phoc_server_get_input (phoc_server_get_default ());
  PhocSwitch *switch_device;
  PhocSeat *seat;

  for (GSList *elem = phoc_input_get_seats (input); elem; elem = elem->next) {
    seat = PHOC_SEAT (elem->data);

    if (!PHOC_IS_SEAT (seat))
        continue;

    g_debug ("KEYPAD: Inside one seat! %p", seat);
    for (GSList *switch_elem = seat->switches; switch_elem; switch_elem = switch_elem->next) {
      switch_device = PHOC_SWITCH (switch_elem->data);

      if (!phoc_switch_is_keypad_slide (switch_device))
        continue;

      uint32_t _switch_state = phoc_switch_get_state (switch_device);

      g_info ("KEYPAD: Found switch details state %d", _switch_state);

      phoc_phosh_private_forward_switch_event (3 /* LIBINPUT_SWITCH_KEYPAD_SLIDE */, _switch_state);
    }
  }
}


static void
phosh_private_bind (struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
  PhocPhoshPrivate *phosh = data;
  struct wl_resource *resource  = wl_resource_create (client, &phosh_private_interface,
                                                      version, id);

  if (phosh->resource) {
    wl_resource_post_error (resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                            "Only a single client can bind to phosh's private protocol");
    return;
  }

  /* FIXME: unsafe, needs client == shell->child.client */
  if (true) {
    g_info ("FIXME: allowing every client to bind as phosh");
    wl_resource_set_implementation (resource,
                                    &phosh_private_impl,
                                    phosh, phosh_handle_resource_destroy);
    phosh->resource = resource;
    g_debug ("Bound client %d with version %d", id, version);
    phosh->version = version;

    /* Sync switch state changes */
    emit_state_changes (phosh);
    return;
  }

  wl_resource_post_error (resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                          "permission to bind phosh denied");
}


static PhocPhoshPrivate *
phoc_phosh_private_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &phosh_private_interface,
                                     &phosh_private_impl));
  return wl_resource_get_user_data (resource);
}


static PhocPhoshPrivateScreencopyFrame *
phoc_phosh_private_screencopy_frame_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zwlr_screencopy_frame_v1_interface,
                                     &phoc_phosh_private_screencopy_frame_impl));
  return wl_resource_get_user_data (resource);
}


static PhocPhoshPrivateKeyboardEventData *
phoc_phosh_private_keyboard_event_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &phosh_private_keyboard_event_interface,
                                     &phoc_phosh_private_keyboard_event_impl));
  return wl_resource_get_user_data (resource);
}


static PhocPhoshPrivateStartupTracker *
phoc_phosh_private_startup_tracker_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &phosh_private_startup_tracker_interface,
                                     &phoc_phosh_private_startup_tracker_impl));
  return wl_resource_get_user_data (resource);
}


static void
phoc_phosh_private_constructed (GObject *object)
{
  PhocPhoshPrivate *self = PHOC_PHOSH_PRIVATE (object);
  struct wl_display *wl_display = phoc_server_get_wl_display (phoc_server_get_default ());

  G_OBJECT_CLASS (phoc_phosh_private_parent_class)->constructed (object);

  g_info ("Initializing phosh private interface");
  self->global = wl_global_create (wl_display, &phosh_private_interface,
                                   PHOSH_PRIVATE_VERSION, self, phosh_private_bind);
}


static void
phoc_phosh_private_finalize (GObject *object)
{
  PhocPhoshPrivate *self = PHOC_PHOSH_PRIVATE (object);

  wl_global_destroy (self->global);

  G_OBJECT_CLASS (phoc_phosh_private_parent_class)->finalize (object);
}


static void
phoc_phosh_private_class_init (PhocPhoshPrivateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phoc_phosh_private_constructed;
  object_class->finalize = phoc_phosh_private_finalize;
  object_class->get_property = phoc_phosh_private_get_property;

  /**
   * PhoshPhocPrivate:shell-state:
   *
   * The attached shell's state
   */
  props[PROP_SHELL_STATE] = g_param_spec_enum ("shell-state",
                                               "",
                                               "",
                                               PHOC_TYPE_PHOSH_PRIVATE_SHELL_STATE,
                                               PHOC_PHOSH_PRIVATE_SHELL_STATE_UNKNOWN,
                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
                                               G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_phosh_private_init (PhocPhoshPrivate *self)
{
  self->last_action_id = 1;
}


PhocPhoshPrivate *
phoc_phosh_private_new (void)
{
  return g_object_new (PHOC_TYPE_PHOSH_PRIVATE, NULL);
}


bool
phoc_phosh_private_forward_keysym (PhocKeyCombo *combo,
                                   uint32_t      timestamp,
                                   bool          pressed)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());
  PhocPhoshPrivate *phosh = phoc_desktop_get_phosh_private (desktop);
  GList *l;
  bool forwarded = false;

  for (l = phosh->keyboard_events; l != NULL; l = l->next) {
    PhocPhoshPrivateKeyboardEventData *kbevent = l->data;
    uint32_t version = wl_resource_get_version (kbevent->resource);

    g_debug("addr of kbevent and res kbev %p res %p", kbevent, kbevent->resource);
    /*  forward the keysym if it is has been subscribed to */
    if (phoc_phosh_private_keyboard_event_accelerator_is_registered (combo, kbevent)) {
      gint64 key = ((gint64)combo->modifiers << 32) | combo->keysym;
      guint action_id = GPOINTER_TO_UINT (g_hash_table_lookup (kbevent->subscribed_accelerators, &key));

      if (pressed) {
        phosh_private_keyboard_event_send_accelerator_activated_event (kbevent->resource,
                                                                       action_id,
                                                                       timestamp);
        forwarded = true;
      } else if (version >= PHOSH_PRIVATE_KEYBOARD_EVENT_ACCELERATOR_RELEASED_EVENT_SINCE_VERSION) {
        phosh_private_keyboard_event_send_accelerator_released_event (kbevent->resource,
                                                                      action_id,
                                                                      timestamp);
        forwarded = true;
      }
    }
  }

  return forwarded;
}

void
phoc_phosh_private_notify_startup_id (PhocPhoshPrivate                           *self,
                                      const char                                 *startup_id,
                                      enum phosh_private_startup_tracker_protocol proto)
{
  g_assert (PHOC_IS_PHOSH_PRIVATE (self));

  /* Nobody bound the protocol */
  if (!self->resource)
    return;

  if (self->version < 6)
    return;

  for (GList *l = self->startup_trackers; l; l = l->next) {
    PhocPhoshPrivateStartupTracker *tracker = (PhocPhoshPrivateStartupTracker *)l->data;

    phosh_private_startup_tracker_send_startup_id (tracker->resource, startup_id, proto, 0);
  }
}


void
phoc_phosh_private_notify_launch (PhocPhoshPrivate                           *self,
                                  const char                                 *startup_id,
                                  enum phosh_private_startup_tracker_protocol proto)
{
  g_assert (PHOC_IS_PHOSH_PRIVATE (self));

  /* Nobody bound the protocol */
  if (!self->resource)
    return;

  if (self->version < 6)
    return;

  for (GList *l = self->startup_trackers; l; l = l->next) {
    PhocPhoshPrivateStartupTracker *tracker = (PhocPhoshPrivateStartupTracker *)l->data;

    phosh_private_startup_tracker_send_launched (tracker->resource, startup_id, proto, 0);
  }
}

PhocPhoshPrivateShellState
phoc_phosh_private_get_shell_state (PhocPhoshPrivate *self)
{
  g_assert (PHOC_IS_PHOSH_PRIVATE (self));

  return self->state;
}


struct wl_global *
phoc_phosh_private_get_global (PhocPhoshPrivate *self)
{
  g_assert (PHOC_IS_PHOSH_PRIVATE (self));

  return self->global;
}
