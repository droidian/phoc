/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0+
 */
#pragma once

#include <glib-object.h>
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-xkb-info.h>

#include <wlr/types/wlr_keyboard.h>

G_BEGIN_DECLS

#define PHOC_TYPE_KEYBINDINGS (phoc_keybindings_get_type())

G_DECLARE_FINAL_TYPE (PhocKeybindings, phoc_keybindings, PHOC, KEYBINDINGS, GObject);

PhocKeybindings *phoc_keybindings_new (void);

/**
 * PhocKeyCombo:
 *
 * A combination of modifiers and a key describing a keyboard shortcut
 */
typedef struct
{
  guint32 modifiers;
  xkb_keysym_t keysym;
} PhocKeyCombo;

typedef struct _PhocSeat PhocSeat;
gboolean         phoc_keybindings_handle_pressed (PhocKeybindings *self,
                                                  guint32 modifiers,
                                                  xkb_keysym_t *pressed_keysyms,
                                                  guint32 length,
                                                  PhocSeat *seat);
PhocKeyCombo *parse_accelerator (const gchar * accelerator);
G_END_DECLS
