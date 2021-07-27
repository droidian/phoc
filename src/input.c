#define G_LOG_DOMAIN "phoc-input"

#include "config.h"

#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include "input.h"
#include "keyboard.h"
#include "seat.h"

G_DEFINE_TYPE (PhocInput, phoc_input, G_TYPE_OBJECT);

const char *
phoc_input_get_device_type (enum wlr_input_device_type type)
{
  switch (type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    return "keyboard";
  case WLR_INPUT_DEVICE_POINTER:
    return "pointer";
  case WLR_INPUT_DEVICE_SWITCH:
    return "switch";
  case WLR_INPUT_DEVICE_TOUCH:
    return "touch";
  case WLR_INPUT_DEVICE_TABLET_TOOL:
    return "tablet tool";
  case WLR_INPUT_DEVICE_TABLET_PAD:
    return "tablet pad";
  default:
    return NULL;
  }
}

struct roots_seat *
phoc_input_get_seat (PhocInput *self, char *name)
{
  struct roots_seat *seat = NULL;

  wl_list_for_each (seat, &self->seats, link) {
    if (strcmp (seat->seat->name, name) == 0) {
      return seat;
    }
  }

  seat = roots_seat_create (self, name);
  return seat;
}

static void
handle_new_input (struct wl_listener *listener, void *data)
{
  struct wlr_input_device *device = data;
  PhocInput *input = wl_container_of (listener, input, new_input);

  char *seat_name = ROOTS_CONFIG_DEFAULT_SEAT_NAME;
  struct roots_seat *seat = phoc_input_get_seat (input, seat_name);

  if (!seat) {
    g_warning ("could not create roots seat");
    return;
  }

  g_debug ("New input device: %s (%d:%d) %s seat:%s", device->name,
           device->vendor, device->product,
           phoc_input_get_device_type (device->type), seat_name);

  roots_seat_add_device (seat, device);
}

static void
phoc_input_init (PhocInput *self)
{
}

PhocInput *
phoc_input_new (struct roots_config *config)
{
  return g_object_new (PHOC_TYPE_INPUT, NULL);
}

static void
phoc_input_constructed (GObject *object)
{
  PhocInput *self = PHOC_INPUT (object);
  PhocServer *server = phoc_server_get_default ();

  g_debug ("Initializing phoc input");
  assert (server->desktop);

  wl_list_init (&self->seats);

  self->new_input.notify = handle_new_input;
  wl_signal_add (&server->backend->events.new_input, &self->new_input);
  G_OBJECT_CLASS (phoc_input_parent_class)->constructed (object);

}

static void
phoc_input_finalize (GObject *object)
{
  PhocInput *self = PHOC_INPUT (object);

  wl_list_remove (&self->seats);

  G_OBJECT_CLASS (phoc_input_parent_class)->finalize (object);
}

static void
phoc_input_class_init (PhocInputClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->constructed = phoc_input_constructed;
  object_class->finalize = phoc_input_finalize;
}

struct roots_seat *
phoc_input_seat_from_wlr_seat (PhocInput       *self,
                               struct wlr_seat *wlr_seat)
{
  struct roots_seat *seat = NULL;

  wl_list_for_each (seat, &self->seats, link) {
    if (seat->seat == wlr_seat) {
      return seat;
    }
  }
  return seat;
}

bool
phoc_input_view_has_focus (PhocInput *self, struct roots_view *view)
{
  if (!view) {
    return false;
  }
  struct roots_seat *seat;

  wl_list_for_each (seat, &self->seats, link) {
    if (view == roots_seat_get_focus (seat)) {
      return true;
    }
  }

  return false;
}

static inline int64_t
timespec_to_msec (const struct timespec *a)
{
  return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

void
phoc_input_update_cursor_focus (PhocInput *self)
{
  struct timespec now;

  clock_gettime (CLOCK_MONOTONIC, &now);
  g_assert_nonnull (self);

  struct roots_seat *seat;

  wl_list_for_each (seat, &self->seats, link) {
    roots_cursor_update_position (roots_seat_get_cursor (seat),
                                  timespec_to_msec (&now));
  }
}
