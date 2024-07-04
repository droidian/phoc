#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include "settings.h"
#include "seat.h"
#include "view.h"

G_BEGIN_DECLS

#define PHOC_TYPE_INPUT (phoc_input_get_type ())

G_DECLARE_FINAL_TYPE (PhocInput, phoc_input, PHOC, INPUT, GObject);

PhocInput         *phoc_input_new (void);
bool               phoc_input_view_has_focus (PhocInput         *self,
                                              PhocView          *view);
const char        *phoc_input_get_device_type (enum wlr_input_device_type type);
PhocSeat          *phoc_input_get_seat (PhocInput *self, char *name);
void               phoc_input_update_cursor_focus (PhocInput *self);
GSList *           phoc_input_get_seats          (PhocInput *self);
PhocSeat          *phoc_input_get_last_active_seat (PhocInput *self);

G_END_DECLS
