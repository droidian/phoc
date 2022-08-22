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

/**
 * PhocKeyboard:
 *
 * A keyboard input device
 *
 * It tracks keybindings and it's keymap.
 */
struct _PhocKeyboard {
  PhocInputDevice parent;

  struct wl_listener keyboard_key;
  struct wl_listener keyboard_modifiers;

  GSettings *input_settings;
  GSettings *keyboard_settings;
  struct xkb_keymap *keymap;
  uint32_t meta_key;
  GnomeXkbInfo *xkbinfo;

  xkb_keysym_t pressed_keysyms_translated[PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP];
  xkb_keysym_t pressed_keysyms_raw[PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP];
};
G_DEFINE_TYPE(PhocKeyboard, phoc_keyboard, PHOC_TYPE_INPUT_DEVICE)


enum {
  ACTIVITY,
  N_SIGNALS
};
static guint signals [N_SIGNALS];


static ssize_t
pressed_keysyms_index(const xkb_keysym_t *pressed_keysyms,
                      xkb_keysym_t keysym)
{
  for (size_t i = 0; i < PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
    if (pressed_keysyms[i] == keysym) {
      return i;
    }
  }
  return -1;
}

static size_t pressed_keysyms_length(const xkb_keysym_t *pressed_keysyms) {
  size_t n = 0;
  for (size_t i = 0; i < PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
    if (pressed_keysyms[i] != XKB_KEY_NoSymbol) {
      ++n;
    }
  }
  return n;
}

static void
pressed_keysyms_add(xkb_keysym_t *pressed_keysyms,
                    xkb_keysym_t keysym)
{
  ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
  if (i < 0) {
    i = pressed_keysyms_index(pressed_keysyms, XKB_KEY_NoSymbol);
    if (i >= 0) {
      pressed_keysyms[i] = keysym;
    }
  }
}

static void
pressed_keysyms_remove(xkb_keysym_t *pressed_keysyms,
                       xkb_keysym_t keysym)
{
  ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
  if (i >= 0) {
    pressed_keysyms[i] = XKB_KEY_NoSymbol;
  }
}

static bool
keysym_is_modifier(xkb_keysym_t keysym)
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
pressed_keysyms_update(xkb_keysym_t *pressed_keysyms,
                       const xkb_keysym_t *keysyms, size_t keysyms_len,
                       enum wl_keyboard_key_state state)
{
  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keysym_is_modifier(keysyms[i])) {
      continue;
    }
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      pressed_keysyms_add(pressed_keysyms, keysyms[i]);
    } else { // WL_KEYBOARD_KEY_STATE_RELEASED
      pressed_keysyms_remove(pressed_keysyms, keysyms[i]);
    }
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
keyboard_execute_compositor_binding(PhocKeyboard *self,
                                    xkb_keysym_t keysym)
{
  PhocServer *server = phoc_server_get_default ();

  if (keysym >= XKB_KEY_XF86Switch_VT_1 &&
      keysym <= XKB_KEY_XF86Switch_VT_12) {

    struct wlr_session *session = wlr_backend_get_session(server->backend);
    if (session) {
      unsigned vt = keysym - XKB_KEY_XF86Switch_VT_1 + 1;
      wlr_session_change_vt(session, vt);
    }

    return true;
  }

  if (keysym == XKB_KEY_Escape) {
    PhocSeat *seat = phoc_input_device_get_seat (PHOC_INPUT_DEVICE (self));

    wlr_seat_pointer_end_grab(seat->seat);
    wlr_seat_keyboard_end_grab(seat->seat);
    wlr_seat_touch_end_grab (seat->seat);
    phoc_seat_end_compositor_grab(seat);
  }

  return false;
}

/*
 * Execute super key. This function will detect if the super key has been pressed
 * or released. If between a press and release no other keys are press then a
 * keysym will be forwarded to phoc. Otherwise nothing happens.
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
  static bool press_valid = false;
  uint32_t super_mod = 1 << xkb_map_mod_get_index(self->keymap, "Mod4");

  if (modifiers > 0 && modifiers != super_mod) {
    /* Check if other modifiers have been pressed e.g. <ctrl><super> */
    return false;
  }

  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keysyms[i] == XKB_KEY_Super_L || keysyms[i] == XKB_KEY_Super_R) {
      PhocKeyCombo combo = { 0, keysyms[i] };

      if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* The super key was pressed wait for a release */
        press_valid = true;
        return phoc_phosh_private_forward_keysym (&combo, time, false);
      } else if (press_valid == true) {
        /* The super key was released and is valid (no other key was pressed) */
        press_valid = false;
        return phoc_phosh_private_forward_keysym (&combo, time, true);
      }

      return false;
    } else {
      /* A non super key was pressed, don't wait for the release
       * as the super key is a modifier.
       */
      press_valid = false;
    }
  }

  return false;
}

static bool
keyboard_execute_power_key (PhocKeyboard              *self,
                            const xkb_keysym_t        *keysyms,
                            size_t                     keysyms_len,
                            enum wl_keyboard_key_state state)
{
  PhocServer *server = phoc_server_get_default ();

  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
      return false;

  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keysyms[i] == XKB_KEY_XF86PowerDown || keysyms[i] == XKB_KEY_XF86PowerOff) {
      g_debug ("Power button pressed");
      phoc_desktop_toggle_output_blank (server->desktop);
      return true;
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
  PhocServer *server = phoc_server_get_default ();
  PhocKeybindings *keybindings;
  PhocSeat *seat = phoc_input_device_get_seat (PHOC_INPUT_DEVICE (self));

  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
      return false;

  /* TODO: should be handled via PhocKeybindings as well */
  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keyboard_execute_compositor_binding(self, keysyms[i])) {
      return true;
    }
  }

  size_t n = pressed_keysyms_length(pressed_keysyms);
  keybindings = server->config->keybindings;

  if (phoc_keybindings_handle_pressed (keybindings, modifiers, pressed_keysyms, n, seat))
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
keyboard_keysyms_translated(PhocKeyboard *self,
                            xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
                            uint32_t *modifiers)
{
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);

  *modifiers = wlr_keyboard_get_modifiers(device->keyboard);
  xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods2(
    device->keyboard->xkb_state, keycode, XKB_CONSUMED_MODE_XKB);
  *modifiers = *modifiers & ~consumed;

  return xkb_state_key_get_syms(device->keyboard->xkb_state,
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
keyboard_keysyms_raw(PhocKeyboard *self,
                     xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
                     uint32_t *modifiers)
{
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);

  *modifiers = wlr_keyboard_get_modifiers(device->keyboard);

  xkb_layout_index_t layout_index = xkb_state_key_get_layout(device->keyboard->xkb_state,
                                                             keycode);
  return xkb_keymap_key_get_syms_by_level(device->keyboard->keymap,
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

  // Do not forward virtual events back to the client that generated them
  if (virtual_keyboard
      && wl_resource_get_client (input_method->keyboard_grab->resource)
      == wl_resource_get_client (virtual_keyboard->resource)) {
    return NULL;
  }

  return input_method->keyboard_grab;
}

static void
phoc_keyboard_handle_key (PhocKeyboard *self, struct wlr_event_keyboard_key *event)
{
  xkb_keycode_t keycode = event->keycode + 8;
  bool handled = false;
  uint32_t modifiers;
  const xkb_keysym_t *keysyms;
  size_t keysyms_len;

  // Handle translated keysyms
  keysyms_len = keyboard_keysyms_translated (self, keycode, &keysyms, &modifiers);
  pressed_keysyms_update (self->pressed_keysyms_translated, keysyms, keysyms_len, event->state);
  handled = keyboard_execute_binding(self,
                                     self->pressed_keysyms_translated, modifiers, keysyms,
                                     keysyms_len, event->state);

  keysyms_len = keyboard_keysyms_raw (self, keycode, &keysyms, &modifiers);
  pressed_keysyms_update (self->pressed_keysyms_raw, keysyms, keysyms_len,
                          event->state);
  // Handle raw keysyms
  if (!handled) {
    handled = keyboard_execute_binding (self,
                                        self->pressed_keysyms_raw, modifiers, keysyms,
                                        keysyms_len, event->state);
  }

  // Check for the super key
  if (!handled) {
    handled = keyboard_execute_super_key (self, keysyms, keysyms_len, modifiers, event->time_msec, event->state);
  }

  // Handle subscribed keysyms
  if (!handled) {
    handled = keyboard_execute_subscribed_binding (self,
                                                   self->pressed_keysyms_raw, modifiers,
                                                   keysyms, keysyms_len, event->time_msec,
                                                   event->state);
  }

  // Check for the power button after the susbscribed bindings so clients can override it
  if (!handled) {
    handled = keyboard_execute_power_key (self, keysyms, keysyms_len, event->state);
  }

  if (!handled) {
    PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
    struct wlr_input_device *device = phoc_input_device_get_device (input_device);
    struct wlr_input_method_keyboard_grab_v2 *grab = phoc_keyboard_get_grab (self);

    if (grab) {
      wlr_input_method_keyboard_grab_v2_set_keyboard (grab, device->keyboard);
      wlr_input_method_keyboard_grab_v2_send_key (grab,
                                                  event->time_msec,
                                                  event->keycode,
                                                  event->state);
    } else {
      PhocSeat *seat = phoc_input_device_get_seat (input_device);
      wlr_seat_set_keyboard (seat->seat, device);
      wlr_seat_keyboard_notify_key (seat->seat,
                                    event->time_msec,
                                    event->keycode,
                                    event->state);
    }
  }
}

static void
phoc_keyboard_handle_modifiers(PhocKeyboard *self)
{
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);
  struct wlr_input_method_keyboard_grab_v2 *grab = phoc_keyboard_get_grab (self);

  if (grab) {
    wlr_input_method_keyboard_grab_v2_set_keyboard (grab, device->keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers (grab, &device->keyboard->modifiers);
  } else {
    PhocSeat *seat = phoc_input_device_get_seat (input_device);
    wlr_seat_set_keyboard (seat->seat, device);
    wlr_seat_keyboard_notify_modifiers (seat->seat, &device->keyboard->modifiers);
  }
}


static void
set_fallback_keymap (PhocKeyboard *self)
{
  struct xkb_context *context;
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  if (context == NULL) {
       return;
  }

  xkb_keymap_unref (self->keymap);
  self->keymap = xkb_keymap_new_from_names (context, NULL,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  wlr_keyboard_set_keymap(device->keyboard, self->keymap);
}


static void
set_xkb_keymap (PhocKeyboard *self, const gchar *layout, const gchar *variant, const gchar *options)
{
  struct xkb_rule_names rules = { 0 };
  struct xkb_context *context = NULL;
  struct xkb_keymap *keymap = NULL;
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);

  g_assert (device->keyboard);

  rules.layout = layout;
  rules.variant = variant;
  rules.options = options;

  context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (context == NULL) {
    g_warning ("Cannot create XKB context");
    goto out;
  }

  keymap = xkb_map_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (keymap == NULL) {
    g_warning ("Cannot create XKB keymap");
  }

 out:
  if (context)
    xkb_context_unref(context);

  if (keymap) {
    xkb_keymap_unref (self->keymap);
    self->keymap = keymap;
  } else if (self->keymap == NULL) {
    set_fallback_keymap (self);
    return;
  }

  wlr_keyboard_set_keymap(device->keyboard, self->keymap);
}


static void
on_input_setting_changed (PhocKeyboard *self,
                          const gchar  *key,
                          GSettings    *settings)
{
  g_auto(GStrv) xkb_options = NULL;
  g_autoptr(GVariant) sources = NULL;
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

  if (wlr_input_device_get_virtual_keyboard(device) != NULL) {
          g_debug ("Virtual keyboard in use, not switching layout");
          return;
  }

  sources = g_settings_get_value(settings, "sources");

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

  g_return_if_fail (PHOC_IS_KEYBOARD (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  input_device = PHOC_INPUT_DEVICE (self);
  device = phoc_input_device_get_device (input_device);

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
  wlr_keyboard_set_repeat_info(device->keyboard, rate, delay);
}


static void
handle_keyboard_key (struct wl_listener *listener, void *data)
{
  PhocKeyboard *self = wl_container_of (listener, self, keyboard_key);
  struct wlr_event_keyboard_key *event = data;

  g_assert (PHOC_IS_KEYBOARD (self));

  phoc_keyboard_handle_key (self, event);
  g_signal_emit (self, signals[ACTIVITY], 0);
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
phoc_keyboard_dispose(GObject *object)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);

  g_clear_object (&self->input_settings);
  g_clear_object (&self->keyboard_settings);
  g_clear_object (&self->xkbinfo);

  G_OBJECT_CLASS (phoc_keyboard_parent_class)->dispose (object);
}


static void
phoc_keyboard_finalize(GObject *object)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);

  wl_list_remove (&self->keyboard_key.link);
  wl_list_remove (&self->keyboard_modifiers.link);

  xkb_keymap_unref (self->keymap);
  self->keymap = NULL;

  G_OBJECT_CLASS (phoc_keyboard_parent_class)->finalize (object);
}


static void
phoc_keyboard_constructed (GObject *object)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);
  PhocInputDevice *input_device = PHOC_INPUT_DEVICE (self);
  struct wlr_input_device *device = phoc_input_device_get_device (input_device);

  device->keyboard->data = self;

  /* wlr listeners */
  self->keyboard_key.notify = handle_keyboard_key;
  wl_signal_add (&device->keyboard->events.key,
                 &self->keyboard_key);
  self->keyboard_modifiers.notify = handle_keyboard_modifiers;
  wl_signal_add (&device->keyboard->events.modifiers,
                 &self->keyboard_modifiers);

  /* Keyboard settings */
  self->input_settings = g_settings_new ("org.gnome.desktop.input-sources");
  self->keyboard_settings = g_settings_new (
    "org.gnome.desktop.peripherals.keyboard");
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

  G_OBJECT_CLASS (phoc_keyboard_parent_class)->constructed (object);
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
   *
   * Emitted whenever there is input activity on this device
   */
  signals[ACTIVITY] = g_signal_new ("activity",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);
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
  g_autoptr(GVariant) sources = NULL;
  GVariantIter iter;
  gchar *type, *id, *cur_type, *cur_id;
  GVariantBuilder builder;
  gboolean next;

  g_return_if_fail (PHOC_IS_KEYBOARD (self));
  sources = g_settings_get_value(self->input_settings, "sources");

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

  g_settings_set_value(self->input_settings, "sources", g_variant_builder_end(&builder));
}


/**
 * phoc_keyboard_get_meta_key:
 * @self: the keyboard
 *
 * Returns: the current Meta key
 */
uint32_t
phoc_keyboard_get_meta_key (PhocKeyboard *self)
{
  g_assert (PHOC_IS_KEYBOARD (self));

  return self->meta_key;
}
