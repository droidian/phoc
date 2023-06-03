/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Sebastian Krzyszkowiak <sebastian.krzyszkowiak@puri.sm>
 */

#include "testlib.h"
#include "gtk-shell-client-protocol.h"

typedef struct _PhocTestThumbnail
{
  char* title;
  PhocTestBuffer buffer;
  guint32 width, height;
} PhocTestThumbnail;

typedef enum {
  GRAB_STATUS_FAILED = -1,
  GRAB_STATUS_UNKNOWN = 0,
  GRAB_STATUS_OK = 1,
} PhocTestGrabStatus;

typedef struct _PhocTestKeyboardEvent
{
  char *title;
  struct phosh_private_keyboard_event *kbevent;
  PhocTestGrabStatus grab_status;
} PhocTestKeyboardEvent;


static PhocTestScreencopyFrame *
phoc_test_get_thumbnail (PhocTestClientGlobals *globals,
                           guint32 max_width, guint32 max_height, PhocTestForeignToplevel *toplevel)
{
  PhocTestScreencopyFrame *thumbnail = g_malloc0 (sizeof(PhocTestScreencopyFrame));

  struct zwlr_screencopy_frame_v1 *handle = phosh_private_get_thumbnail (globals->phosh, toplevel->handle, max_width, max_height);
  phoc_test_client_capture_frame (globals, thumbnail, handle);

  zwlr_screencopy_frame_v1_destroy (handle);
  return thumbnail;
}

static void
phoc_test_thumbnail_free (PhocTestScreencopyFrame *frame)
{
  phoc_test_buffer_free (&frame->buffer);
  g_free (frame);
}

static gboolean
test_client_phosh_private_thumbnail_simple (PhocTestClientGlobals *globals, gpointer data)
{
  PhocTestXdgToplevelSurface *toplevel_green;
  PhocTestScreencopyFrame *green_thumbnail;

  toplevel_green = phoc_test_xdg_toplevel_new_with_buffer (globals, 0, 0, "green", 0xFF00FF00);
  g_assert_nonnull (toplevel_green);
  phoc_assert_screenshot (globals, "test-phosh-private-thumbnail-simple-1.png");

  green_thumbnail = phoc_test_get_thumbnail (globals, toplevel_green->width, toplevel_green->height, toplevel_green->foreign_toplevel);
  phoc_assert_buffer_equal (&toplevel_green->buffer, &green_thumbnail->buffer);
  phoc_test_thumbnail_free (green_thumbnail);

  phoc_test_xdg_toplevel_free (toplevel_green);
  phoc_assert_screenshot (globals, "empty.png");

  return TRUE;
}

static void
test_phosh_private_thumbnail_simple (void)
{
  PhocTestClientIface iface = {
   .client_run = test_client_phosh_private_thumbnail_simple,
   .debug_flags    = PHOC_SERVER_DEBUG_FLAG_DISABLE_ANIMATIONS,
  };

  /* pixman renderer can work in containers, skip tests otherwise */
  if (g_strcmp0 (g_getenv ("WLR_RENDERER"), "pixman")) {
    g_test_skip ("Not using pixman renderer");
    return;
  }

  phoc_test_client_run (TEST_PHOC_CLIENT_TIMEOUT, &iface, GINT_TO_POINTER (FALSE));
}

static void
keyboard_event_handle_grab_failed (void *data,
                                   struct phosh_private_keyboard_event *kbevent,
                                   const char *accelerator,
                                   uint32_t error)
{
  PhocTestKeyboardEvent *kbe = data;

  g_assert_nonnull (kbevent);
  g_assert (kbe->kbevent == kbevent);

  kbe->grab_status = GRAB_STATUS_FAILED;
}

static void
keyboard_event_handle_grab_success (void *data,
                                   struct phosh_private_keyboard_event *kbevent,
                                   const char *accelerator,
                                   uint32_t action_id)
{
  PhocTestKeyboardEvent *kbe = data;

  g_assert_nonnull (kbevent);
  g_assert (kbe->kbevent == kbevent);

  if (action_id > 0)
    kbe->grab_status = GRAB_STATUS_OK;
}

static const struct phosh_private_keyboard_event_listener keyboard_event_listener = {
   .grab_failed_event = keyboard_event_handle_grab_failed,
   .grab_success_event = keyboard_event_handle_grab_success,
};

static PhocTestKeyboardEvent *
phoc_test_keyboard_event_new (PhocTestClientGlobals *globals,
                              char* title)
{
  PhocTestKeyboardEvent *kbe = g_new0 (PhocTestKeyboardEvent, 1);

  g_assert (phosh_private_get_version (globals->phosh) >= 5);

  kbe->kbevent = phosh_private_get_keyboard_event (globals->phosh);
  kbe->title = title;

  phosh_private_keyboard_event_add_listener (kbe->kbevent, &keyboard_event_listener, kbe);

  return kbe;
}


static void
phoc_test_keyboard_event_free (PhocTestKeyboardEvent *event)
{
  phosh_private_keyboard_event_destroy (event->kbevent);
  g_free (event);
}

#define RAISE_VOL_KEY "XF86AudioRaiseVolume"




static gboolean
test_client_phosh_private_kbevent_simple (PhocTestClientGlobals *globals, gpointer unused)
{
  PhocTestKeyboardEvent *test1;
  PhocTestKeyboardEvent *test2;

  test1 = phoc_test_keyboard_event_new (globals, "test-mediakey-grabbing");
  test2 = phoc_test_keyboard_event_new (globals, "test-invalid-grabbing");

  phosh_private_keyboard_event_grab_accelerator_request (test1->kbevent,
                                                         "XF86AudioLowerVolume");
  /* Not allowed to bind this one: */
  phosh_private_keyboard_event_grab_accelerator_request (test2->kbevent,
                                                         "F9");
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_cmpint (test1->grab_status, ==, GRAB_STATUS_OK);
  g_assert_cmpint (test2->grab_status, ==, GRAB_STATUS_FAILED);

  test1->grab_status = GRAB_STATUS_UNKNOWN;
  test2->grab_status = GRAB_STATUS_UNKNOWN;

  phosh_private_keyboard_event_grab_accelerator_request (test1->kbevent,
                                                         RAISE_VOL_KEY);
  /* Can't bind same key twice: */
  phosh_private_keyboard_event_grab_accelerator_request (test2->kbevent,
                                                         RAISE_VOL_KEY);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_cmpint (test1->grab_status, ==, GRAB_STATUS_OK);
  g_assert_cmpint (test2->grab_status, ==, GRAB_STATUS_FAILED);

  test1->grab_status = GRAB_STATUS_UNKNOWN;
  test2->grab_status = GRAB_STATUS_UNKNOWN;

  /* Allowing to bind a already bound key with an additional accelerator is o.k. */
  phosh_private_keyboard_event_grab_accelerator_request (test1->kbevent,
                                                         "<SHIFT>" RAISE_VOL_KEY);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_cmpint (test1->grab_status, ==, GRAB_STATUS_OK);
  g_assert_cmpint (test2->grab_status, ==, GRAB_STATUS_UNKNOWN);

  test1->grab_status = GRAB_STATUS_UNKNOWN;
  test2->grab_status = GRAB_STATUS_UNKNOWN;

  /* Binding non existing key must fail */
  phosh_private_keyboard_event_grab_accelerator_request (test2->kbevent,
                                                         "does-not-exist");
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_cmpint (test1->grab_status, ==, GRAB_STATUS_UNKNOWN);
  g_assert_cmpint (test2->grab_status, ==, GRAB_STATUS_FAILED);

  phoc_test_keyboard_event_free (test1);
  phoc_test_keyboard_event_free (test2);

  return TRUE;
}

static void
test_phosh_private_kbevents_simple (void)
{
  PhocTestClientIface iface = {
   .client_run = test_client_phosh_private_kbevent_simple,
  };

  phoc_test_client_run (TEST_PHOC_CLIENT_TIMEOUT, &iface, NULL);
}

static void
startup_tracker_handle_launched (void                                 *data,
                                 struct phosh_private_startup_tracker *startup_tracker,
                                 const char                           *startup_id,
                                 unsigned int                          protocol,
                                 unsigned int                          flags)
{
  int *counter = data;

  (*counter)++;
  g_assert_cmpint (flags, ==, 0);
  g_assert_cmpint(protocol, ==, PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_GTK_SHELL);
}


static void
startup_tracker_handle_startup_id (void                                 *data,
                                   struct phosh_private_startup_tracker *startup_tracker,
                                   const char                           *startup_id,
                                   unsigned int                          protocol,
                                   unsigned int                          flags)

{
  int *counter = data;

  (*counter)++;
  g_assert_cmpint (flags, ==, 0);
  g_assert_cmpint(protocol, ==, PHOSH_PRIVATE_STARTUP_TRACKER_PROTOCOL_GTK_SHELL);
}

static const struct phosh_private_startup_tracker_listener startup_tracker_listener = {
  .startup_id = startup_tracker_handle_startup_id,
  .launched = startup_tracker_handle_launched,
};

static gboolean
test_client_phosh_private_startup_tracker_simple (PhocTestClientGlobals *globals, gpointer unused)
{
  struct phosh_private_startup_tracker *tracker;
  int counter = 0;

  tracker = phosh_private_get_startup_tracker (globals->phosh);
  g_assert_cmpint (phosh_private_get_version (globals->phosh), >=, 6);
  g_assert_cmpint (gtk_shell1_get_version (globals->gtk_shell1), >=, 3);
  phosh_private_startup_tracker_add_listener (tracker, &startup_tracker_listener, &counter);
  gtk_shell1_set_startup_id (globals->gtk_shell1, "startup_id1");

  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_cmpint (counter, ==, 1);

  gtk_shell1_notify_launch (globals->gtk_shell1, "startup_id1");

  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  phosh_private_startup_tracker_destroy (tracker);

  g_assert_cmpint (counter, ==, 2);

  return TRUE;
}

static void
test_phosh_private_startup_tracker_simple (void)
{
  PhocTestClientIface iface = {
   .client_run = test_client_phosh_private_startup_tracker_simple,
  };

  phoc_test_client_run (TEST_PHOC_CLIENT_TIMEOUT, &iface, NULL);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  PHOC_TEST_ADD ("/phoc/phosh/thumbnail/simple", test_phosh_private_thumbnail_simple);
  PHOC_TEST_ADD ("/phoc/phosh/kbevents/simple", test_phosh_private_kbevents_simple);
  PHOC_TEST_ADD ("/phoc/phosh/startup-tracker/simple", test_phosh_private_startup_tracker_simple);
  return g_test_run ();
}
