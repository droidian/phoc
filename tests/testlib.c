/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib.h"
#include "server.h"
#include <wayland-client.h>

#include <cairo.h>
#include <errno.h>
#include <sys/mman.h>

struct task_data {
  PhocTestClientFunc func;
  gpointer data;
};

static bool
abgr_to_argb (PhocTestBuffer *buffer)
{
  g_assert_true (buffer->format == WL_SHM_FORMAT_ABGR8888 ||
		 buffer->format == WL_SHM_FORMAT_XBGR8888);
  guint8 *data = buffer->shm_data;

  for (int i = 0; i < buffer->height * buffer->stride; i += 4) {
      guint32 *px = (guint32 *)(data + i);
      guint8 r, g, b, a;

      a = (*px >> 24) & 0xFF;
      b = (*px >> 16) & 0xFF;
      g = (*px >> 8) & 0xFF;
      r = *px & 0xFF;
      *px = (a << 24) | (r << 16) | (g << 8) | b;
  }

  switch (buffer->format) {
  case WL_SHM_FORMAT_ABGR8888:
    buffer->format = WL_SHM_FORMAT_ARGB8888;
    break;
  case WL_SHM_FORMAT_XBGR8888:
    buffer->format = WL_SHM_FORMAT_XRGB8888;
    break;
  default:
    g_assert_not_reached ();
  }
  return true;
}

static void
buffer_to_argb(PhocTestBuffer *buffer)
{
  switch (buffer->format) {
  case WL_SHM_FORMAT_XRGB8888:
  case WL_SHM_FORMAT_ARGB8888:
    break;
  case WL_SHM_FORMAT_XBGR8888:
  case WL_SHM_FORMAT_ABGR8888:
    abgr_to_argb(buffer);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
screencopy_frame_handle_buffer (void *data,
				struct zwlr_screencopy_frame_v1 *handle,
				uint32_t format,
				uint32_t width,
				uint32_t height,
				uint32_t stride)
{
  PhocTestScreencopyFrame *frame = data;
  gboolean success;

  success = phoc_test_client_create_shm_buffer (frame->globals,
						&frame->buffer,
						width,
						height,
						format);
  g_assert_true (success);
  zwlr_screencopy_frame_v1_copy(handle, frame->buffer.wl_buffer);
}

static void
screencopy_frame_handle_flags(void *data, struct zwlr_screencopy_frame_v1 *handle, uint32_t flags)
{
  PhocTestScreencopyFrame *frame = data;

  frame->flags = flags;
}

static void
screencopy_frame_handle_ready (void *data, struct zwlr_screencopy_frame_v1 *handle,
			       uint32_t tv_sec_hi, uint32_t tv_sec_lo,
			       uint32_t tv_nsec)
{
  PhocTestScreencopyFrame *frame = data;

  frame->done = TRUE;
}

static void
screencopy_frame_handle_failed(void *data, struct zwlr_screencopy_frame_v1 *frame)
{
  g_assert_not_reached ();
}

static const struct zwlr_screencopy_frame_v1_listener screencopy_frame_listener = {
  .buffer = screencopy_frame_handle_buffer,
  .flags = screencopy_frame_handle_flags,
  .ready = screencopy_frame_handle_ready,
  .failed = screencopy_frame_handle_failed,
};

static void
shm_format (void *data, struct wl_shm *wl_shm, guint32 format)
{
  PhocTestClientGlobals *globals = data;

  globals->formats |= (1 << format);
}

static void
buffer_release (void *data, struct wl_buffer *buffer)
{
  /* TBD */
}

struct wl_shm_listener shm_listener = {
  shm_format,
};

static const struct wl_buffer_listener buffer_listener = {
  buffer_release,
};

static void
output_handle_geometry (void *data, struct wl_output *wl_output,
			int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
			int32_t subpixel, const char *make, const char *model,
			int32_t transform) {
  /* TBD */
}

static void
output_handle_mode (void *data, struct wl_output *wl_output,
		    uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
  PhocTestOutput *output = data;

  if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
    /* Make sure we got the right mode to not mess up screenshot comparisons */
    g_assert_cmpint (width, ==, 1024);
    g_assert_cmpint (height, ==, 768);
    output->width = width;
    output->height = height;
  }
}

static void
output_handle_done (void *data, struct wl_output *wl_output)
{
  /* TBD */
}

static void
output_handle_scale (void *data, struct wl_output *wl_output,
		     int32_t scale)
{
  g_assert_cmpint (scale, ==, 1);
}

static const struct wl_output_listener output_listener = {
  .geometry = output_handle_geometry,
  .mode = output_handle_mode,
  .done = output_handle_done,
  .scale = output_handle_scale,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
  xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
  xdg_wm_base_ping,
};


static void
foreign_toplevel_handle_title (void *data,
			       struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
			       const char* title)
{
  PhocTestForeignToplevel *toplevel = data;
  toplevel->title = g_strdup (title);
  g_debug ("Got toplevel's title: %p %s", toplevel, title);
}

static void
foreign_toplevel_handle_app_id (void *data,
				struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
				const char* app_id)
{
}

static void
foreign_toplevel_handle_output_enter (void *data,
				      struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
				      struct wl_output *output)
{
}

static void
foreign_toplevel_handle_output_leave (void *data,
				      struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
				      struct wl_output *output)
{
}

static void
foreign_toplevel_handle_state (void *data,
			       struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
			       struct wl_array *state)
{
}

static void
foreign_toplevel_handle_done (void *data,
			      struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1)
{
}


static void
foreign_toplevel_handle_closed (void *data,
				struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1)
{
  PhocTestForeignToplevel *toplevel = data;
  PhocTestClientGlobals *globals = toplevel->globals;
  globals->foreign_toplevels = g_slist_remove (globals->foreign_toplevels, toplevel);
  g_free (toplevel->title);
  g_free (toplevel);
}

static const struct zwlr_foreign_toplevel_handle_v1_listener foreign_toplevel_handle_listener = {
  foreign_toplevel_handle_title,
  foreign_toplevel_handle_app_id,
  foreign_toplevel_handle_output_enter,
  foreign_toplevel_handle_output_leave,
  foreign_toplevel_handle_state,
  foreign_toplevel_handle_done,
  foreign_toplevel_handle_closed
};


static void
foreign_toplevel_manager_handle_toplevel (void *data,
					  struct zwlr_foreign_toplevel_manager_v1 *zwlr_foreign_toplevel_manager_v1,
					  struct zwlr_foreign_toplevel_handle_v1 *handle)
{
  PhocTestClientGlobals *globals = data;

  PhocTestForeignToplevel *toplevel = g_malloc0 (sizeof (PhocTestForeignToplevel));
  toplevel->handle = handle;
  toplevel->globals = globals;
  globals->foreign_toplevels = g_slist_append (globals->foreign_toplevels, toplevel);
  zwlr_foreign_toplevel_handle_v1_add_listener (handle, &foreign_toplevel_handle_listener, toplevel);
  g_debug ("New toplevel: %p", toplevel);
}

static void
foreign_toplevel_manager_handle_finished (void *data,
					  struct zwlr_foreign_toplevel_manager_v1 *zwlr_foreign_toplevel_manager_v1)
{
  g_debug ("wlr_foreign_toplevel_manager_finished");
}


static const struct zwlr_foreign_toplevel_manager_v1_listener foreign_toplevel_manager_listener = {
  foreign_toplevel_manager_handle_toplevel,
  foreign_toplevel_manager_handle_finished
};

static void registry_handle_global(void *data, struct wl_registry *registry,
				   uint32_t name, const char *interface, uint32_t version)
{
  PhocTestClientGlobals *globals = data;

  if (!g_strcmp0 (interface, wl_compositor_interface.name)) {
    globals->compositor = wl_registry_bind (registry, name, &wl_compositor_interface, 4);
  } else if (!g_strcmp0 (interface, wl_shm_interface.name)) {
    globals->shm = wl_registry_bind (registry, name, &wl_shm_interface, 1);
    wl_shm_add_listener (globals->shm, &shm_listener, globals);
  } else if (!g_strcmp0 (interface, wl_output_interface.name)) {
    /* TODO: only one output atm */
    g_assert_null (globals->output.output);
    globals->output.output = wl_registry_bind (registry, name,
					       &wl_output_interface, 3);
    wl_output_add_listener(globals->output.output, &output_listener, &globals->output);
  } else if (!g_strcmp0 (interface, xdg_wm_base_interface.name)) {
    globals->xdg_shell = wl_registry_bind (registry, name,
					   &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(globals->xdg_shell, &wm_base_listener, NULL);
  } else if (!g_strcmp0 (interface, zwlr_layer_shell_v1_interface.name)) {
    globals->layer_shell = wl_registry_bind (registry, name,
					     &zwlr_layer_shell_v1_interface, 2);
  } else if (!g_strcmp0 (interface, zwlr_screencopy_manager_v1_interface.name)) {
    globals->screencopy_manager = wl_registry_bind (registry, name,
						    &zwlr_screencopy_manager_v1_interface, 1);
  } else if (!g_strcmp0 (interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
    globals->foreign_toplevel_manager = wl_registry_bind (registry, name,
							  &zwlr_foreign_toplevel_manager_v1_interface, 2);
    zwlr_foreign_toplevel_manager_v1_add_listener (globals->foreign_toplevel_manager,
						   &foreign_toplevel_manager_listener, globals);
  } else if (!g_strcmp0 (interface, phosh_private_interface.name)) {
    globals->phosh = wl_registry_bind (registry, name, &phosh_private_interface, 6);
  } else if (!g_strcmp0 (interface, gtk_shell1_interface.name)) {
    globals->gtk_shell1 = wl_registry_bind (registry, name, &gtk_shell1_interface, 3);
  }
}

static void registry_handle_global_remove (void *data,
		struct wl_registry *registry, uint32_t name) {
  // This space is intentionally left blank
}

static const struct wl_registry_listener registry_listener = {
  .global = registry_handle_global,
  .global_remove = registry_handle_global_remove,
};

static void
wl_client_run (GTask *task, gpointer source,
	       gpointer data, GCancellable *cancel)
{
  struct wl_registry *registry;
  gboolean success = FALSE;
  struct task_data *td = data;
  PhocTestClientGlobals globals = { 0 };

  globals.display = wl_display_connect(NULL);
  g_assert_nonnull (globals.display);
  registry = wl_display_get_registry(globals.display);
  wl_registry_add_listener(registry, &registry_listener, &globals);
  wl_display_dispatch(globals.display);
  wl_display_roundtrip(globals.display);
  g_assert_nonnull (globals.compositor);
  g_assert_nonnull (globals.layer_shell);
  g_assert_nonnull (globals.shm);
  g_assert_nonnull (globals.xdg_shell);
  g_assert_nonnull (globals.gtk_shell1);

  g_assert (globals.formats & (1 << WL_SHM_FORMAT_XRGB8888));

  if (td->func)
    success = (td->func)(&globals, td->data);
  else
    success = TRUE;

  g_task_return_boolean (task, success);
}

static void
on_wl_client_finish (GObject *source, GAsyncResult *res, gpointer data)
{
  GMainLoop *loop = data;
  gboolean success;
  g_autoptr(GError) err = NULL;

  g_assert_true (g_task_is_valid (res, source));
  success = g_task_propagate_boolean (G_TASK (res), &err);

  /* Client ran succesfully */
  g_assert_true (success);
  g_main_loop_quit (loop);
}

static gboolean
on_timer_expired (gpointer unused)
{
  /* Compositor did not quit in time */
  g_assert_not_reached ();
  return FALSE;
}

/**
 * phoc_test_client_run:
 *
 * timeout: Abort test after timeout seconds
 * func: The test function to run
 * data: Data passed to the test function
 *
 * Run func in a wayland client connected to compositor instance. The
 * test function is expected to return %TRUE on success and %FALSE
 * otherwise.
 */
void
phoc_test_client_run (gint timeout, PhocTestClientIface *iface, gpointer data)
{
  struct task_data td = { .data = data };
  g_autoptr(PhocServer) server = phoc_server_get_default ();
  g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr(GTask) wl_client_task = g_task_new (NULL, NULL,
						on_wl_client_finish,
						loop);
  if (iface)
    td.func = iface->client_run;

  g_assert_true (PHOC_IS_SERVER (server));
  g_assert_true (phoc_server_setup(server, TEST_PHOC_INI, NULL, loop,
                                   PHOC_SERVER_FLAG_NONE,
				   PHOC_SERVER_DEBUG_FLAG_NONE));
  if (iface && iface->server_prepare)
    g_assert_true (iface->server_prepare(server, data));

  g_task_set_task_data (wl_client_task, &td, NULL);
  g_task_run_in_thread (wl_client_task, wl_client_run);
  g_timeout_add_seconds (timeout, on_timer_expired, NULL);
  g_main_loop_run (loop);
}

static int
create_anon_file (off_t size)
{
  char template[] = "/tmp/phoctest-shared-XXXXXX";
  int fd;
  int ret;

  fd = mkstemp(template);
  g_assert_cmpint (fd, >=, 0);

  do {
    errno = 0;
    ret = ftruncate(fd, size);
  } while (errno == EINTR);
  g_assert_cmpint (ret, ==, 0);
  unlink(template);
  return fd;
}

/**
 * phoc_test_client_create_shm_buffer:
 *
 * Create a shm buffer, this assumes RGBA8888
 */
gboolean
phoc_test_client_create_shm_buffer (PhocTestClientGlobals *globals,
				    PhocTestBuffer *buffer,
				    int width, int height, guint32 format)
{
  struct wl_shm_pool *pool;
  int fd, size;
  void *data;

  g_assert (globals->shm);
  buffer->stride = width * 4;
  buffer->width = width;
  buffer->height = height;
  buffer->format = format;
  size = buffer->stride * height;

  fd = create_anon_file(size);
  g_assert_cmpint (fd, >=, 0);

  data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  g_assert (data != MAP_FAILED);

  pool = wl_shm_create_pool(globals->shm, fd, size);
  buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0,
						width, height,
						buffer->stride, format);
  wl_buffer_add_listener (buffer->wl_buffer, &buffer_listener, buffer);
  wl_shm_pool_destroy (pool);
  close (fd);

  buffer->shm_data = data;

  return TRUE;
}

static gint
foreign_toplevel_compare (gconstpointer data, gconstpointer title)
{
  const PhocTestForeignToplevel *toplevel = data;
  return g_strcmp0 (toplevel->title, title);
}

/**
 *
 * phoc_test_client_get_foreign_toplevel_handle:
 *
 * Get the PhocTestForeignToplevel for a toplevel with the given title
 * using the wlr_foreign_toplevel_management protocol.
 *
 * Returns: (transfer-none): The toplevel's handle, or NULL if it doesn't exist.
 */
PhocTestForeignToplevel *
phoc_test_client_get_foreign_toplevel_handle (PhocTestClientGlobals *globals,
                                              const char *title)
{
  GSList *list = g_slist_find_custom (globals->foreign_toplevels, title, foreign_toplevel_compare);
  if (!list || !list->data)
    return NULL;

  return list->data;
}


/**
 *
 * phoc_test_client_capture_frame:
 *
 * Capture the given wlr_screencopy_frame and return its screenshot buffer
 *
 * Returns: (transfer-none): The screenshot buffer.
 */
PhocTestBuffer *
phoc_test_client_capture_frame (PhocTestClientGlobals *globals,
				PhocTestScreencopyFrame *frame, struct zwlr_screencopy_frame_v1 *handle)
{
  frame->globals = globals;
  frame->handle = handle;
  g_assert_false (frame->done);

  zwlr_screencopy_frame_v1_add_listener(handle,
                                        &screencopy_frame_listener, frame);
  while (!frame->done && wl_display_dispatch (globals->display) != -1) {
  }
  g_assert_true (frame->done);

  /* Reverse captured buffer */
  if (frame->flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) {
    guint32 height = frame->buffer.height;
    guint32 stride = frame->buffer.stride;
    guint8 *src = frame->buffer.shm_data;
    g_autofree guint8 *dst = g_malloc0 (height * stride);

    for (guint i = 0, j = height - 1; i < height; i++, j--)
      memmove((dst + (i * stride)), (src + (j * stride)), stride);

    memmove (src, dst, height * stride);
    frame->flags &= ~ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
    /* There shouldn't be any other flags left */
    g_assert_false (frame->flags);
  }
  buffer_to_argb(&frame->buffer);

  frame->done = FALSE;

  return &frame->buffer;
}

/**
 *
 * phoc_test_client_capture_output:
 *
 * Capture the given output and return its screenshot buffer
 *
 * Returns: (transfer-none): The screenshot buffer.
 */
PhocTestBuffer *
phoc_test_client_capture_output (PhocTestClientGlobals *globals,
				 PhocTestOutput *output)
{
  struct zwlr_screencopy_frame_v1 *handle = zwlr_screencopy_manager_v1_capture_output (globals->screencopy_manager, FALSE, output->output);
  phoc_test_client_capture_frame (globals, &output->screenshot, handle);

  g_assert_cmpint (output->screenshot.buffer.width, ==, output->width);
  g_assert_cmpint (output->screenshot.buffer.height, ==, output->height);

  return &output->screenshot.buffer;
}

/**
 *
 * phoc_test_buffer_equal:
 *
 * Compare two buffers
 *
 * Returns: %TRUE if buffers have identical content, otherwise %FALSE
 */
gboolean phoc_test_buffer_equal (PhocTestBuffer *buf1, PhocTestBuffer *buf2)
{
  guint8 *c1 = buf1->shm_data;
  guint8 *c2 = buf2->shm_data;

  g_assert_true (buf1->format == WL_SHM_FORMAT_XRGB8888
                || buf1->format == WL_SHM_FORMAT_ARGB8888);
  g_assert_true (buf2->format == WL_SHM_FORMAT_XRGB8888
                || buf2->format == WL_SHM_FORMAT_ARGB8888);

  if (buf1->width != buf2->width ||
      buf1->height != buf2->height) {
    return FALSE;
  }

  for (guint y = 0; y < buf1->height; y++) {
    for (guint x = 0; x < buf1->width; x++) {
      // B
      if (c1[y * buf1->stride + x * 4 + 0] != c2[y * buf2->stride + x * 4 + 0])
          return FALSE;
      // G
      if (c1[y * buf1->stride + x * 4 + 1] != c2[y * buf2->stride + x * 4 + 1])
          return FALSE;
      // R
      if (c1[y * buf1->stride + x * 4 + 2] != c2[y * buf2->stride + x * 4 + 2])
          return FALSE;
      // A/X
      if (buf1->format == WL_SHM_FORMAT_ARGB8888 && buf2->format == WL_SHM_FORMAT_ARGB8888)
        if (c1[y * buf1->stride + x * 4 + 3] != c2[y * buf2->stride + x * 4 + 3])
          return FALSE;
      if (buf1->format == WL_SHM_FORMAT_ARGB8888 && buf2->format == WL_SHM_FORMAT_XRGB8888)
        if (c1[y * buf1->stride + x * 4 + 3] != 255)
          return FALSE;
      if (buf1->format == WL_SHM_FORMAT_XRGB8888 && buf2->format == WL_SHM_FORMAT_ARGB8888)
        if (c2[y * buf2->stride + x * 4 + 3] != 255)
          return FALSE;
    }
  }

  return TRUE;
}

/**
 * phoc_test_buffer_save:
 *
 * Save a buffer as png
 *
 * Returns: %TRUE if buffers was saved successfully, otherwise %FALSE
 */
gboolean
phoc_test_buffer_save (PhocTestBuffer *buffer, const gchar *filename)
{
  cairo_surface_t *surface;
  cairo_status_t status;

  g_assert_nonnull (buffer);
  g_assert_nonnull (filename);

  g_assert_true (buffer->format == WL_SHM_FORMAT_XRGB8888
		 || buffer->format == WL_SHM_FORMAT_ARGB8888);

  surface = cairo_image_surface_create_for_data ((guchar*)buffer->shm_data,
						 CAIRO_FORMAT_ARGB32,
						 buffer->width,
						 buffer->height,
						 buffer->stride);
  status = cairo_surface_write_to_png (surface, filename);
  g_assert_cmpint (status, ==, CAIRO_STATUS_SUCCESS);
  g_debug ("Saved buffer png %s", filename);

  cairo_surface_destroy(surface);
  return TRUE;
}

gboolean
phoc_test_buffer_matches_screenshot (PhocTestBuffer *buffer, const gchar *filename)
{
  const char *msg;
  cairo_surface_t *surface = cairo_image_surface_create_from_png (filename);
  cairo_format_t format;
  guint32 *l, *r;
  guint32 mask = 0xFFFFFFFF;
  int ret;

  g_assert_true (buffer->format == WL_SHM_FORMAT_XRGB8888
		 || buffer->format == WL_SHM_FORMAT_ARGB8888);

  switch (cairo_surface_status (surface)) {
    case CAIRO_STATUS_NO_MEMORY:
      msg = "no memory";
      break;
    case CAIRO_STATUS_FILE_NOT_FOUND:
      msg = "file not found";
      break;
    case CAIRO_STATUS_READ_ERROR:
      msg = "read error";
      break;
    case CAIRO_STATUS_PNG_ERROR:
      msg = "png error";
      break;
    default:
      msg = NULL;
  }

  if (msg)
    g_error("Failed to load screenshot %s: %s", filename, msg);

  format = cairo_image_surface_get_format (surface);
  switch (format) {
  case CAIRO_FORMAT_RGB24:
    mask = 0x00FFFFFF;
    break;
  case CAIRO_FORMAT_ARGB32:
    mask = 0xFFFFFFFF;
    break;
  default:
    g_assert_not_reached();
  }

  if (buffer->height != cairo_image_surface_get_height (surface) ||
      buffer->width != cairo_image_surface_get_width (surface) ||
      buffer->stride != cairo_image_surface_get_stride (surface)) {
    g_test_message ("Metadata mismatch for %s", filename);
    return FALSE;
  }

  l = (guint32*)buffer->shm_data;
  r = (guint32*)cairo_image_surface_get_data (surface);
  g_assert_nonnull (r);

  ret = TRUE;
  for (int i = 0; i < buffer->height * buffer->stride / 4; i++) {
    if ((l[i] & mask) != (r[i] & mask)) {
      g_test_message ("Mismatch: %d: 0x%x 0x%x for %s", i, l[i], r[i], filename);
      ret = FALSE;
    }
  }
  return ret;
}

void
phoc_test_buffer_free (PhocTestBuffer *buffer)
{
  g_assert_nonnull (buffer);

  munmap(buffer->shm_data, buffer->stride * buffer->height);
  wl_buffer_destroy(buffer->wl_buffer);
  buffer->valid = FALSE;
}
