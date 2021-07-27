#define G_LOG_DOMAIN "phoc-pointer"

#include "config.h"

#include "pointer.h"
#include "seat.h"

#include <glib.h>
#include <gdesktop-enums.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/backend/libinput.h>

enum {
  PROP_0,
  PROP_DEVICE,
  PROP_SEAT,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE (PhocPointer, phoc_pointer, G_TYPE_OBJECT);


static void
check_touchpad (PhocPointer *self)
{
  struct libinput_device *ldev;

  if (!wlr_input_device_is_libinput (self->device))
    return;

  ldev = wlr_libinput_get_device_handle(self->device);
  if (libinput_device_config_tap_get_finger_count (ldev) == 0)
    return;

  g_debug ("%s is a touchpad device", self->device->name);
  self->touchpad = TRUE;
}


static void
phoc_pointer_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  PhocPointer *self = PHOC_POINTER (object);

  switch (property_id) {
  case PROP_DEVICE:
    self->device = g_value_get_pointer (value);
    self->device->data = self;
    check_touchpad (self);
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
phoc_pointer_get_property (GObject    *object,
                           guint       property_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  PhocPointer *self = PHOC_POINTER (object);

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
on_mouse_settings_changed (PhocPointer *self,
			   const gchar *key,
			   GSettings   *settings)
{
  struct libinput_device *ldev;
  gboolean enabled;
  gdouble speed;

  g_debug ("Setting changed, reloading mouse settings");

  g_assert (PHOC_IS_POINTER (self));
  g_assert (G_IS_SETTINGS (settings));

  if (!wlr_input_device_is_libinput (self->device))
    return;

  ldev = wlr_libinput_get_device_handle(self->device);
  if (libinput_device_config_scroll_has_natural_scroll (ldev)) {
    enabled = g_settings_get_boolean (settings, "natural-scroll");
    libinput_device_config_scroll_set_natural_scroll_enabled (ldev, enabled);
  }

  if (libinput_device_config_middle_emulation_is_available (ldev)) {
    enabled = g_settings_get_boolean (settings, "middle-click-emulation");
    libinput_device_config_middle_emulation_set_enabled (ldev, enabled);
  }

  speed = g_settings_get_double (settings, "speed");
  libinput_device_config_accel_set_speed (ldev,
                                          CLAMP (speed, -1, 1));

  if (libinput_device_config_left_handed_is_available (ldev)) {
    enabled = g_settings_get_boolean (self->mouse_settings, "left-handed");
    libinput_device_config_left_handed_set (ldev, enabled);
  }
}


static void
on_touchpad_settings_changed (PhocPointer *self,
			      const gchar *key)
{
  struct libinput_device *ldev;
  gboolean enabled;
  gdouble speed;
  enum libinput_config_scroll_method current, method;
  GDesktopTouchpadHandedness handedness;
  GSettings *settings;

  g_debug ("Setting changed, reloading touchpad settings");

  g_assert (PHOC_IS_POINTER (self));

  settings = self->touchpad_settings;
  if (!wlr_input_device_is_libinput (self->device))
    return;

  ldev = wlr_libinput_get_device_handle(self->device);

  if (libinput_device_config_scroll_has_natural_scroll (ldev)) {
    enabled = g_settings_get_boolean (settings, "natural-scroll");
    libinput_device_config_scroll_set_natural_scroll_enabled (ldev, enabled);
  }

  enabled = g_settings_get_boolean (settings, "tap-to-click");
  libinput_device_config_tap_set_enabled (ldev, enabled ?
					  LIBINPUT_CONFIG_TAP_ENABLED :
					  LIBINPUT_CONFIG_TAP_DISABLED);

  enabled = g_settings_get_boolean (settings, "tap-and-drag");
  libinput_device_config_tap_set_drag_enabled (ldev,
					       enabled ?
					       LIBINPUT_CONFIG_DRAG_ENABLED :
					       LIBINPUT_CONFIG_DRAG_DISABLED);

  enabled = g_settings_get_boolean (settings, "tap-and-drag-lock");
  libinput_device_config_tap_set_drag_lock_enabled (ldev,
						    enabled ?
						    LIBINPUT_CONFIG_DRAG_LOCK_ENABLED :
						    LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);

  enabled = g_settings_get_boolean (settings, "disable-while-typing");
  libinput_device_config_dwt_set_enabled (ldev,
					  enabled ?
					  LIBINPUT_CONFIG_DWT_ENABLED :
					  LIBINPUT_CONFIG_DWT_DISABLED);

  if (libinput_device_config_middle_emulation_is_available (ldev)) {
    enabled = g_settings_get_boolean (settings, "middle-click-emulation");
    libinput_device_config_middle_emulation_set_enabled (ldev, enabled);
  }

  current = libinput_device_config_scroll_get_method (ldev);
  current &= ~(LIBINPUT_CONFIG_SCROLL_EDGE | LIBINPUT_CONFIG_SCROLL_2FG);
  enabled = g_settings_get_boolean (settings, "edge-scrolling-enabled");
  method = enabled ? LIBINPUT_CONFIG_SCROLL_EDGE : LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
  current |= method;
  enabled = g_settings_get_boolean (settings, "two-finger-scrolling-enabled");
  method = enabled ? LIBINPUT_CONFIG_SCROLL_2FG : LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
  current |= method;
  libinput_device_config_scroll_set_method (ldev, current | method);

  speed = g_settings_get_double (settings, "speed");
  libinput_device_config_accel_set_speed (ldev,
                                          CLAMP (speed, -1, 1));

  handedness = g_settings_get_enum (self->touchpad_settings, "left-handed");
  switch (handedness) {
  case G_DESKTOP_TOUCHPAD_HANDEDNESS_RIGHT:
    enabled = FALSE;
    break;
  case G_DESKTOP_TOUCHPAD_HANDEDNESS_LEFT:
    enabled = TRUE;
    break;
  case G_DESKTOP_TOUCHPAD_HANDEDNESS_MOUSE:
    enabled = g_settings_get_boolean (self->mouse_settings, "left-handed");
    break;
  default:
    g_assert_not_reached ();
  }
  if (libinput_device_config_left_handed_is_available (ldev))
    libinput_device_config_left_handed_set (ldev, enabled);
}


static void
phoc_pointer_constructed (GObject *object)
{
  PhocPointer *self = PHOC_POINTER (object);

  G_OBJECT_CLASS (phoc_pointer_parent_class)->constructed (object);

  self->mouse_settings = g_settings_new ("org.gnome.desktop.peripherals.mouse");
  if (self->touchpad) {
    self->touchpad_settings = g_settings_new ("org.gnome.desktop.peripherals.touchpad");

    g_signal_connect_swapped (self->touchpad_settings,
			      "changed",
			      G_CALLBACK (on_touchpad_settings_changed),
			      self);
    /* "left-handed" is read from mouse settings */
    g_signal_connect_swapped (self->mouse_settings,
			      "changed::left-handed",
			      G_CALLBACK (on_touchpad_settings_changed),
			      self);
    on_touchpad_settings_changed (self, NULL);
  } else {
    g_signal_connect_swapped (self->mouse_settings,
			      "changed",
			      G_CALLBACK (on_mouse_settings_changed),
			      self);
    on_mouse_settings_changed (self, NULL, self->mouse_settings);
  }
}


static void
phoc_pointer_dispose(GObject *object)
{
  PhocPointer *self = PHOC_POINTER (object);

  g_clear_object (&self->touchpad_settings);
  g_clear_object (&self->mouse_settings);

  G_OBJECT_CLASS (phoc_pointer_parent_class)->dispose (object);
}


static void
phoc_pointer_class_init (PhocPointerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->set_property = phoc_pointer_set_property;
  object_class->get_property = phoc_pointer_get_property;

  object_class->constructed = phoc_pointer_constructed;
  object_class->dispose = phoc_pointer_dispose;

  props[PROP_DEVICE] =
    g_param_spec_pointer ("device",
			  "Device",
			  "The device object",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_SEAT] =
    g_param_spec_pointer ("seat",
			  "Seat",
			  "The seat this pointer belongs to",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

static void
phoc_pointer_init (PhocPointer *self)
{
}

PhocPointer *
phoc_pointer_new (struct wlr_input_device *device, struct roots_seat *seat)
{
  return g_object_new (PHOC_TYPE_POINTER,
                       "device", device,
                       "seat", seat,
                       NULL);
}
