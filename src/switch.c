#define G_LOG_DOMAIN "phoc-switch"

#include "phoc-config.h"

#include <stdlib.h>
#include "switch.h"

void
phoc_switch_handle_toggle (struct phoc_switch *switch_device,
                           struct wlr_switch_toggle_event *event)
{
  g_debug ("Switch %s, type: %d, state: %d",
           switch_device->device->name,
           event->switch_type,
           event->switch_state);
}
