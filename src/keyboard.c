/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *         The wlroots authors
 */

#define G_LOG_DOMAIN "phoc-keyboard"

#include "phoc-config.h"
#include "server.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>
#include "keyboard.h"
#include "phosh-private.h"
#include "seat.h"

#include <glib.h>
#include <glib/gprintf.h>
#include <wlr/backend/libinput.h>

#define WAKEUP_KEY_UDEV_PREFIX "GM_WAKEUP_KEY_"

/**
 * PhocWakeupKey:
 * @PHOC_WAKEUP_KEY_IGNORE: The key should not trigger activity notifications.
 * @PHOC_WAKEUP_KEY_USE: The key should trigger activity notifications.
 *
 * Describes whether a particular key should trigger activity notifications.
 *
 * 0 is intentionally skipped as it's the same as NULL (use default) when stored in a GHashTable.
 */
typedef enum {
  PHOC_WAKEUP_KEY_IGNORE = 1,
  PHOC_WAKEUP_KEY_USE = 2,
} PhocWakeupKey;

/**
 * PhocKeyboard:
 *
 * A keyboard input device
 *
 * It's responsible for forwarding keys press and modifier changes
 * to the seat, it tracks keybindings and the keymap.
 */

enum {
  ACTIVITY,
  N_SIGNALS
};
static guint signals[N_SIGNALS];


struct _PhocKeyboard {
  PhocInputDevice    parent;

  struct wl_listener keyboard_key;
  struct wl_listener keyboard_modifiers;

  GSettings         *input_settings;
  GSettings         *keyboard_settings;
  struct xkb_keymap *keymap;
  GnomeXkbInfo      *xkbinfo;

  gboolean           wakeup_key_default;
  GHashTable        *wakeup_keys;

  xkb_keysym_t       pressed_keysyms_translated[PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP];
  xkb_keysym_t       pressed_keysyms_raw[PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP];

  uint32_t           meta_key;
  bool               meta_press_valid;
};
G_DEFINE_TYPE (PhocKeyboard, phoc_keyboard, PHOC_TYPE_INPUT_DEVICE)


static ssize_t
pressed_keysyms_index (const xkb_keysym_t *pressed_keysyms, xkb_keysym_t keysym)
{
  for (size_t i = 0; i < PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
    if (pressed_keysyms[i] == keysym)
      return i;
  }
  return -1;
}


static size_t
pressed_keysyms_length (const xkb_keysym_t *pressed_keysyms)
{
  size_t n = 0;

  for (size_t i = 0; i < PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
    if (pressed_keysyms[i] != XKB_KEY_NoSymbol)
      ++n;
  }
  return n;
}


static void
pressed_keysyms_add (xkb_keysym_t *pressed_keysyms, xkb_keysym_t keysym)
{
  ssize_t i = pressed_keysyms_index (pressed_keysyms, keysym);
  if (i < 0) {
    i = pressed_keysyms_index (pressed_keysyms, XKB_KEY_NoSymbol);
    if (i >= 0)
      pressed_keysyms[i] = keysym;
  }
}


static void
pressed_keysyms_remove (xkb_keysym_t *pressed_keysyms, xkb_keysym_t keysym)
{
  ssize_t i = pressed_keysyms_index (pressed_keysyms, keysym);
  if (i >= 0)
    pressed_keysyms[i] = XKB_KEY_NoSymbol;
}


static bool
keysym_is_modifier (xkb_keysym_t keysym)
{
  switch (keysym) {
  case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
  case XKB_KEY_Control_L: case XKB_KEY_Control_R:
  case XKB_KEY_Caps_Lock:
  case XKB_KEY_Shift_Lock:
  case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
  case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
  case XKB_KEY_Super_L: case XKB_KEY_Super_R:
  case XKB_KEY_Hyper_L: case XKB_KEY_Hyper_R:
    return true;
  default:
    return false;
  }
}

static void
pressed_keysyms_update (xkb_keysym_t              *pressed_keysyms,
                        const xkb_keysym_t        *keysyms,
                        size_t                     keysyms_len,
                        enum wl_keyboard_key_state state)
{
  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keysym_is_modifier (keysyms[i]))
      continue;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
      pressed_keysyms_add (pressed_keysyms, keysyms[i]);
    else /* WL_KEYBOARD_KEY_STATE_RELEASED */
      pressed_keysyms_remove (pressed_keysyms, keysyms[i]);
  }
}

/*
 * Execute a built-in, hardcoded compositor binding. These are triggered from a
 * single keysym.
 *
 * Returns `true` if the keysym was handled by a binding and `false` if the event
 * should be propagated further.
 */
static bool
keyboard_execute_compositor_binding (PhocKeyboard *self, xkb_keysym_t keysym)
{
  PhocServer *server = phoc_server_get_default ();

  if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
    struct wlr_session *session = phoc_server_get_session (server);

    if (session) {
      unsigned vt = keysym - XKB_KEY_XF86Switch_VT_1 + 1;
      wlr_session_change_vt (session, vt);
    }

    return true;
  }

  if (keysym == XKB_KEY_Escape) {
    PhocSeat *seat = phoc_input_device_get_seat (PHOC_INPUT_DEVICE (self));

    wlr_seat_pointer_end_grab (seat->seat);
    wlr_seat_keyboard_end_grab (seat->seat);
    wlr_seat_touch_end_grab (seat->seat);
    phoc_seat_end_compositor_grab (seat);
  }

  return false;
}

/*
 * Execute super key. This function will detect if the super key has been pressed
 * or released. If between a press and release no other keys are press then a
 * keysym will be forwarded to phosh. Otherwise nothing happens.
 *
 * Returns `true` if the keysym was handled by a binding and `false` if the event
 * should be propagated further.
 */
static bool
keyboard_execute_super_key (PhocKeyboard              *self,
                            const xkb_keysym_t        *keysyms,
                            size_t                     keysyms_len,
                            uint32_t                   modifiers,
                            uint32_t                   time,
                            enum wl_keyboard_key_state state)
{
  uint32_t super_mod = 1 << xkb_map_mod_get_index (self->keymap, "Mod4");

  if (modifiers > 0 && modifiers != super_mod) {
    /* Check if other modifiers have been pressed e.g. <ctrl><super> */
    return false;
  }

  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keysyms[i] == XKB_KEY_Super_L || keysyms[i] == XKB_KEY_Super_R) {
      PhocKeyCombo combo = { 0, keysyms[i] };

      if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* The super key was pressed wait for a release */
        self->meta_press_valid = true;
        return phoc_phosh_private_forward_keysym (&combo, time, false);
      } else if (self->meta_press_valid == true) {
        /* The super key was released and is valid (no other key was pressed) */
        self->meta_press_valid = false;
        return phoc_phosh_private_forward_keysym (&combo, time, true);
      }

      return false;
    } else {
      /* A non super key was pressed, don't wait for the release
       * as the super key is a modifier.
       */
      self->meta_press_valid = false;
    }
  }

  return false;
}

/*
 * Execute keyboard bindings. These include compositor bindings and user-defined
 * bindings.
 *
 * Returns `true` if the keysym was handled by a binding and `false` if the event
 * should be propagated further.
 */
static bool
keyboard_execute_binding (PhocKeyboard              *self,
                          xkb_keysym_t              *pressed_keysyms,
                          uint32_t                   modifiers,
                          const xkb_keysym_t        *keysyms,
                          size_t                     keysyms_len,
                          enum wl_keyboard_key_state state)
{
  PhocConfig *config = phoc_server_get_config (phoc_server_get_default ());
  PhocSeat *seat = phoc_input_device_get_seat (PHOC_INPUT_DEVICE (self));

  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
    return false;

  /* TODO: should be handled via PhocKeybindings as well */
  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keyboard_execute_compositor_binding (self, keysyms[i]))
      return true;
  }

  size_t n = pressed_keysyms_length (pressed_keysyms);
  if (phoc_keybindings_handle_pressed (config->keybindings, modifiers, pressed_keysyms, n, seat))
    return true;

  return false;
}


/*
 * Forward keyboard bindings.
 *
 * Returns `true` if the keysym was handled by forwarding and `false` if the event
 * should be propagated further.
 */
static bool
keyboard_execute_subscribed_binding (PhocKeyboard              *self,
                                     xkb_keysym_t              *pressed_keysyms,
                                     uint32_t                   modifiers,
                                     const xkb_keysym_t        *keysyms,
                                     size_t                     keysyms_len,
                                     uint32_t                   time,
                                     enum wl_keyboard_key_state state)
{
  bool handled = false;
  bool pressed;

  pressed = !!(state == WL_KEYBOARD_KEY_STATE_PRESSED);
  for (size_t i = 0; i < keysyms_len; ++i) {
    PhocKeyCombo combo = { modifiers, keysyms[i] };
    handled |= phoc_phosh_private_forward_keysym (&combo, time, pressed);
  }
  return handled;
}

/*
 * Get keysyms and modifiers from the keyboard as xkb sees them.
 *
 * This uses the xkb keysyms translation based on pressed modifiers and clears
 * the consumed modifiers from the list of modifiers passed to keybind
 * detection.
 *
 * On US layout, pressing Alt+Shift+2 will trigger Alt+@.
 */
static size_t
keyboard_keysyms_translated (PhocKeyboard        *self,
                             xkb_keycode_t        keycode,
                             const xkb_keysym_t **keysyms,
                             uint32_t            *modifiers)
{
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device (device);

  *modifiers = wlr_keyboard_get_modifiers (wlr_keyboard);
  xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods2 (
    wlr_keyboard->xkb_state, keycode, XKB_CONSUMED_MODE_XKB);
  *modifiers = *modifiers & ~consumed;

  return xkb_state_key_get_syms (wlr_keyboard->xkb_state,
                                 keycode, keysyms);
}

/*
 * Get keysyms and modifiers from the keyboard as if modifiers didn't change
 * keysyms.
 *
 * This avoids the xkb keysym translation based on modifiers considered pressed
 * in the state.
 *
 * This will trigger keybinds such as Alt+Shift+2.
 */
static size_t
keyboard_keysyms_raw (PhocKeyboard        *self,
                      xkb_keycode_t        keycode,
                      const xkb_keysym_t **keysyms,
                      uint32_t            *modifiers)
{
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device (device);

  *modifiers = wlr_keyboard_get_modifiers (wlr_keyboard);

  xkb_layout_index_t layout_index = xkb_state_key_get_layout (wlr_keyboard->xkb_state,
                                                              keycode);
  return xkb_keymap_key_get_syms_by_level (wlr_keyboard->keymap,
                                           keycode, layout_index, 0, keysyms);
}


static struct wlr_input_method_keyboard_grab_v2 *
phoc_keyboard_get_grab (PhocKeyboard *self)
{
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  PhocSeat *seat = phoc_input_device_get_seat (input_device);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_input_method_v2 *input_method = seat->im_relay.input_method;
  struct wlr_virtual_keyboard_v1 *virtual_keyboard =
    wlr_input_device_get_virtual_keyboard (device);

  if (!input_method || !input_method->keyboard_grab)
    return NULL;

  /* Do not forward virtual events back to the client that generated them */
  if (virtual_keyboard
      && wl_resource_get_client (input_method->keyboard_grab->resource)
      == wl_resource_get_client (virtual_keyboard->resource))
    return NULL;

  return input_method->keyboard_grab;
}


static void
phoc_keyboard_handle_key (PhocKeyboard *self, struct wlr_keyboard_key_event *event)
{
  xkb_keycode_t keycode = event->keycode + 8;
  bool handled = false;
  uint32_t modifiers;
  const xkb_keysym_t *keysyms;
  size_t keysyms_len;

  /* Handle translated keysyms */
  keysyms_len = keyboard_keysyms_translated (self, keycode, &keysyms, &modifiers);
  pressed_keysyms_update (self->pressed_keysyms_translated, keysyms, keysyms_len, event->state);
  handled = keyboard_execute_binding (self,
                                      self->pressed_keysyms_translated, modifiers, keysyms,
                                      keysyms_len, event->state);

  keysyms_len = keyboard_keysyms_raw (self, keycode, &keysyms, &modifiers);
  pressed_keysyms_update (self->pressed_keysyms_raw, keysyms, keysyms_len,
                          event->state);
  /*  Handle raw keysyms */
  if (!handled) {
    handled = keyboard_execute_binding (self,
                                        self->pressed_keysyms_raw, modifiers, keysyms,
                                        keysyms_len, event->state);
  }

  /* Check for the super key */
  if (!handled)
    handled = keyboard_execute_super_key (self, keysyms, keysyms_len, modifiers, event->time_msec,
                                          event->state);

  /* Handle subscribed keysyms */
  if (!handled) {
    handled = keyboard_execute_subscribed_binding (self,
                                                   self->pressed_keysyms_raw, modifiers,
                                                   keysyms, keysyms_len, event->time_msec,
                                                   event->state);
  }

  if (!handled) {
    PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
    struct wlr_input_device *device = phoc_input_device_get_device (input_device);
    struct wlr_input_method_keyboard_grab_v2 *grab = phoc_keyboard_get_grab (self);

    if (grab) {
      wlr_input_method_keyboard_grab_v2_set_keyboard (grab,
                                                      wlr_keyboard_from_input_device (device));
      wlr_input_method_keyboard_grab_v2_send_key (grab,
                                                  event->time_msec,
                                                  event->keycode,
                                                  event->state);
    } else {
      PhocSeat *seat = phoc_input_device_get_seat (input_device);
      wlr_seat_set_keyboard (seat->seat, wlr_keyboard_from_input_device (device));
      wlr_seat_keyboard_notify_key (seat->seat,
                                    event->time_msec,
                                    event->keycode,
                                    event->state);
    }
  }
}


static void
phoc_keyboard_handle_modifiers (PhocKeyboard *self)
{
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_input_method_keyboard_grab_v2 *grab = phoc_keyboard_get_grab (self);
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device (device);

  if (grab) {
    wlr_input_method_keyboard_grab_v2_set_keyboard (grab, wlr_keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers (grab, &wlr_keyboard->modifiers);
  } else {
    PhocSeat *seat = phoc_input_device_get_seat (input_device);
    wlr_seat_set_keyboard (seat->seat, wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers (seat->seat, &wlr_keyboard->modifiers);
  }
}


static void
set_fallback_keymap (PhocKeyboard *self)
{
  struct xkb_context *context;
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device (device);

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  if (context == NULL)
    return;

  xkb_keymap_unref (self->keymap);
  self->keymap = xkb_keymap_new_from_names (context, NULL,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  wlr_keyboard_set_keymap (wlr_keyboard, self->keymap);
}


static void
set_xkb_keymap (PhocKeyboard *self, const gchar *layout, const gchar *variant, const gchar *options)
{
  struct xkb_rule_names rules = { 0 };
  struct xkb_context *context = NULL;
  struct xkb_keymap *keymap = NULL;
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device (device);

  g_assert (wlr_keyboard);

  rules.layout = layout;
  rules.variant = variant;
  rules.options = options;

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  if (context == NULL) {
    g_warning ("Cannot create XKB context");
    goto out;
  }

  keymap = xkb_map_new_from_names (context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (keymap == NULL)
    g_warning ("Cannot create XKB keymap");

 out:
  if (context)
    xkb_context_unref (context);

  if (keymap) {
    xkb_keymap_unref (self->keymap);
    self->keymap = keymap;
  } else if (self->keymap == NULL) {
    set_fallback_keymap (self);
    return;
  }

  wlr_keyboard_set_keymap (wlr_keyboard, self->keymap);
}


static void
on_input_setting_changed (PhocKeyboard *self,
                          const gchar  *key,
                          GSettings    *settings)
{
  g_auto (GStrv) xkb_options = NULL;
  g_autoptr (GVariant) sources = NULL;
  GVariantIter iter;
  g_autofree gchar *id = NULL;
  g_autofree gchar *type = NULL;
  g_autofree gchar *xkb_options_string = NULL;
  const gchar *layout = NULL;
  const gchar *variant = NULL;
  PhocInputDevice *input_device;
  struct wlr_input_device *device;

  g_return_if_fail (PHOC_IS_KEYBOARD (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  input_device = PHOC_INPUT_DEVICE (self);
  device = phoc_input_device_get_device (input_device);

  g_debug ("Setting changed, reloading input settings");

  if (wlr_input_device_get_virtual_keyboard (device) != NULL) {
    g_debug ("Virtual keyboard in use, not switching layout");
    return;
  }

  sources = g_settings_get_value (settings, "sources");

  g_variant_iter_init (&iter, sources);
  g_variant_iter_next (&iter, "(ss)", &type, &id);

  if (g_strcmp0 (type, "xkb")) {
    g_debug ("Not a xkb layout: '%s' - ignoring", id);
    return;
  }

  xkb_options = g_settings_get_strv (settings, "xkb-options");
  if (xkb_options) {
    xkb_options_string = g_strjoinv (",", xkb_options);
    g_debug ("Setting options %s", xkb_options_string);
  }

  if (!gnome_xkb_info_get_layout_info (self->xkbinfo, id,
                                       NULL, NULL, &layout, &variant)) {
    g_debug ("Failed to get layout info for %s", id);
    return;
  }
  g_debug ("Switching to layout %s %s", layout, variant);

  set_xkb_keymap (self, layout, variant, xkb_options_string);
}


static void
on_keyboard_setting_changed (PhocKeyboard *self,
                             const gchar  *key,
                             GSettings    *settings)
{
  gboolean repeat;
  gint rate = 0, delay = 0;
  PhocInputDevice *input_device;
  struct wlr_input_device *device;
  struct wlr_keyboard *wlr_keyboard;

  g_return_if_fail (PHOC_IS_KEYBOARD (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  input_device = PHOC_INPUT_DEVICE (self);
  device = phoc_input_device_get_device (input_device);
  wlr_keyboard = wlr_keyboard_from_input_device (device);

  repeat = g_settings_get_boolean (self->keyboard_settings, "repeat");
  if (repeat) {
    guint interval = g_settings_get_uint (self->keyboard_settings, "repeat-interval");

    /* The setting is in the milliseconds between keys. "rate" is the number
     * of keys per second. */
    if (interval > 0)
      rate = (1000 / interval);
    else
      rate = 0;

    delay = g_settings_get_uint (self->keyboard_settings, "delay");
  }

  g_debug ("Setting repeat rate to %d, delay %d", rate, delay);
  wlr_keyboard_set_repeat_info (wlr_keyboard, rate, delay);
}


static void
handle_keyboard_key (struct wl_listener *listener, void *data)
{
  PhocKeyboard *self = wl_container_of (listener, self, keyboard_key);
  struct wlr_keyboard_key_event *event = data;

  g_assert (PHOC_IS_KEYBOARD (self));

  phoc_keyboard_handle_key (self, event);
  g_signal_emit (self, signals[ACTIVITY], 0, event->keycode);
}


static void
handle_keyboard_modifiers (struct wl_listener *listener, void *data)
{
  PhocKeyboard *self = wl_container_of (listener, self, keyboard_modifiers);

  g_assert (PHOC_IS_KEYBOARD (self));

  phoc_keyboard_handle_modifiers (self);
  g_signal_emit (self, signals[ACTIVITY], 0);
}


static void
phoc_keyboard_dispose (GObject *object)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);

  g_clear_object (&self->input_settings);
  g_clear_object (&self->keyboard_settings);
  g_clear_object (&self->xkbinfo);

  G_OBJECT_CLASS (phoc_keyboard_parent_class)->dispose (object);
}


static void
phoc_keyboard_finalize (GObject *object)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);

  wl_list_remove (&self->keyboard_key.link);
  wl_list_remove (&self->keyboard_modifiers.link);

  xkb_keymap_unref (self->keymap);
  self->keymap = NULL;

  g_hash_table_destroy (self->wakeup_keys);

  G_OBJECT_CLASS (phoc_keyboard_parent_class)->finalize (object);
}


/**
 * parse_udev_wakeup_keys:
 * @keyboard: PhocKeyboard to update with wakeup keycode config
 * @input_device: device to check for wakeup keys udev properties
 *
 * If the provided input_device has an underlying udev device, the properties are checked for any
 * that are prefixed with WAKEUP_KEY_UDEV_PREFIX.
 *
 * All properties with this prefix must have a value of either "0" (ignored) or "1" (used).
 *
 * For each property with a valid (positive non-zero int) keycode suffix, the appropriate
 * PhocWakeupKey (based on value) is inserted into @keyboard->wakeup_keys for that keycode.
 *
 * If the suffix is "DEFAULT", @keyboard->wakeup_key_default is set.
 */
static void
parse_udev_wakeup_keys (PhocKeyboard *keyboard, PhocInputDevice *input_device)
{
  struct wlr_input_device *device;
  struct libinput_device *dev_handle;
  struct udev_device *udev_dev;
  struct udev_list_entry *props, *prop_list_entry;
  const char *prop_name, *prop_value, *wakeup_prop_name;
  PhocWakeupKey key_state;
  gint64 val;
  char *endptr;

  device = phoc_input_device_get_device (input_device);

  if (!wlr_input_device_is_libinput (device))
    return;

  dev_handle = phoc_input_device_get_libinput_device_handle (input_device);
  udev_dev = libinput_device_get_udev_device (dev_handle);

  if (!udev_dev)
    return;

  props = udev_device_get_properties_list_entry (udev_dev);

  udev_list_entry_foreach (prop_list_entry, props) {
    prop_name = udev_list_entry_get_name (prop_list_entry);

    if (!g_str_has_prefix (prop_name, WAKEUP_KEY_UDEV_PREFIX))
      continue;

    wakeup_prop_name = prop_name + strlen (WAKEUP_KEY_UDEV_PREFIX);
    if (!strlen (wakeup_prop_name))
      continue;

    prop_value = udev_list_entry_get_value (prop_list_entry);
    key_state = PHOC_WAKEUP_KEY_IGNORE;
    if (g_str_equal (prop_value, "1"))
      key_state = PHOC_WAKEUP_KEY_USE;
    else if (!g_str_equal (prop_value, "0")) {
      g_warning ("udev property '%s' has invalid value '%s': should be '0' or '1'",
                 prop_name, prop_value);
      continue;
    }

    if (g_str_equal (wakeup_prop_name, "DEFAULT")) {
      keyboard->wakeup_key_default = key_state == PHOC_WAKEUP_KEY_USE;
      continue;
    }

    val = g_ascii_strtoll (wakeup_prop_name, &endptr, 10);
    if (wakeup_prop_name == endptr || val >= G_MAXUINT32 || val <= 0) {
      g_warning ("udev property '%s' has invalid keycode '%s': should be a positive non-zero int",
                 prop_name, wakeup_prop_name);
      continue;
    }

    g_hash_table_insert (keyboard->wakeup_keys, GUINT_TO_POINTER (val),
                         GUINT_TO_POINTER (key_state));
  }

  udev_device_unref (udev_dev);
}

static void
phoc_keyboard_constructed (GObject *object)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device (device);

  G_OBJECT_CLASS (phoc_keyboard_parent_class)->constructed (object);

  /* By default, all inputs from this keyboard will trigger activity events, unless there's a udev
   * property that explicitly prevents that. */
  self->wakeup_key_default = TRUE;
  self->wakeup_keys = g_hash_table_new (g_direct_hash, g_direct_equal);
  parse_udev_wakeup_keys (self, input_device);

  wlr_keyboard->data = self;

  /* wlr listeners */
  self->keyboard_key.notify = handle_keyboard_key;
  wl_signal_add (&wlr_keyboard->events.key, &self->keyboard_key);

  self->keyboard_modifiers.notify = handle_keyboard_modifiers;
  wl_signal_add (&wlr_keyboard->events.modifiers, &self->keyboard_modifiers);

  /* Keyboard settings */
  self->input_settings = g_settings_new ("org.gnome.desktop.input-sources");
  self->keyboard_settings = g_settings_new ("org.gnome.desktop.peripherals.keyboard");
  self->meta_key = WLR_MODIFIER_LOGO;

  set_fallback_keymap (self);
  self->xkbinfo = gnome_xkb_info_new ();

  g_object_connect (self->input_settings,
    "swapped-signal::changed::sources", G_CALLBACK (on_input_setting_changed), self,
    "swapped-signal::changed::xkb-options", G_CALLBACK (on_input_setting_changed), self,
    NULL);
  on_input_setting_changed (self, NULL, self->input_settings);

  g_object_connect (self->keyboard_settings,
    "swapped-signal::changed::repeat", G_CALLBACK (on_keyboard_setting_changed), self,
    "swapped-signal::changed::repeat-interval", G_CALLBACK (on_keyboard_setting_changed), self,
    "swapped-signal::changed::delay", G_CALLBACK (on_keyboard_setting_changed), self,
    NULL);
  on_keyboard_setting_changed (self, NULL, self->keyboard_settings);
}


static void
phoc_keyboard_class_init (PhocKeyboardClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->constructed = phoc_keyboard_constructed;
  object_class->dispose = phoc_keyboard_dispose;
  object_class->finalize = phoc_keyboard_finalize;

  /**
   * PhocKeyboard::activity
   * @keyboard: The keyboard that originated the signal.
   * @keycode: Raw scancode of the keypress that triggered the signal.
   *
   * Emitted whenever there is input activity on this device
   */
  signals[ACTIVITY] = g_signal_new ("activity",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 1, G_TYPE_UINT);
}


static void
phoc_keyboard_init (PhocKeyboard *self)
{
}


PhocKeyboard *
phoc_keyboard_new (struct wlr_input_device *device, PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_KEYBOARD,
                       "device", device,
                       "seat", seat,
                       NULL);
}

/**
 * phoc_keyboard_next_layout:
 * @self: the keyboard
 *
 * Switch to next keyboard in the list of available layouts
 */
void
phoc_keyboard_next_layout (PhocKeyboard *self)
{
  g_autoptr (GVariant) sources = NULL;
  GVariantIter iter;
  gchar *type, *id, *cur_type, *cur_id;
  GVariantBuilder builder;
  gboolean next;

  g_return_if_fail (PHOC_IS_KEYBOARD (self));
  sources = g_settings_get_value (self->input_settings, "sources");

  if (g_variant_n_children (sources) < 2)
    return;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));
  g_variant_iter_init (&iter, sources);
  next = g_variant_iter_next (&iter, "(ss)", &cur_type, &cur_id);
  while (next) {
    next = g_variant_iter_next (&iter, "(ss)", &type, &id);
    if (!next)
      break;
    g_variant_builder_add (&builder, "(ss)", type, id);
  }
  g_variant_builder_add (&builder, "(ss)", cur_type, cur_id);

  g_settings_set_value (self->input_settings, "sources", g_variant_builder_end (&builder));
}

/**
 * phoc_keyboard_grab_meta_press:
 * @self: the keyboard
 *
 * If the meta key is currently the only pressed modifier grab it
 * (hence canceling the meta key press/release sequence) making it
 * available to other gestures (like left-click + meta).
 *
 * Returns: %TRUE if the meta key is pressed. Otherwise %FALSE.
 */
gboolean
phoc_keyboard_grab_meta_press (PhocKeyboard *self)
{
  uint32_t modifiers;
  struct wlr_input_device *device;

  g_assert (PHOC_IS_KEYBOARD (self));

  device = phoc_input_device_get_device (PHOC_INPUT_DEVICE (self));
  modifiers = wlr_keyboard_get_modifiers (wlr_keyboard_from_input_device (device));
  if ((modifiers ^ self->meta_key) == 0) {
    self->meta_press_valid = false;
    return TRUE;
  }

  return FALSE;
}


/**
 * phoc_keyboard_is_wakeup_key:
 * @keycode: scancode of key to check
 *
 * Returns: Whether or not the specified key should trigger activity events.
 */
gboolean
phoc_keyboard_is_wakeup_key (PhocKeyboard *self, uint32_t keycode)
{
  g_assert (PHOC_IS_KEYBOARD (self));

  switch (GPOINTER_TO_UINT (g_hash_table_lookup (self->wakeup_keys, GUINT_TO_POINTER (keycode)))) {
  case PHOC_WAKEUP_KEY_USE:
    return true;
  case PHOC_WAKEUP_KEY_IGNORE:
    return false;
  default:
    return self->wakeup_key_default;
  }
}
