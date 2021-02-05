#define G_LOG_DOMAIN "phoc-keybindings"

/**
 * SECTION:phoc-keybindings
 * @short_description: keybindings
 * @Title: PhocKeybindings
 * @Summary: Handled keybindings stored in gsettings
 */
#include "config.h"
#include "keybindings.h"
#include "seat.h"

#include <wlr/types/wlr_keyboard.h>

#include <gio/gio.h>

#define KEYBINDINGS_SCHEMA_ID "org.gnome.desktop.wm.keybindings"
#define MUTTER_KEYBINDINGS_SCHEMA_ID "org.gnome.mutter.keybindings"

typedef void (*PhocKeyHandlerFunc) (struct roots_seat *);



typedef struct
{
  gchar *name;
  PhocKeyHandlerFunc func;

  GSList *combos;
} PhocKeybinding;


typedef struct _PhocKeybindings
{
  GObject parent;

  GSList *bindings;
  GSettings *settings;
  GSettings *mutter_settings;
} PhocKeybindings;

G_DEFINE_TYPE (PhocKeybindings, phoc_keybindings, G_TYPE_OBJECT);

static void
handle_maximize (struct roots_seat *seat)
{
  struct roots_view *focus = roots_seat_get_focus(seat);

  if (focus != NULL)
    view_maximize(focus);
}

static void
handle_unmaximize (struct roots_seat *seat)
{
  struct roots_view *focus = roots_seat_get_focus(seat);

  if (focus != NULL)
    view_restore(focus);
}

static void
handle_tile_right (struct roots_seat *seat)
{
  struct roots_view *view = roots_seat_get_focus(seat);

  if (view != NULL)
    view_tile(view, PHOC_VIEW_TILE_RIGHT);
}


static void
handle_tile_left (struct roots_seat *seat)
{
  struct roots_view *view = roots_seat_get_focus(seat);

  if (view != NULL)
    view_tile(view, PHOC_VIEW_TILE_LEFT);
}


static void
handle_toggle_maximized (struct roots_seat *seat)
{
  struct roots_view *focus = roots_seat_get_focus(seat);

  if (focus != NULL) {
    if (view_is_maximized(focus))
      view_restore(focus);
    else
      view_maximize(focus);
  }
}

static void
handle_toggle_fullscreen (struct roots_seat *seat)
{
  struct roots_view *focus = roots_seat_get_focus(seat);

  if (focus) {
    bool is_fullscreen = focus->fullscreen_output != NULL;
    view_set_fullscreen(focus, !is_fullscreen, NULL);
  }
}


static void handle_cycle_windows (struct roots_seat *seat)
{
  roots_seat_cycle_focus(seat);
}


static void handle_close (struct roots_seat *seat)
{
  struct roots_view *focus = roots_seat_get_focus(seat);

  if (focus)
    view_close(focus);
}

static void
handle_move_to_monitor_right (struct roots_seat *seat)
{
  struct roots_view *view = roots_seat_get_focus(seat);

  if (view)
    view_move_to_next_output(view, WLR_DIRECTION_RIGHT);
}


static void
handle_move_to_monitor_left (struct roots_seat *seat)
{
  struct roots_view *view = roots_seat_get_focus(seat);

  if (view)
    view_move_to_next_output(view, WLR_DIRECTION_LEFT);
}


static void handle_switch_input_source (struct roots_seat *seat)
{
  struct wlr_keyboard *wlr_keyboard = wlr_seat_get_keyboard (seat->seat);
  PhocKeyboard *keyboard;

  if (wlr_keyboard == NULL)
    return;

  keyboard = wlr_keyboard->data;
  g_return_if_fail (PHOC_IS_KEYBOARD (keyboard));

  phoc_keyboard_next_layout (keyboard);
}


/* This is copied from mutter which in turn got it form GTK+ */
static inline gboolean
is_alt (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'a' || string[1] == 'A') &&
          (string[2] == 'l' || string[2] == 'L') &&
          (string[3] == 't' || string[3] == 'T') &&
          (string[4] == '>'));
}

static inline gboolean
is_ctl (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'c' || string[1] == 'C') &&
          (string[2] == 't' || string[2] == 'T') &&
          (string[3] == 'l' || string[3] == 'L') &&
          (string[4] == '>'));
}

static inline gboolean
is_modx (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'm' || string[1] == 'M') &&
          (string[2] == 'o' || string[2] == 'O') &&
          (string[3] == 'd' || string[3] == 'D') &&
          (string[4] >= '1' && string[4] <= '5') &&
          (string[5] == '>'));
}

static inline gboolean
is_ctrl (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'c' || string[1] == 'C') &&
          (string[2] == 't' || string[2] == 'T') &&
          (string[3] == 'r' || string[3] == 'R') &&
          (string[4] == 'l' || string[4] == 'L') &&
          (string[5] == '>'));
}

static inline gboolean
is_shft (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 's' || string[1] == 'S') &&
          (string[2] == 'h' || string[2] == 'H') &&
          (string[3] == 'f' || string[3] == 'F') &&
          (string[4] == 't' || string[4] == 'T') &&
          (string[5] == '>'));
}

static inline gboolean
is_shift (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 's' || string[1] == 'S') &&
          (string[2] == 'h' || string[2] == 'H') &&
          (string[3] == 'i' || string[3] == 'I') &&
          (string[4] == 'f' || string[4] == 'F') &&
          (string[5] == 't' || string[5] == 'T') &&
          (string[6] == '>'));
}

static inline gboolean
is_control (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'c' || string[1] == 'C') &&
          (string[2] == 'o' || string[2] == 'O') &&
          (string[3] == 'n' || string[3] == 'N') &&
          (string[4] == 't' || string[4] == 'T') &&
          (string[5] == 'r' || string[5] == 'R') &&
          (string[6] == 'o' || string[6] == 'O') &&
          (string[7] == 'l' || string[7] == 'L') &&
          (string[8] == '>'));
}

static inline gboolean
is_meta (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'm' || string[1] == 'M') &&
          (string[2] == 'e' || string[2] == 'E') &&
          (string[3] == 't' || string[3] == 'T') &&
          (string[4] == 'a' || string[4] == 'A') &&
          (string[5] == '>'));
}

static inline gboolean
is_super (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 's' || string[1] == 'S') &&
          (string[2] == 'u' || string[2] == 'U') &&
          (string[3] == 'p' || string[3] == 'P') &&
          (string[4] == 'e' || string[4] == 'E') &&
          (string[5] == 'r' || string[5] == 'R') &&
          (string[6] == '>'));
}

static inline gboolean
is_hyper (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'h' || string[1] == 'H') &&
          (string[2] == 'y' || string[2] == 'Y') &&
          (string[3] == 'p' || string[3] == 'P') &&
          (string[4] == 'e' || string[4] == 'E') &&
          (string[5] == 'r' || string[5] == 'R') &&
          (string[6] == '>'));
}

static inline gboolean
is_primary (const gchar *string)
{
  return ((string[0] == '<') &&
	  (string[1] == 'p' || string[1] == 'P') &&
	  (string[2] == 'r' || string[2] == 'R') &&
	  (string[3] == 'i' || string[3] == 'I') &&
	  (string[4] == 'm' || string[4] == 'M') &&
	  (string[5] == 'a' || string[5] == 'A') &&
	  (string[6] == 'r' || string[6] == 'R') &&
	  (string[7] == 'y' || string[7] == 'Y') &&
	  (string[8] == '>'));
}

static inline gboolean
is_keycode (const gchar *string)
{
  return (string[0] == '0' &&
          string[1] == 'x' &&
          g_ascii_isxdigit (string[2]) &&
          g_ascii_isxdigit (string[3]));
}

PhocKeyCombo *
parse_accelerator (const gchar *accelerator)
{
  PhocKeyCombo *combo;
  xkb_keysym_t keyval;
  guint32 mods;
  gint len;

  if (accelerator == NULL)
    return FALSE;

  keyval = 0;
  mods = 0;
  len = strlen (accelerator);
  while (len) {
    if (*accelerator == '<') {
      if (len >= 9 && is_control (accelerator)) {
	  accelerator += 9;
	  len -= 9;
	  mods |= WLR_MODIFIER_CTRL;
      } else if (len >= 7 && is_shift (accelerator)) {
	accelerator += 7;
	len -= 7;
	mods |= WLR_MODIFIER_SHIFT;
      } else if (len >= 6 && is_shft (accelerator)) {
	accelerator += 6;
	len -= 6;
	mods |= WLR_MODIFIER_SHIFT;
      } else if (len >= 6 && is_ctrl (accelerator)) {
	accelerator += 6;
	len -= 6;
	mods |= WLR_MODIFIER_CTRL;
      } else if (len >= 6 && is_modx (accelerator)) {
	  static const guint mod_vals[] = {
					   WLR_MODIFIER_ALT,
					   WLR_MODIFIER_MOD2,
					   WLR_MODIFIER_MOD3,
					   WLR_MODIFIER_LOGO,
					   WLR_MODIFIER_MOD5,
	  };

	  len -= 6;
	  accelerator += 4;
	  mods |= mod_vals[*accelerator - '1'];
	  accelerator += 2;
      }  else if (len >= 5 && is_ctl (accelerator)) {
	accelerator += 5;
	len -= 5;
	mods |= WLR_MODIFIER_CTRL;
      } else if (len >= 5 && is_alt (accelerator)) {
	accelerator += 5;
	len -= 5;
	mods |= WLR_MODIFIER_ALT;
      } else if (len >= 6 && is_meta (accelerator)) {
	accelerator += 6;
	len -= 6;
	g_warning ("Unhandled modifier meta");
	return FALSE;
      } else if (len >= 7 && is_hyper (accelerator)) {
	accelerator += 7;
	len -= 7;
	g_warning ("Unhandled modifier hyper");
	return FALSE;
      } else if (len >= 7 && is_super (accelerator)) {
	accelerator += 7;
	len -= 7;
	mods |= WLR_MODIFIER_LOGO;
      } else {
	gchar last_ch;

	last_ch = *accelerator;
	while (last_ch && last_ch != '>') {
	  last_ch = *accelerator;
	  accelerator += 1;
	  len -= 1;
	}
      }
    } else {
      if (len >= 4 && is_keycode (accelerator)) {
	//keycode = strtoul (accelerator, NULL, 16);
	g_warning ("Unhandled keycode accelerator'");
	goto out;
      }	else if (strcmp (accelerator, "Above_Tab") == 0) {
	g_warning ("Unhandled key 'Above_Tab'");
	return FALSE;
      } else {
	keyval = xkb_keysym_from_name (accelerator, XKB_KEYSYM_CASE_INSENSITIVE);
	if (keyval == XKB_KEY_NoSymbol) {
	  g_autofree gchar *with_xf86 = g_strconcat ("XF86", accelerator, NULL);
	  keyval = xkb_keysym_from_name (with_xf86, XKB_KEYSYM_CASE_INSENSITIVE);

	  if (keyval == XKB_KEY_NoSymbol)
	    return FALSE;
	}
      }

      accelerator += len;
      len -= len;
    }
  }

 out:
  combo = g_new0 (PhocKeyCombo, 1);
  combo->modifiers = mods;
  combo->keysym = keyval;
  return combo;
}


static void
phoc_keybinding_free (PhocKeybinding *self)
{
  g_slist_free_full (self->combos, (GDestroyNotify)g_free);
  g_free (self->name);
  g_free (self);
}


static gboolean
key_combo_eq (const PhocKeyCombo *sym1, const PhocKeyCombo *sym2)
{
  return (sym1->modifiers == sym2->modifiers &&
	  sym1->keysym == sym2->keysym);
}


static gboolean
keybinding_by_name (const PhocKeybinding *keybinding, const gchar *name)
{
  return g_strcmp0 (keybinding->name, name);
}


static gboolean
keybinding_by_key_combo (const PhocKeybinding *keybinding, const PhocKeyCombo *combo)
{
  GSList *elem = keybinding->combos;

  while (elem) {
    if (key_combo_eq (elem->data, combo))
      return FALSE;
    elem = elem->next;
  };
  return TRUE;
}


static void
on_keybinding_setting_changed (PhocKeybindings *self,
			       const gchar     *key,
			       GSettings       *settings)
{
  g_auto(GStrv) accelerators = NULL;
  PhocKeybinding *keybinding;
  GSList *elem;
  int i;

  g_return_if_fail (PHOC_IS_KEYBINDINGS (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  accelerators = g_settings_get_strv (settings, key);

  elem = g_slist_find_custom (self->bindings,
			      key,
			      (GCompareFunc)keybinding_by_name);
  if (!elem) {
    g_warning ("Changed keybinding %s not known", key);
    return;
  }

  keybinding = elem->data;

  g_slist_free_full (keybinding->combos, g_free);
  keybinding->combos = NULL;

  for (i = 0; accelerators && accelerators[i]; i++) {
    PhocKeyCombo *combo;

    g_debug ("New keybinding %s for %s", key, accelerators[i]);
    combo = parse_accelerator (accelerators[i]);
    if (combo)
      keybinding->combos = g_slist_append (keybinding->combos, combo);
  }
}


static gboolean
phoc_add_keybinding (PhocKeybindings *self, GSettings *settings,
		     const gchar *name, PhocKeyHandlerFunc func)
{
  g_autofree gchar *signal_name = NULL;
  PhocKeybinding *binding;

  if (g_slist_find_custom (self->bindings, name, (GCompareFunc)keybinding_by_name)) {
    g_warning ("Keybinding '%s' already exists", name);
    return FALSE;
  }

  binding = g_new0 (PhocKeybinding, 1);
  binding->name = g_strdup (name);
  binding->func = func;

  signal_name = g_strdup_printf ("changed::%s", name);
  g_signal_connect_swapped (settings, signal_name,
			    G_CALLBACK (on_keybinding_setting_changed), self);

  self->bindings = g_slist_append (self->bindings, binding);
  /* Fill in initial values */
  on_keybinding_setting_changed (self, name, settings);

  return TRUE;
}

static void
phoc_keybindings_set_property (GObject *object,
			       guint property_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_keybindings_get_property (GObject *object,
			       guint property_id,
			       GValue *value,
			       GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_keybindings_dispose (GObject *object)
{
  PhocKeybindings *self = PHOC_KEYBINDINGS (object);

  g_slist_free_full (self->bindings, (GDestroyNotify)phoc_keybinding_free);
  self->bindings = NULL;

  G_OBJECT_CLASS (phoc_keybindings_parent_class)->dispose (object);
}


static void
phoc_keybindings_finalize (GObject *object)
{
  PhocKeybindings *self = PHOC_KEYBINDINGS (object);

  g_clear_object (&self->settings);
  g_clear_object (&self->mutter_settings);

  G_OBJECT_CLASS (phoc_keybindings_parent_class)->finalize (object);
}


static void
phoc_keybindings_constructed (GObject *object)
{
  PhocKeybindings *self = PHOC_KEYBINDINGS (object);

  G_OBJECT_CLASS (phoc_keybindings_parent_class)->constructed (object);

  self->settings = g_settings_new (KEYBINDINGS_SCHEMA_ID);
  phoc_add_keybinding (self, self->settings,
		       "close", handle_close);
  phoc_add_keybinding (self, self->settings,
		       "cycle-windows", handle_cycle_windows);
  phoc_add_keybinding (self, self->settings,
		       "maximize", handle_maximize);
  phoc_add_keybinding (self, self->settings,
		       "toggle-fullscreen", handle_toggle_fullscreen);
  phoc_add_keybinding (self, self->settings,
		       "toggle-maximized", handle_toggle_maximized);
  phoc_add_keybinding (self, self->settings,
		       "move-to-monitor-right", handle_move_to_monitor_right);
  phoc_add_keybinding (self, self->settings,
		       "move-to-monitor-left", handle_move_to_monitor_left);
  /* TODO: we need a real switch-applications but ALT-TAB should do s.th.
   * useful */
  phoc_add_keybinding (self, self->settings,
		       "switch-applications", handle_cycle_windows);
  phoc_add_keybinding (self, self->settings,
		       "unmaximize", handle_unmaximize);
  phoc_add_keybinding (self, self->settings,
		       "switch-input-source", handle_switch_input_source);

  self->mutter_settings = g_settings_new (MUTTER_KEYBINDINGS_SCHEMA_ID);
  phoc_add_keybinding (self, self->mutter_settings,
		       "toggle-tiled-left", handle_tile_left);
  phoc_add_keybinding (self, self->mutter_settings,
		       "toggle-tiled-right", handle_tile_right);

  G_OBJECT_CLASS (phoc_keybindings_parent_class)->constructed (object);
}


static void
phoc_keybindings_class_init (PhocKeybindingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phoc_keybindings_constructed;
  object_class->dispose = phoc_keybindings_dispose;
  object_class->finalize = phoc_keybindings_finalize;
  object_class->set_property = phoc_keybindings_set_property;
  object_class->get_property = phoc_keybindings_get_property;
}


static void
phoc_keybindings_init (PhocKeybindings *self)
{
  self->bindings = NULL;
}


PhocKeybindings *
phoc_keybindings_new (void)
{
  return g_object_new (PHOC_TYPE_KEYBINDINGS, NULL);
}

/**
 * phoc_keybindings_handle_pressed:
 *
 * Check if a keybinding is known and run the associated action
 */
gboolean
phoc_keybindings_handle_pressed (PhocKeybindings *self,
				 guint32 modifiers,
				 xkb_keysym_t *pressed_keysyms,
				 guint32 length,
				 struct roots_seat *seat)
{
  PhocKeybinding *keybinding;
  GSList *elem;
  PhocKeyCombo combo;

  if (length != 1)
    return FALSE;

  combo.keysym = pressed_keysyms[0];
  combo.modifiers = modifiers;

  elem = g_slist_find_custom (self->bindings,
			      &combo,
			      (GCompareFunc)keybinding_by_key_combo);
  if (!elem)
    return FALSE;

  g_return_val_if_fail (elem->data, FALSE);
  keybinding = elem->data;

  (*keybinding->func) (seat);
  return TRUE;
}
