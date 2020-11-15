#define G_LOG_DOMAIN "phoc-keyboard"

#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "keyboard.h"
#include "phosh.h"
#include "seat.h"

#include <glib.h>
#include <glib/gprintf.h>

#define KEYBOARD_DEFAULT_XKB_RULES "evdev";
#define KEYBOARD_DEFAULT_XKB_MODEL "pc105";

enum {
  PROP_0,
  PROP_DEVICE,
  PROP_SEAT,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE(PhocKeyboard, phoc_keyboard, G_TYPE_OBJECT);

static void
phoc_keyboard_set_property (GObject     *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);

  switch (property_id) {
  case PROP_DEVICE:
    self->device = g_value_get_pointer (value);
    self->device->data = self;
    self->device->keyboard->data = self;
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
phoc_keyboard_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);

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

static ssize_t
pressed_keysyms_index(xkb_keysym_t *pressed_keysyms,
                      xkb_keysym_t keysym)
{
  for (size_t i = 0; i < PHOC_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
    if (pressed_keysyms[i] == keysym) {
      return i;
    }
  }
  return -1;
}

static size_t pressed_keysyms_length(xkb_keysym_t *pressed_keysyms) {
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
                       enum wlr_key_state state)
{
  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keysym_is_modifier(keysyms[i])) {
      continue;
    }
    if (state == WLR_KEY_PRESSED) {
      pressed_keysyms_add(pressed_keysyms, keysyms[i]);
    } else { // WLR_KEY_RELEASED
      pressed_keysyms_remove(pressed_keysyms, keysyms[i]);
    }
  }
}

/**
 * Execute a built-in, hardcoded compositor binding. These are triggered from a
 * single keysym.
 *
 * Returns true if the keysym was handled by a binding and false if the event
 * should be propagated to clients.
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

  if (keysym == XKB_KEY_XF86PowerDown || keysym == XKB_KEY_XF86PowerOff) {
    g_debug ("Power button pressed");
    phoc_desktop_toggle_output_blank (server->desktop);
    return true;
  }

  if (keysym == XKB_KEY_Escape) {
    wlr_seat_pointer_end_grab(self->seat->seat);
    wlr_seat_keyboard_end_grab(self->seat->seat);
    roots_seat_end_compositor_grab(self->seat);
  }

  return false;
}

/**
 * Execute keyboard bindings. These include compositor bindings and user-defined
 * bindings.
 *
 * Returns true if the keysym was handled by a binding and false if the event
 * should be propagated to clients.
 */
static bool
keyboard_execute_binding(PhocKeyboard *self,
                         xkb_keysym_t *pressed_keysyms, uint32_t modifiers,
                         const xkb_keysym_t *keysyms, size_t keysyms_len)
{
  PhocServer *server = phoc_server_get_default ();
  PhocKeybindings *keybindings;

  /* TODO: should be handled via PhocKeybindings as well */
  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keyboard_execute_compositor_binding(self, keysyms[i])) {
      return true;
    }
  }

  size_t n = pressed_keysyms_length(pressed_keysyms);
  keybindings = server->config->keybindings;

  if (phoc_keybindings_handle_pressed (keybindings, modifiers, pressed_keysyms, n,
                                       self->seat))
    return true;

  return false;
}


/**
 * Forward keyboard bindings.
 *
 * Returns true if the keysym was handled by forwarding and false if the event
 * should be propagated to clients.
 */
static bool
keyboard_execute_subscribed_binding(PhocKeyboard *self,
                                    xkb_keysym_t *pressed_keysyms, uint32_t modifiers,
                                    const xkb_keysym_t *keysyms, size_t keysyms_len,
                                    uint32_t time)
{
  bool handled = false;
  for (size_t i = 0; i < keysyms_len; ++i) {
    PhocKeyCombo combo = { modifiers, keysyms[i] };
    handled = handled |
      phosh_forward_keysym (&combo,
                            time);
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
  *modifiers = wlr_keyboard_get_modifiers(self->device->keyboard);
  xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods2(
    self->device->keyboard->xkb_state, keycode, XKB_CONSUMED_MODE_XKB);
  *modifiers = *modifiers & ~consumed;

  return xkb_state_key_get_syms(self->device->keyboard->xkb_state,
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
  *modifiers = wlr_keyboard_get_modifiers(self->device->keyboard);

  xkb_layout_index_t layout_index = xkb_state_key_get_layout(self->device->keyboard->xkb_state,
                                                             keycode);
  return xkb_keymap_key_get_syms_by_level(self->device->keyboard->keymap,
                                          keycode, layout_index, 0, keysyms);
}

void
phoc_keyboard_handle_key(PhocKeyboard *self,
                         struct wlr_event_keyboard_key *event) {
  xkb_keycode_t keycode = event->keycode + 8;

  bool handled = false;
  uint32_t modifiers;
  const xkb_keysym_t *keysyms;
  size_t keysyms_len;

  // Handle translated keysyms

  keysyms_len = keyboard_keysyms_translated(self, keycode, &keysyms,
                                            &modifiers);
  pressed_keysyms_update(self->pressed_keysyms_translated, keysyms,
                         keysyms_len, event->state);
  if (event->state == WLR_KEY_PRESSED) {
    handled = keyboard_execute_binding(self,
                                       self->pressed_keysyms_translated, modifiers, keysyms,
                                       keysyms_len);
  }

  // Handle raw keysyms
  keysyms_len = keyboard_keysyms_raw(self, keycode, &keysyms, &modifiers);
  pressed_keysyms_update(self->pressed_keysyms_raw, keysyms, keysyms_len,
                         event->state);
  if (event->state == WLR_KEY_PRESSED && !handled) {
    handled = keyboard_execute_binding(self,
                                       self->pressed_keysyms_raw, modifiers, keysyms, keysyms_len);
  }

  // Handle subscribed keysyms
  if (event->state == WLR_KEY_PRESSED && !handled) {
    handled = keyboard_execute_subscribed_binding (self,
                                                   self->pressed_keysyms_raw, modifiers,
                                                   keysyms, keysyms_len, event->time_msec);
  }

  if (!handled) {
    wlr_seat_set_keyboard(self->seat->seat, self->device);
    wlr_seat_keyboard_notify_key(self->seat->seat, event->time_msec,
                                 event->keycode, event->state);
  }
}

void
phoc_keyboard_handle_modifiers(PhocKeyboard *self)
{
  struct wlr_seat *seat = self->seat->seat;
  wlr_seat_set_keyboard(seat, self->device);
  wlr_seat_keyboard_notify_modifiers(seat, &self->device->keyboard->modifiers);
}


static void
set_fallback_keymap (PhocKeyboard *self)
{
  struct xkb_rule_names rules = { 0 };
  struct xkb_context *context;

  rules.rules = KEYBOARD_DEFAULT_XKB_RULES;
  rules.model = KEYBOARD_DEFAULT_XKB_MODEL;
  rules.layout = "us";
  rules.variant = "";
  rules.options = "";

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  if (context == NULL) {
       return;
  }

  xkb_keymap_unref (self->keymap);
  self->keymap = xkb_keymap_new_from_names (context, &rules,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  g_return_if_fail (self->device);
  wlr_keyboard_set_keymap(self->device->keyboard, self->keymap);
}


static void
set_xkb_keymap (PhocKeyboard *self, const gchar *layout, const gchar *variant, const gchar *options)
{
  struct xkb_rule_names rules = { 0 };
  struct xkb_context *context = NULL;
  struct xkb_keymap *keymap = NULL;

  g_return_if_fail (self->device);
  g_return_if_fail (self->device->keyboard);

  rules.rules = KEYBOARD_DEFAULT_XKB_RULES;
  rules.model = KEYBOARD_DEFAULT_XKB_MODEL;
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

  wlr_keyboard_set_keymap(self->device->keyboard, self->keymap);
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

  g_return_if_fail (PHOC_IS_KEYBOARD (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  g_debug ("Setting changed, reloading input settings");

  if (wlr_input_device_get_virtual_keyboard(self->device) != NULL) {
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

  g_return_if_fail (PHOC_IS_KEYBOARD (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

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
  wlr_keyboard_set_repeat_info(self->device->keyboard, rate, delay);
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

  xkb_keymap_unref (self->keymap);
  self->keymap = NULL;

  wl_list_remove(&self->link);

  G_OBJECT_CLASS (phoc_keyboard_parent_class)->finalize (object);
}


static void
phoc_keyboard_constructed (GObject *object)
{
  PhocKeyboard *self = PHOC_KEYBOARD (object);

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

  object_class->set_property = phoc_keyboard_set_property;
  object_class->get_property = phoc_keyboard_get_property;

  object_class->constructed = phoc_keyboard_constructed;
  object_class->dispose = phoc_keyboard_dispose;
  object_class->finalize = phoc_keyboard_finalize;

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

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

static void
phoc_keyboard_init (PhocKeyboard *self)
{
}

PhocKeyboard *
phoc_keyboard_new (struct wlr_input_device *device, struct roots_seat *seat)
{
  return g_object_new (PHOC_TYPE_KEYBOARD,
                       "device", device,
                       "seat", seat,
                       NULL);
}

/**
 * phoc_keyboard_next_layout:
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
