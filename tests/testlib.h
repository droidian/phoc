/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#include "server.h"

#include <glib.h>
#include "gtk-shell-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "phosh-private-client-protocol.h"
#include "phoc-layer-shell-effects-unstable-v1-client-protocol.h"

#pragma once

G_BEGIN_DECLS

typedef struct _PhocTestWlGlobals PhocTestClientGlobals;

typedef struct _PhocTestBuffer {
  struct wl_buffer *wl_buffer;
  guint8 *shm_data;
  guint32 width, height, stride;
  enum wl_shm_format format;
  gboolean valid;
} PhocTestBuffer;

typedef struct _PhocTestScreencopyFrame {
  PhocTestBuffer buffer;
  gboolean done;
  uint32_t flags;
  PhocTestClientGlobals *globals;
} PhocTestScreencopyFrame;

typedef struct _PhocTestOutput {
  struct wl_output *output;
  guint32 width, height;
  PhocTestScreencopyFrame screenshot;
} PhocTestOutput;

typedef struct _PhocTestWlGlobals {
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct xdg_wm_base *xdg_shell;
  struct zwlr_layer_shell_v1 *layer_shell;
  struct zphoc_layer_shell_effects_v1 *layer_shell_effects;
  struct zwlr_screencopy_manager_v1 *screencopy_manager;
  struct zwlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;
  GSList *foreign_toplevels;
  struct phosh_private *phosh;
  struct gtk_shell1 *gtk_shell1;
  /* TODO: handle multiple outputs */
  PhocTestOutput output;

  guint32 formats;
} PhocTestClientGlobals;

typedef struct _PhocTestForeignToplevel {
    char* title;
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    PhocTestClientGlobals *globals;
} PhocTestForeignToplevel;

typedef gboolean (* PhocTestServerFunc) (PhocServer *server, gpointer data);
typedef gboolean (* PhocTestClientFunc) (PhocTestClientGlobals *globals, gpointer data);

typedef struct PhocTestClientIface {
  /* Prepare function runs in server context */
  PhocTestServerFunc server_prepare;
  PhocTestClientFunc client_run;
} PhocTestClientIface;

typedef struct _PhocTestXdgToplevelSurface
{
  struct wl_surface *wl_surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  PhocTestForeignToplevel *foreign_toplevel;
  char* title;
  PhocTestBuffer buffer;
  guint32 width, height;
  gboolean configured;
  gboolean toplevel_configured;
} PhocTestXdgToplevelSurface;

typedef struct _PhocTestFixture {
  GTestDBus   *bus;
  char        *tmpdir;
} PhocTestFixture;


/* Test client */
void phoc_test_client_run (gint timeout, PhocTestClientIface *iface, gpointer data);
int  phoc_test_client_create_shm_buffer (PhocTestClientGlobals *globals,
                                         PhocTestBuffer *buffer,
                                         int width, int height, guint32 format);
PhocTestBuffer *phoc_test_client_capture_frame (PhocTestClientGlobals *globals,
                                                PhocTestScreencopyFrame *frame,
                                                struct zwlr_screencopy_frame_v1 *handle);
PhocTestBuffer *phoc_test_client_capture_output (PhocTestClientGlobals *globals,
                                                 PhocTestOutput *output);
PhocTestForeignToplevel *phoc_test_client_get_foreign_toplevel_handle (PhocTestClientGlobals *globals,
                                                                       const char *title);

/* Test surfaces */
PhocTestXdgToplevelSurface *
                phoc_test_xdg_toplevel_new  (PhocTestClientGlobals     *globals,
                                             guint32                    width,
                                             guint32                    height,
                                             const char                *title,
                                             guint32                    color);
void            phoc_test_xdg_toplevel_free (PhocTestXdgToplevelSurface *xs);
void            phoc_test_xdg_update_buffer (PhocTestClientGlobals      *globals,
                                             PhocTestXdgToplevelSurface *xs,
                                             guint32                     color);

/* Buffers */
gboolean phoc_test_buffer_equal (PhocTestBuffer *buf1, PhocTestBuffer *buf2);
gboolean phoc_test_buffer_save (PhocTestBuffer *buffer, const gchar *filename);
gboolean phoc_test_buffer_matches_screenshot (PhocTestBuffer *buffer, const gchar *filename);
void phoc_test_buffer_free (PhocTestBuffer *buffer);

#define _phoc_test_screenshot_name(l, f, n) (g_strdup_printf ("phoc-test-screenshot-%d-%s_%d.png", l, f, n))

/*
 * phoc_assert_screenshot:
 * @g: The client global object
 * @f: The screenshot to compare the current output to
 */
#define phoc_assert_screenshot(g, f) G_STMT_START {                      \
    PhocTestClientGlobals *__g = (g);                                    \
    gchar *__f = g_test_build_filename (G_TEST_DIST, "screenshots", f, NULL); \
    PhocTestBuffer *__s = phoc_test_client_capture_output (__g, &__g->output); \
    if (phoc_test_buffer_matches_screenshot (__s, __f)) ; else {         \
      g_autofree gchar *__name = _phoc_test_screenshot_name(__LINE__, G_STRFUNC, 0); \
      phoc_test_buffer_save (&__g->output.screenshot.buffer, __name);            \
      g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,  \
                           "Output content does not match " #f);         \
    }                                                                    \
    phoc_test_buffer_free (__s);                                         \
    g_free (__f);                                                        \
  } G_STMT_END

/**
 * phoc_test_assert_buffer_equal:
 * @b1: A PhocClientBuffer
 * @b2: A PhocClientBuffer
 *
 * Debugging macro to compare two buffers. If the buffers don't match
 * screenshots are taken and saved as PNG.
 */
#define phoc_assert_buffer_equal(b1, b2)    G_STMT_START { \
    PhocTestBuffer *__b1 = (b1), *__b2 = (b2);                          \
    if (phoc_test_buffer_equal (__b1 , __b2)) ; else {                  \
      g_autofree gchar *__name1 = _phoc_test_screenshot_name(__LINE__, G_STRFUNC, 1); \
      g_autofree gchar *__name2 = _phoc_test_screenshot_name(__LINE__, G_STRFUNC, 2); \
      phoc_test_buffer_save (__b1, __name1);                            \
      phoc_test_buffer_save (__b2, __name2);                            \
      g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                         "Buffer " #b1 " != " #b2);                     \
    } \
  } G_STMT_END


/* Test setup and fixtures */
void phoc_test_setup (PhocTestFixture *fixture, gconstpointer data);
void phoc_test_teardown (PhocTestFixture *fixture, gconstpointer unused);
#define PHOC_TEST_ADD(name, func) g_test_add ((name), PhocTestFixture, NULL, \
                                              (gpointer)phoc_test_setup,     \
                                              (gpointer)(func),              \
                                              (gpointer)phoc_test_teardown)

G_END_DECLS
