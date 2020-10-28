#define G_LOG_DOMAIN "phoc-input"

#include "config.h"

#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend/libinput.h>
#include <wlr/config.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#ifdef PHOC_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "input.h"
#include "keyboard.h"
#include "seat.h"

G_DEFINE_TYPE (PhocInput, phoc_input, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_CONFIG,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

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

static void
phoc_input_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  PhocInput *self = PHOC_INPUT (object);

  switch (property_id) {
  case PROP_CONFIG:
    self->config = g_value_get_pointer (value);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIG]);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_input_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  PhocInput *self = PHOC_INPUT (object);

  switch (property_id) {
  case PROP_CONFIG:
    g_value_set_pointer (value, self->config);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
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
  struct roots_device_config *dc =
    roots_config_get_device (input->config, device);

  if (dc) {
    seat_name = dc->seat;
  }

  struct roots_seat *seat = phoc_input_get_seat (input, seat_name);

  if (!seat) {
    g_warning ("could not create roots seat");
    return;
  }

  g_debug ("New input device: %s (%d:%d) %s seat:%s", device->name,
           device->vendor, device->product,
           phoc_input_get_device_type (device->type), seat_name);

  roots_seat_add_device (seat, device);

  if (dc && wlr_input_device_is_libinput (device)) {
    struct libinput_device *libinput_dev =
      wlr_libinput_get_device_handle (device);

    g_debug ("input has config, tap_enabled: %d\n", dc->tap_enabled);
    if (dc->tap_enabled) {
      libinput_device_config_tap_set_enabled (libinput_dev,
                                              LIBINPUT_CONFIG_TAP_ENABLED);
    }
  }
}

static void
phoc_input_init (PhocInput *self)
{
}

PhocInput *
phoc_input_new (struct roots_config *config)
{
  return g_object_new (PHOC_TYPE_INPUT,
                       "config", config,
                       NULL);
}

static void
phoc_input_constructed (GObject *object)
{
  PhocInput *self = PHOC_INPUT (object);
  PhocServer *server = phoc_server_get_default ();

  g_debug ("Initializing roots input");
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
phoc_input_dispose (GObject *object)
{
  PhocInput *self = PHOC_INPUT (object);

  g_clear_object (&self->config);

  G_OBJECT_CLASS (phoc_input_parent_class)->dispose (object);
}

static void
phoc_input_class_init (PhocInputClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->set_property = phoc_input_set_property;
  object_class->get_property = phoc_input_get_property;

  object_class->constructed = phoc_input_constructed;
  object_class->dispose = phoc_input_dispose;
  object_class->finalize = phoc_input_finalize;

  props[PROP_CONFIG] =
    g_param_spec_pointer (
      "config",
      "Config",
      "The config object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

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
