/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-device-state"

#include "phoc-config.h"
#include "device-state.h"
#include "phoc-enums.h"
#include "seat.h"
#include "server.h"
#include "switch.h"
#include "utils.h"

#include <glib-object.h>

#define DEVICE_STATE_PROTOCOL_VERSION 2

enum {
  PROP_0,
  PROP_SEAT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


typedef struct _PhocTabletModeSwitch {
  struct wl_resource *resource;

  PhocDeviceState    *device_state;
} PhocTabletModeSwitch;


typedef struct _PhocLidSwitch {
  struct wl_resource *resource;

  PhocDeviceState    *device_state;
} PhocLidSwitch;


/**
 * PhocDeviceState:
 *
 * Device state protocol:
 */
struct _PhocDeviceState {
  GObject             parent;

  PhocSeat           *seat; /* unowned, we're embedded in this seat */
  enum zphoc_device_state_v1_capability
                      caps;

  struct wl_global   *global;
  GSList             *resources;

  GSList             *tablet_mode_switches;
  PhocSwitchState     tablet_mode_state;

  GSList             *lid_switches;
  PhocSwitchState     lid_state;
};

G_DEFINE_TYPE (PhocDeviceState, phoc_device_state, G_TYPE_OBJECT)

static PhocDeviceState    *phoc_device_state_from_resource    (struct wl_resource *resource);

static void
resource_handle_destroy(struct wl_client *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}


static const struct zphoc_tablet_mode_switch_v1_interface tablet_mode_switch_v1_impl = {
  .destroy = resource_handle_destroy,
};


static PhocTabletModeSwitch *
phoc_tablet_mode_switch_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_tablet_mode_switch_v1_interface,
                                     &tablet_mode_switch_v1_impl));
  return wl_resource_get_user_data (resource);
}


static void
phoc_tablet_mode_switch_destroy (PhocTabletModeSwitch *tablet_mode_switch)
{
  PhocDeviceState *device_state;

  if (tablet_mode_switch == NULL)
    return;

  g_debug ("Destroying tablet_mode_switch %p (res %p)",
           tablet_mode_switch,
           tablet_mode_switch->resource);
  device_state = PHOC_DEVICE_STATE (tablet_mode_switch->device_state);
  g_assert (PHOC_IS_DEVICE_STATE (device_state));

  device_state->tablet_mode_switches = g_slist_remove (device_state->tablet_mode_switches,
                                                       tablet_mode_switch);

  wl_resource_set_user_data (tablet_mode_switch->resource, NULL);
  g_free (tablet_mode_switch);
}


static void
tablet_mode_switch_handle_resource_destroy (struct wl_resource *resource)
{
  PhocTabletModeSwitch *tablet_mode_switch = phoc_tablet_mode_switch_from_resource (resource);

  phoc_tablet_mode_switch_destroy (tablet_mode_switch);
}


static void
handle_get_tablet_mode_switch (struct wl_client   *client,
                               struct wl_resource *device_state_resource,
                               uint32_t            id)
{
  PhocDeviceState *self;
  g_autofree PhocTabletModeSwitch *tablet_mode_switch = NULL;
  int version;

  self = phoc_device_state_from_resource (device_state_resource);
  g_assert (PHOC_IS_DEVICE_STATE (self));

  tablet_mode_switch = g_new0 (PhocTabletModeSwitch, 1);

  version = wl_resource_get_version (device_state_resource);
  tablet_mode_switch->device_state = self;
  tablet_mode_switch->resource = wl_resource_create (client,
                                                     &zphoc_tablet_mode_switch_v1_interface,
                                                     version,
                                                     id);
  if (tablet_mode_switch->resource == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  g_debug ("New tablet_mode_switch %p (res %p)", tablet_mode_switch, tablet_mode_switch->resource);
  wl_resource_set_implementation (tablet_mode_switch->resource,
                                  &tablet_mode_switch_v1_impl,
                                  tablet_mode_switch,
                                  tablet_mode_switch_handle_resource_destroy);

  /* Send initial state when known */
  switch (self->tablet_mode_state) {
  case PHOC_SWITCH_STATE_ON:
    zphoc_tablet_mode_switch_v1_send_enabled (tablet_mode_switch->resource);
    break;
  case PHOC_SWITCH_STATE_OFF:
    zphoc_tablet_mode_switch_v1_send_disabled (tablet_mode_switch->resource);
    break;
  default:
    /* nothing to do */
    g_assert_not_reached ();
  }

  self->tablet_mode_switches = g_slist_prepend (self->tablet_mode_switches,
                                                g_steal_pointer (&tablet_mode_switch));
}


static const struct zphoc_lid_switch_v1_interface lid_switch_v1_impl = {
  .destroy = resource_handle_destroy,
};


static PhocLidSwitch *
phoc_lid_switch_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_lid_switch_v1_interface,
                                     &lid_switch_v1_impl));
  return wl_resource_get_user_data (resource);
}


static void
phoc_lid_switch_destroy (PhocLidSwitch *lid_switch)
{
  PhocDeviceState *device_state;

  if (lid_switch == NULL)
    return;

  g_debug ("Destroying lid_switch %p (res %p)",
           lid_switch,
           lid_switch->resource);
  device_state = PHOC_DEVICE_STATE (lid_switch->device_state);
  g_assert (PHOC_IS_DEVICE_STATE (device_state));

  device_state->lid_switches = g_slist_remove (device_state->lid_switches,
                                               lid_switch);

  wl_resource_set_user_data (lid_switch->resource, NULL);
  g_free (lid_switch);
}


static void
lid_switch_handle_resource_destroy (struct wl_resource *resource)
{
  PhocLidSwitch *lid_switch = phoc_lid_switch_from_resource (resource);

  phoc_lid_switch_destroy (lid_switch);
}


static void
handle_get_lid_switch (struct wl_client   *client,
                       struct wl_resource *device_state_resource,
                       uint32_t            id)
{
  PhocDeviceState *self;
  g_autofree PhocLidSwitch *lid_switch = NULL;
  int version;

  self = phoc_device_state_from_resource (device_state_resource);
  g_assert (PHOC_IS_DEVICE_STATE (self));

  lid_switch = g_new0 (PhocLidSwitch, 1);

  version = wl_resource_get_version (device_state_resource);
  lid_switch->device_state = self;
  lid_switch->resource = wl_resource_create (client,
                                             &zphoc_lid_switch_v1_interface,
                                             version,
                                             id);
  if (lid_switch->resource == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  g_debug ("New lid_switch %p (res %p)", lid_switch, lid_switch->resource);
  wl_resource_set_implementation (lid_switch->resource,
                                  &lid_switch_v1_impl,
                                  lid_switch,
                                  lid_switch_handle_resource_destroy);

  /* Send initial state when known */
  switch (self->lid_state) {
  case PHOC_SWITCH_STATE_ON:
    zphoc_lid_switch_v1_send_closed (lid_switch->resource);
    break;
  case PHOC_SWITCH_STATE_OFF:
    zphoc_lid_switch_v1_send_opened (lid_switch->resource);
    break;
  default:
    /* nothing to do */
    g_assert_not_reached ();
  }

  self->lid_switches = g_slist_prepend (self->lid_switches,
                                        g_steal_pointer (&lid_switch));
}

static void
device_state_handle_resource_destroy (struct wl_resource *resource)
{
  PhocDeviceState *self = wl_resource_get_user_data (resource);

  g_assert (PHOC_IS_DEVICE_STATE (self));

  g_debug ("Destroying device_state %p (res %p)", self, resource);
  self->resources = g_slist_remove (self->resources, resource);
}


static const struct zphoc_device_state_v1_interface device_state_impl = {
  .get_tablet_mode_switch = handle_get_tablet_mode_switch,
  .get_lid_switch = handle_get_lid_switch,
};


static void
device_state_bind (struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
  PhocDeviceState *self = PHOC_DEVICE_STATE (data);
  struct wl_resource *resource  = wl_resource_create (client, &zphoc_device_state_v1_interface,
                                                      version, id);

  g_assert (PHOC_IS_DEVICE_STATE (self));

  wl_resource_set_implementation (resource,
                                  &device_state_impl,
                                  self,
                                  device_state_handle_resource_destroy);

  self->resources = g_slist_prepend (self->resources, resource);

  /* Send initial capabilities */
  zphoc_device_state_v1_send_capabilities (resource, self->caps);
  return;
}


static PhocDeviceState *
phoc_device_state_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_device_state_v1_interface,
                                     &device_state_impl));
  return wl_resource_get_user_data (resource);
}


static void
phoc_device_state_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PhocDeviceState *self = PHOC_DEVICE_STATE (object);

  switch (property_id) {
  case PROP_SEAT:
    self->seat = g_value_get_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_device_state_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PhocDeviceState *self = PHOC_DEVICE_STATE (object);

  switch (property_id) {
  case PROP_SEAT:
    g_value_set_object (value, self->seat);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_device_state_finalize (GObject *object)
{
  PhocDeviceState *self = PHOC_DEVICE_STATE (object);

  if (self->tablet_mode_switches)
    g_slist_free_full (g_steal_pointer (&self->tablet_mode_switches), g_free);

  wl_global_destroy (self->global);

  G_OBJECT_CLASS (phoc_device_state_parent_class)->finalize (object);
}


static void
phoc_device_state_class_init (PhocDeviceStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phoc_device_state_finalize;
  object_class->get_property = phoc_device_state_get_property;
  object_class->set_property = phoc_device_state_set_property;

  props[PROP_SEAT] =
    g_param_spec_object ("seat", "", "",
                         PHOC_TYPE_SEAT,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_device_state_init (PhocDeviceState *self)
{
  struct wl_display *wl_display = phoc_server_get_wl_display (phoc_server_get_default ());

  self->global = wl_global_create (wl_display, &zphoc_device_state_v1_interface,
                                   DEVICE_STATE_PROTOCOL_VERSION, self, device_state_bind);

}


PhocDeviceState *
phoc_device_state_new (PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_DEVICE_STATE,
                       "seat", seat,
                       NULL);
}


void
phoc_device_state_update_capabilities (PhocDeviceState *self)
{
  uint32_t caps = 0;

  g_assert (PHOC_IS_DEVICE_STATE (self));

  if (phoc_seat_has_switch (self->seat, WLR_SWITCH_TYPE_TABLET_MODE))
    caps |= ZPHOC_DEVICE_STATE_V1_CAPABILITY_TABLET_MODE_SWITCH;

  if (phoc_seat_has_switch (self->seat, WLR_SWITCH_TYPE_LID))
    caps |= ZPHOC_DEVICE_STATE_V1_CAPABILITY_LID_SWITCH;

  if (phoc_seat_has_hw_keyboard (self->seat))
    caps |= ZPHOC_DEVICE_STATE_V1_CAPABILITY_KEYBOARD;

  if (caps == self->caps)
    return;

  self->caps = caps;

  /* Send out updated capabilities */
  for (GSList *l = self->resources; l; l = l->next) {
    struct wl_resource *resource = l->data;
    uint32_t versioned_caps = caps;
    int version;

    version = wl_resource_get_version (resource);
    if (version < ZPHOC_DEVICE_STATE_V1_CAPABILITY_KEYBOARD_SINCE_VERSION)
      versioned_caps &= ~ZPHOC_DEVICE_STATE_V1_CAPABILITY_KEYBOARD;

    zphoc_device_state_v1_send_capabilities (resource, versioned_caps);
  }
}


void
phoc_device_state_notify_lid_change (PhocDeviceState *self, gboolean closed)
{
  g_assert (PHOC_IS_DEVICE_STATE (self));
  PhocSwitchState state = closed ? PHOC_SWITCH_STATE_ON : PHOC_SWITCH_STATE_OFF;

  if (self->lid_state == state)
    return;
  self->lid_state = state;

  for (GSList *l = self->lid_switches; l; l = l->next) {
    PhocLidSwitch *switch_ = l->data;

    if (closed)
      zphoc_lid_switch_v1_send_closed (switch_->resource);
    else
      zphoc_lid_switch_v1_send_opened (switch_->resource);
  }
}

void
phoc_device_state_notify_keypad_switch_change (PhocDeviceState *self, gboolean closed)
{
  g_assert (PHOC_IS_DEVICE_STATE (self));
  PhocSwitchState state = closed ? PHOC_SWITCH_STATE_ON : PHOC_SWITCH_STATE_OFF;

  phoc_phosh_private_forward_switch_event (3 /* LIBINPUT_SWITCH_KEYPAD_SLIDE */, closed);
}


void
phoc_device_state_notify_tablet_mode_change (PhocDeviceState *self, gboolean enabled)
{
  g_assert (PHOC_IS_DEVICE_STATE (self));
  PhocSwitchState state = enabled ? PHOC_SWITCH_STATE_ON : PHOC_SWITCH_STATE_OFF;

  if (self->tablet_mode_state == state)
    return;
  self->tablet_mode_state = state;

  for (GSList *l = self->tablet_mode_switches; l; l = l->next) {
    PhocTabletModeSwitch *switch_ = l->data;

    if (enabled)
      zphoc_tablet_mode_switch_v1_send_enabled (switch_->resource);
    else
      zphoc_tablet_mode_switch_v1_send_disabled (switch_->resource);
  }
}
