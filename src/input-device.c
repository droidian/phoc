/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-input-device"

#include "phoc-config.h"

#include "input-device.h"
#include "seat.h"
#include "utils.h"

#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>

#include <linux/input-event-codes.h>

#define PHOC_INPUT_DEVICE_SELF(p) PHOC_PRIV_CONTAINER(PHOC_INPUT_DEVICE, PhocInputDevice, (p))

enum {
  PROP_0,
  PROP_SEAT,
  PROP_DEVICE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  DEVICE_DESTROY,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

/**
 * PhocInputDevice:
 *
 * Abstract base class for input device like pointers or touch.
 */
typedef struct _PhocInputDevicePrivate {
  struct wlr_input_device *device;
  PhocSeat                *seat;

  char                    *vendor;
  char                    *product;

  struct wl_listener       device_destroy;
} PhocInputDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhocInputDevice, phoc_input_device, G_TYPE_OBJECT)


static void
phoc_input_device_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PhocInputDevice *self = PHOC_INPUT_DEVICE (object);
  PhocInputDevicePrivate *priv = phoc_input_device_get_instance_private (self);

  switch (property_id) {
  case PROP_DEVICE:
    priv->device = g_value_get_pointer (value);
    priv->device->data = self;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEVICE]);
    break;
  case PROP_SEAT:
    priv->seat = g_value_dup_object (value);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SEAT]);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_input_device_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PhocInputDevice *self = PHOC_INPUT_DEVICE (object);
  PhocInputDevicePrivate *priv = phoc_input_device_get_instance_private (self);

  switch (property_id) {
  case PROP_DEVICE:
    g_value_set_pointer (value, priv->device);
    break;
  case PROP_SEAT:
    g_value_set_pointer (value, priv->seat);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
handle_device_destroy (struct wl_listener *listener, void *data)
{
  PhocInputDevicePrivate *priv = wl_container_of (listener, priv, device_destroy);
  PhocInputDevice *self = PHOC_INPUT_DEVICE_SELF (priv);

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  g_debug ("Device destroy %p", self);

  /* Prevent further signal emission */
  wl_list_remove (&priv->device_destroy.link);

  g_signal_emit (self, signals[DEVICE_DESTROY], 0);
}


static void
phoc_input_device_dispose (GObject *object)
{
  PhocInputDevice *self = PHOC_INPUT_DEVICE(object);
  PhocInputDevicePrivate *priv = phoc_input_device_get_instance_private (self);

  g_clear_object (&priv->seat);
  g_clear_pointer (&priv->vendor, g_free);
  g_clear_pointer (&priv->product, g_free);

  G_OBJECT_CLASS (phoc_input_device_parent_class)->dispose (object);
}


static void
phoc_input_device_constructed (GObject *object)
{
  PhocInputDevicePrivate *priv;
  PhocInputDevice *self = PHOC_INPUT_DEVICE (object);

  G_OBJECT_CLASS (phoc_input_device_parent_class)->constructed (object);

  priv = phoc_input_device_get_instance_private (self);
  if (priv->device) {
    priv->device_destroy.notify = handle_device_destroy;
    wl_signal_add (&priv->device->events.destroy, &priv->device_destroy);

    priv->vendor = g_strdup_printf ("%.4x", priv->device->vendor);
    priv->product = g_strdup_printf ("%.4x", priv->device->product);
  }
}


static void
phoc_input_device_class_init (PhocInputDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phoc_input_device_constructed;
  object_class->get_property = phoc_input_device_get_property;
  object_class->set_property = phoc_input_device_set_property;
  object_class->dispose = phoc_input_device_dispose;

  /**
   * PhocInputDevice:device:
   *
   * The underlying wlroots device
   */
  props[PROP_DEVICE] =
    g_param_spec_pointer ("device", "", "",
                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * PhocInputDevice:seat:
   *
   * The seat this device belongs to
   */
  props[PROP_SEAT] =
    g_param_spec_object ("seat", "", "",
                         PHOC_TYPE_SEAT,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhocInputDevice::device-destroy:
   *
   * The underlying wlr input device is about to be destroyed
   */
  signals[DEVICE_DESTROY] = g_signal_new ("device-destroy",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          G_STRUCT_OFFSET (PhocInputDeviceClass, device_destroy),
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);
}


static void
phoc_input_device_init (PhocInputDevice *self)
{
}

/**
 * phoc_input_device_get_seat:
 * @self: The %PhocInputDevice
 *
 * Returns: (transfer none): The seat this input device belongs to.
 */
PhocSeat *
phoc_input_device_get_seat (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return priv->seat;
}

/**
 * phoc_input_device_get_device:
 * @self: The %PhocInputDevice
 *
 * Returns: (transfer none): The wlr_input_device. Note that
 * %PhocInputDevice device owns this so don't keep references around.
 */
struct wlr_input_device *
phoc_input_device_get_device (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return priv->device;
}

/**
 * phoc_input_device_get_is_touchpad:
 * @self: The %PhocInputDevice
 *
 * Returns: %TRUE if this is a touchpad
 */
gboolean
phoc_input_device_get_is_touchpad (PhocInputDevice *self)
{
  struct libinput_device *ldev;
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  if (!wlr_input_device_is_libinput (priv->device))
    return FALSE;

  ldev = phoc_input_device_get_libinput_device_handle (self);
  if (libinput_device_config_tap_get_finger_count (ldev) == 0)
    return FALSE;

  g_debug ("%s is a touchpad device", libinput_device_get_name (ldev));
  return TRUE;
}

/**
 * phoc_input_device_get_is_keyboard:
 * @self: The %PhocInputDevice
 *
 * Returns: %TRUE if this is a keyboard
 */
gboolean
phoc_input_device_get_is_keyboard (PhocInputDevice *self)
{
  struct libinput_device *ldev;
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  if (!wlr_input_device_is_libinput (priv->device))
    return FALSE;

  ldev = phoc_input_device_get_libinput_device_handle (self);
  /* A physical keyboard should at least have a space, enter and a letter */
  if (!libinput_device_keyboard_has_key (ldev, KEY_A) ||
      !libinput_device_keyboard_has_key (ldev, KEY_ENTER) ||
      !libinput_device_keyboard_has_key (ldev, KEY_SPACE)) {
    return FALSE;
  }

  g_debug ("%s is a keyboard device", libinput_device_get_name (ldev));
  return TRUE;
}

/**
 * phoc_input_device_get_is_libinput:
 * @self: The %PhocInputDevice
 *
 * Returns: %TRUE if the device is driven by libinput
 */
gboolean
phoc_input_device_get_is_libinput (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return wlr_input_device_is_libinput (priv->device);
}

/**
 * phoc_input_device_get_libinput_device_handle:
 * @self: The %PhocInputDevice
 *
 * Returns: (nullable): The libinput device
 */
struct libinput_device *
phoc_input_device_get_libinput_device_handle (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return wlr_libinput_get_device_handle (priv->device);
}


/**
 * phoc_input_get_name
 * @self: The %PhocInputDevice
 *
 * Returns: (nullable): The input device name
 */
const char *
phoc_input_device_get_name (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return priv->device->name;
}

/**
 * phoc_input_device_get_device_type:
 * @self: The %PhocInputDevice
 *
 * Returns: The wlr type of the input device
 */
enum wlr_input_device_type
phoc_input_device_get_device_type (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return priv->device->type;
}

/**
 * phoc_input_device_get_vendor_id:
 * @self: The %PhocInputDevice
 *
 * Gets the vendor id as string. This is often represented by a hex
 * number corresponding to the usb vendor id.
 *
 * Returns: The vendor id
 */
const char *
phoc_input_device_get_vendor_id (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return priv->vendor;
}

/**
 * phoc_input_device_get_product_id:
 * @self: The %PhocInputDevice
 *
 * Gets the product id as string. This is often represented by a hex
 * number corresponding to the usb product id.
 *
 * Returns: The vendor id
 */
const char *
phoc_input_device_get_product_id (PhocInputDevice *self)
{
  PhocInputDevicePrivate *priv;

  g_assert (PHOC_IS_INPUT_DEVICE (self));
  priv = phoc_input_device_get_instance_private (self);

  return priv->product;
}
