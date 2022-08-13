#define G_LOG_DOMAIN "phoc-switch"

#include "config.h"

#include <stdlib.h>
#include "switch.h"

void phoc_switch_handle_toggle(struct phoc_switch *switch_device,
		struct wlr_event_switch_toggle *event) {
	g_debug ("Switch %s, type: %d, state: %d",
		 event->device->name,
		 event->switch_type,
		 event->switch_state);
}
