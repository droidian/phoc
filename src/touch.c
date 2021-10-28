#define G_LOG_DOMAIN "phoc-touch"

#include "config.h"

#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/libinput.h>

#include "touch.h"
#include "seat.h"

G_DEFINE_TYPE (PhocTouch, phoc_touch, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_SEAT,
  PROP_DEVICE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  TOUCH_DESTROY,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

static void
phoc_touch_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  PhocTouch *self = PHOC_TOUCH (object);

  switch (property_id) {
  case PROP_DEVICE:
    self->device = g_value_get_pointer (value);
    self->device->data = self;
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
phoc_touch_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  PhocTouch *self = PHOC_TOUCH (object);

  switch (property_id) {
  case PROP_DEVICE:
    g_value_set_pointer (value, self->device);
    break;
  case PROP_SEAT:
    g_value_set_pointer (value, self->seat);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phoc_touch_init (PhocTouch *self)
{
}

PhocTouch *
phoc_touch_new (struct wlr_input_device *device, PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_TOUCH,
                       "device", device,
                       "seat", seat,
                       NULL);
}

static void
handle_touch_destroy (struct wl_listener *listener, void *data)
{
  PhocTouch *self = wl_container_of (listener, self, touch_destroy);

  g_signal_emit (self, signals[TOUCH_DESTROY], 0);
}

static void
phoc_touch_constructed (GObject *object)
{
  PhocTouch *self = PHOC_TOUCH (object);

  self->touch_destroy.notify = handle_touch_destroy;
  wl_signal_add (&self->device->events.destroy, &self->touch_destroy);

  G_OBJECT_CLASS (phoc_touch_parent_class)->constructed (object);
}

static void
phoc_touch_finalize (GObject *object)
{
  PhocTouch *self = PHOC_TOUCH (object);

  wl_list_remove (&self->touch_destroy.link);

  G_OBJECT_CLASS (phoc_touch_parent_class)->finalize (object);
}

static void
phoc_touch_dispose (GObject *object)
{
  G_OBJECT_CLASS (phoc_touch_parent_class)->dispose (object);
}

static void
phoc_touch_class_init (PhocTouchClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->set_property = phoc_touch_set_property;
  object_class->get_property = phoc_touch_get_property;

  object_class->constructed = phoc_touch_constructed;
  object_class->dispose = phoc_touch_dispose;
  object_class->finalize = phoc_touch_finalize;

  props[PROP_DEVICE] =
    g_param_spec_pointer (
      "device",
      "Device",
      "The device object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_SEAT] =
    g_param_spec_pointer (
      "seat",
      "Seat",
      "The seat object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[TOUCH_DESTROY] = g_signal_new ("touch-destroyed",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_LAST,
                                         0, NULL, NULL, NULL, G_TYPE_NONE, 0);

}
