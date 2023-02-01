#pragma once

#include "input.h"

G_BEGIN_DECLS

#define PHOC_TYPE_SWITCH phoc_switch_get_type ()
G_DECLARE_FINAL_TYPE (PhocSwitch, phoc_switch, PHOC, SWITCH, GObject)

typedef enum _PhocSwitchType
{
  PHOC_SWITCH_NONE,
  PHOC_SWITCH_LID,
  PHOC_SWITCH_TABLET_MODE,
  PHOC_SWITCH_KEYPAD_SLIDE,
} PhocSwitchType;

struct _PhocSwitch {
  GObject parent;

  struct wl_listener device_destroy;
  struct wl_listener toggle;
  struct wl_list link;

  guint switch_type;
  guint state;

  PhocSeat *seat;
  struct wlr_input_device *device;
};

PhocSwitch *phoc_switch_new (struct wlr_input_device *device,
                             PhocSeat *seat);

void phoc_switch_handle_toggle(PhocSwitch *switch_device,
		struct wlr_event_switch_toggle *event);

G_END_DECLS
