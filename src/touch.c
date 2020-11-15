#define G_LOG_DOMAIN "phoc-touch"

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
phoc_touch_new (struct wlr_input_device *device, struct roots_seat *seat)
{
  return g_object_new (PHOC_TYPE_TOUCH,
                       "device", device,
                       "seat", seat,
                       NULL);
}

static void
phoc_touch_constructed (GObject *object)
{
  G_OBJECT_CLASS (phoc_touch_parent_class)->constructed (object);
}

static void
phoc_touch_finalize (GObject *object)
{
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
}
