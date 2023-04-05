#define G_LOG_DOMAIN "phoc-switch"

#include "phoc-config.h"

#include "phosh-private.h"

#include "switch.h"

#include <wlr/backend/libinput.h>

enum {
  TOGGLED,
  N_SIGNALS,
};
static guint signals[N_SIGNALS];

/**
 * PhocSwitch:
 *
 * A switch device. E.g. a tablet mode switch or laptop lid switch
 */
struct _PhocSwitch {
  PhocInputDevice          parent;

  struct wl_listener       toggle;
  PhocSwitchState          state;
};

G_DEFINE_TYPE (PhocSwitch, phoc_switch, PHOC_TYPE_INPUT_DEVICE);


static void
handle_switch_toggle (struct wl_listener *listener, void *data)
{
  PhocSwitch *self = wl_container_of (listener, self, toggle);
  struct wlr_switch_toggle_event *event = data;

  g_debug ("Switch %s, type: %d, state: %d",
           phoc_input_device_get_name (PHOC_INPUT_DEVICE (self)),
           event->switch_type,
           event->switch_state);

  self->state = !!event->switch_state ? PHOC_SWITCH_STATE_ON : PHOC_SWITCH_STATE_OFF;

  g_signal_emit (self, signals[TOGGLED], 0, !!event->switch_state);
}


static void
phoc_switch_constructed (GObject *object)
{
  PhocSwitch *self = PHOC_SWITCH (object);
  struct wlr_input_device *device = phoc_input_device_get_device (PHOC_INPUT_DEVICE (self));
  struct wlr_switch *wlr_switch = wlr_switch_from_input_device (device);

  G_OBJECT_CLASS (phoc_switch_parent_class)->constructed (object);

  self->toggle.notify = handle_switch_toggle;
  wl_signal_add (&wlr_switch->events.toggle, &self->toggle);
}


static void
phoc_switch_finalize (GObject *object)
{
  PhocSwitch *self = PHOC_SWITCH (object);

  wl_list_remove (&self->toggle.link);

  G_OBJECT_CLASS (phoc_switch_parent_class)->finalize (object);
}


static void
phoc_switch_class_init (PhocSwitchClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->constructed = phoc_switch_constructed;
  object_class->finalize = phoc_switch_finalize;

  signals[TOGGLED] =
    g_signal_new ("toggled",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_BOOLEAN);
}


static void
phoc_switch_init (PhocSwitch *self)
{
  /* libinput will notify us if switch is on initially */
  self->state = PHOC_SWITCH_STATE_OFF;
}


PhocSwitch *
phoc_switch_new (struct wlr_input_device *device, PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_SWITCH,
                       "device", device,
                       "seat", seat,
                       NULL);
}


gboolean
phoc_switch_is_tablet_mode_switch (PhocSwitch *self)
{
  struct libinput_device *ldev;

  if (!phoc_input_device_get_is_libinput (PHOC_INPUT_DEVICE (self)))
    return FALSE;

  ldev = phoc_input_device_get_libinput_device_handle (PHOC_INPUT_DEVICE (self));

  return libinput_device_switch_has_switch (ldev, LIBINPUT_SWITCH_TABLET_MODE) > 0;
}


gboolean
phoc_switch_is_lid_switch (PhocSwitch *self)
{
  struct libinput_device *ldev;

  if (!phoc_input_device_get_is_libinput (PHOC_INPUT_DEVICE (self)))
    return FALSE;

  ldev = phoc_input_device_get_libinput_device_handle (PHOC_INPUT_DEVICE (self));

  return libinput_device_switch_has_switch (ldev, LIBINPUT_SWITCH_LID) > 0;
}


gboolean
phoc_switch_is_keypad_slide (PhocSwitch *self)
{
  struct libinput_device *ldev;

  if (!phoc_input_device_get_is_libinput (PHOC_INPUT_DEVICE (self)))
    return FALSE;

  ldev = phoc_input_device_get_libinput_device_handle (PHOC_INPUT_DEVICE (self));

  return libinput_device_switch_has_switch (ldev, 3 /* LIBINPUT_SWITCH_KEYPAD_SLIDE */) > 0;
}

gboolean
phoc_switch_is_type (PhocSwitch *self, enum wlr_switch_type type)
{
  switch (type) {
  case WLR_SWITCH_TYPE_LID:
    return phoc_switch_is_lid_switch (self);
  case WLR_SWITCH_TYPE_KEYPAD_SLIDE:
    return phoc_switch_is_keypad_slide (self);
  case WLR_SWITCH_TYPE_TABLET_MODE:
    return phoc_switch_is_tablet_mode_switch (self);
  default:
    g_assert_not_reached ();
  }
}

gboolean
phoc_switch_get_state (PhocSwitch *self)
{
  return self->state;
}
