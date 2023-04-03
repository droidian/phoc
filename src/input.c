#define G_LOG_DOMAIN "phoc-input"

#include "phoc-config.h"

#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include "cursor.h"
#include "input.h"
#include "seat.h"
#include "server.h"

/**
 * PhocInput:
 *
 * PhocInput handles new input devices and seats
 */
struct _PhocInput {
  GObject              parent;

  struct wl_listener   new_input;
  GSList              *seats; // PhocSeat
};

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


/**
 * phoc_input_get_seat:
 * @self: The input to look up the seat on
 * @name: The seats name
 *
 * Returns: (transfer none): The seat of the given name.
 */
PhocSeat *
phoc_input_get_seat (PhocInput *self, char *name)
{
  PhocSeat *seat = NULL;

  g_assert (PHOC_IS_INPUT (self));

  for (GSList *elem = self->seats; elem; elem = elem->next) {
    seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    if (strcmp (seat->seat->name, name) == 0) {
      return seat;
    }
  }

  seat = phoc_seat_new (self, name);
  self->seats = g_slist_prepend (self->seats, seat);

  return seat;
}

static void
handle_new_input (struct wl_listener *listener, void *data)
{
  struct wlr_input_device *device = data;
  PhocInput *input = wl_container_of (listener, input, new_input);

  char *seat_name = PHOC_CONFIG_DEFAULT_SEAT_NAME;
  PhocSeat *seat = phoc_input_get_seat (input, seat_name);

  if (!seat) {
    g_warning ("could not create PhocSeat");
    return;
  }

  g_debug ("New input device: %s (%d:%d) %s seat:%s", device->name,
           device->vendor, device->product,
           phoc_input_get_device_type (device->type), seat_name);

  phoc_seat_add_device (seat, device);
}


static void
phoc_input_constructed (GObject *object)
{
  PhocInput *self = PHOC_INPUT (object);
  PhocServer *server = phoc_server_get_default ();

  g_debug ("Initializing phoc input");
  assert (server->desktop);

  self->new_input.notify = handle_new_input;
  wl_signal_add (&server->backend->events.new_input, &self->new_input);
  G_OBJECT_CLASS (phoc_input_parent_class)->constructed (object);

}

static void
phoc_input_finalize (GObject *object)
{
  PhocInput *self = PHOC_INPUT (object);

  g_clear_slist (&self->seats, g_object_unref);

  G_OBJECT_CLASS (phoc_input_parent_class)->finalize (object);
}

static void
phoc_input_class_init (PhocInputClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->constructed = phoc_input_constructed;
  object_class->finalize = phoc_input_finalize;
}


static void
phoc_input_init (PhocInput *self)
{
}

PhocInput *
phoc_input_new (void)
{
  return g_object_new (PHOC_TYPE_INPUT, NULL);
}


/**
 * phoc_input_seat_from_wlr_seat:
 * @self: The input
 * @wlr_seat: The wlr_seat
 *
 * Returns: (nullable)(transfer none): The [class@Seat] associated with the given wlr_seat
 */
PhocSeat *
phoc_input_seat_from_wlr_seat (PhocInput       *self,
                               struct wlr_seat *wlr_seat)
{
  g_assert (PHOC_IS_INPUT (self));

  for (GSList *elem = phoc_input_get_seats (self); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    if (seat->seat == wlr_seat) {
      return seat;
    }
  }
  return NULL;
}

bool
phoc_input_view_has_focus (PhocInput *self, PhocView *view)
{
  g_assert (PHOC_IS_INPUT (self));

  if (!view)
    return false;

  for (GSList *elem = phoc_input_get_seats (self); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    if (view == phoc_seat_get_focus (seat)) {
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

  g_assert (PHOC_IS_INPUT (self));

  clock_gettime (CLOCK_MONOTONIC, &now);
  g_assert_nonnull (self);

  for (GSList *elem = phoc_input_get_seats (self); elem; elem = elem->next) {
    PhocSeat *seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (seat));
    phoc_cursor_update_position (phoc_seat_get_cursor (seat),
                                 timespec_to_msec (&now));
  }
}

/**
 * phoc_input_get_seats:
 * @self: The input
 *
 * Returns: (element-type PhocSeat) (transfer none): a list of seats associated with the input
 */
GSList *
phoc_input_get_seats (PhocInput *self)
{
  g_assert (PHOC_IS_INPUT (self));

  return self->seats;
}

/**
 * phoc_input_get_last_active_seat:
 * @self: The input
 *
 * Returns: (nullable) (transfer none): The last active seat or %NULL
 */
PhocSeat *
phoc_input_get_last_active_seat (PhocInput *self)
{
  PhocSeat *seat = NULL;

  g_assert (PHOC_IS_INPUT (self));

  for (GSList *elem = phoc_input_get_seats (self); elem; elem = elem->next) {
    PhocSeat *_seat = PHOC_SEAT (elem->data);

    g_assert (PHOC_IS_SEAT (_seat));
    if (!seat || (seat->seat->last_event.tv_sec > _seat->seat->last_event.tv_sec &&
                  seat->seat->last_event.tv_nsec > _seat->seat->last_event.tv_nsec)) {
      seat = _seat;
    }
  }
  return seat;
}
