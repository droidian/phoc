#define G_LOG_DOMAIN "phoc-virtual"

#include "config.h"

#define _POSIX_C_SOURCE 199309L
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include "virtual.h"
#include "seat.h"

void
phoc_handle_virtual_keyboard (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop =
    wl_container_of (listener, desktop, virtual_keyboard_new);
  struct wlr_virtual_keyboard_v1 *keyboard = data;

  struct roots_seat *seat = phoc_input_seat_from_wlr_seat (server->input,
						      keyboard->seat);
  g_return_if_fail (seat);

  roots_seat_add_device (seat, &keyboard->input_device);
}

void
phoc_handle_virtual_pointer(struct wl_listener *listener, void *data)
{
  PhocDesktop *desktop =
    wl_container_of(listener, desktop, virtual_pointer_new);
  struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
  struct wlr_virtual_pointer_v1 *pointer = event->new_pointer;
  struct wlr_input_device *device = &pointer->input_device;
  char *seat_name = ROOTS_CONFIG_DEFAULT_SEAT_NAME;
  PhocServer *server = phoc_server_get_default ();
  struct roots_seat *seat;

  g_return_if_fail (PHOC_IS_DESKTOP (desktop));
  g_return_if_fail (PHOC_IS_SERVER (server));
  seat = phoc_input_get_seat (server->input, seat_name);
  g_return_if_fail (seat);

  g_debug ("New virtual input device: %s (%d:%d) %s seat:%s", device->name,
	   device->vendor, device->product,
	   phoc_input_get_device_type(device->type), seat_name);

  roots_seat_add_device (seat, device);

#if 0 /* TODO: can be enabled after next wlroots release */
  if (event->suggested_output) {
    wlr_cursor_map_input_to_output(seat->cursor->cursor, device,
				   event->suggested_output);
  }
#endif
}
