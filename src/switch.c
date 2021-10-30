#define G_LOG_DOMAIN "phoc-switch"

#include <stdlib.h>
#include <wlr/util/log.h>
#include "switch.h"

enum {
  PROP_0,
  PROP_DEVICE,
  PROP_SEAT,
  PROP_SWITCH_TYPE,
  PROP_STATE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE (PhocSwitch, phoc_switch, G_TYPE_OBJECT);

static void
phoc_switch_set_property (GObject      *object,
                          guint        property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  PhocSwitch *self = PHOC_SWITCH (object);

  switch (property_id)
  {
  case PROP_DEVICE:
    self->device = g_value_get_pointer (value);
    self->device->data = self;
    self->device->switch_device->data = self;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEVICE]);
    break;
  case PROP_SEAT:
    self->seat = g_value_get_pointer (value);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SEAT]);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_switch_get_property (GObject      *object,
                          guint        property_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  PhocSwitch *self = PHOC_SWITCH (object);

  switch (property_id)
  {
  case PROP_DEVICE:
    g_value_set_pointer (value, self->device);
    break;
  case PROP_SEAT:
    g_value_set_pointer (value, self->seat);
    break;
  case PROP_SWITCH_TYPE:
    g_value_set_uint (value, self->switch_type);
    break;
  case PROP_STATE:
    g_value_set_uint (value, self->state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_switch_constructed (GObject *object)
{
  PhocSwitch *self = PHOC_SWITCH (object);

  self->switch_type = PHOC_SWITCH_NONE; /* We don't know yet */
  self->state = 0;

  G_OBJECT_CLASS (phoc_switch_parent_class)->constructed (object);
}

static void
phoc_switch_finalize (GObject *object)
{
  PhocSwitch *self = PHOC_SWITCH (object);

  wl_list_remove(&self->link);

  G_OBJECT_CLASS (phoc_switch_parent_class)->finalize (object);
}

static void
phoc_switch_class_init (PhocSwitchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = phoc_switch_set_property;
  object_class->get_property = phoc_switch_get_property;

  object_class->constructed = phoc_switch_constructed;
  object_class->finalize = phoc_switch_finalize;

  props[PROP_DEVICE] =
    g_param_spec_pointer (
      "device",
      "Device",
      "The device object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_SEAT] =
    g_param_spec_pointer (
      "seat",
      "Seat",
      "The seat this keyboard belongs to",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /* FIXME: turn it into an enum */
  props[PROP_SWITCH_TYPE] = g_param_spec_uint ("switch-type", "Switch Type",
                                               "The switch type.",
                                               0, 3, 0,
                                               G_PARAM_READABLE);

  props[PROP_STATE] = g_param_spec_uint ("state", "Switch state",
                                          "The switch state.",
                                          0, 1, 0,
                                          G_PARAM_READABLE);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

static void
phoc_switch_init (PhocSwitch *self)
{
}

PhocSwitch *
phoc_switch_new (struct wlr_input_device *device, PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_SWITCH,
                       "device", device,
                       "seat", seat,
                       NULL);
}

void
phoc_switch_handle_toggle (PhocSwitch *switch_device,
                           struct wlr_event_switch_toggle *event)
{
  switch_device->switch_type = event->switch_type;
  switch_device->state = event->switch_state;

  /* TODO: emit property changed signals */

  g_debug ("Handling switch event %i (state %i)", event->switch_type,
           event->switch_state);

  /* Forward to phosh */
  if (!phoc_phosh_private_forward_switch_event (switch_device->switch_type, switch_device->state))
    g_warning ("Unable to forward event state");
}
