#pragma once

#include "input.h"

G_BEGIN_DECLS

typedef struct phoc_switch {
	PhocSeat *seat;
	struct wlr_input_device *device;
	struct wl_listener device_destroy;

	struct wl_listener toggle;
	struct wl_list link;
} PhocSwitch;

void phoc_switch_handle_toggle(struct phoc_switch *switch_device,
		struct wlr_event_switch_toggle *event);

G_END_DECLS
