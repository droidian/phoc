#define G_LOG_DOMAIN "phoc-input-method-relay"

#include "phoc-config.h"

#include "seat.h"
#include "server.h"
#include "input-method-relay.h"

#include <gmobile.h>

#include <assert.h>
#include <stdlib.h>

/**
 * PhocTextInput:
 * @pending_focused_surface: The surface getting seat's focus. Stored for when text-input cannot
 *    be sent an enter event immediately after getting focus, e.g. when
 *    there's no input method available. Cleared once text-input is entered.
 */
typedef struct _PhocTextInput {
  PhocInputMethodRelay *relay;

  struct wlr_text_input_v3 *input;
  struct wlr_surface *pending_focused_surface;

  struct wl_list link;

  struct wl_listener pending_focused_surface_destroy;
  struct wl_listener enable;
  struct wl_listener commit;
  struct wl_listener disable;
  struct wl_listener destroy;
} PhocTextInput;


static void
elevate_osk (struct wlr_surface *surface)
{
  struct wlr_layer_surface_v1 *layer;

  if (!surface)
    return;

  layer = wlr_layer_surface_v1_try_from_wlr_surface (surface);
  if (!layer)
    return;

  phoc_layer_shell_update_osk (PHOC_OUTPUT (layer->output->data), TRUE);
}


static PhocTextInput *
relay_get_focusable_text_input (PhocInputMethodRelay *relay)
{
  PhocTextInput *text_input = NULL;
  wl_list_for_each (text_input, &relay->text_inputs, link) {
    if (text_input->pending_focused_surface)
      return text_input;
  }
  return NULL;
}


static PhocTextInput *
relay_get_focused_text_input (PhocInputMethodRelay *relay)
{
  PhocTextInput *text_input = NULL;

  wl_list_for_each (text_input, &relay->text_inputs, link) {
    if (text_input->input->focused_surface) {
      g_assert (text_input->pending_focused_surface == NULL);
      return text_input;
    }
  }
  return NULL;
}


static void
handle_im_commit (struct wl_listener *listener, void *data)
{
  PhocInputMethodRelay *relay = wl_container_of (listener, relay, input_method_commit);
  PhocTextInput *text_input = relay_get_focused_text_input (relay);

  if (!text_input)
    return;

  struct wlr_input_method_v2 *context = data;
  g_assert (context == relay->input_method);
  if (context->current.preedit.text) {
    wlr_text_input_v3_send_preedit_string (text_input->input,
                                           context->current.preedit.text,
                                           context->current.preedit.cursor_begin,
                                           context->current.preedit.cursor_end);
  }
  if (context->current.commit_text) {
    wlr_text_input_v3_send_commit_string (text_input->input,
                                          context->current.commit_text);
  }
  if (context->current.delete.before_length
      || context->current.delete.after_length) {
    wlr_text_input_v3_send_delete_surrounding_text (text_input->input,
                                                    context->current.delete.before_length,
                                                    context->current.delete.after_length);
  }
  wlr_text_input_v3_send_done (text_input->input);
}


static void
handle_im_keyboard_grab_destroy (struct wl_listener *listener, void *data)
{
  PhocInputMethodRelay *relay =
    wl_container_of (listener, relay, input_method_keyboard_grab_destroy);
  struct wlr_input_method_keyboard_grab_v2 *keyboard_grab = data;

  wl_list_remove (&relay->input_method_keyboard_grab_destroy.link);

  if (keyboard_grab->keyboard) {
    /* send modifier state to original client */
    wlr_seat_keyboard_notify_modifiers (keyboard_grab->input_method->seat,
                                        &keyboard_grab->keyboard->modifiers);
  }
}


static void
handle_im_grab_keyboard (struct wl_listener *listener, void *data)
{
  PhocInputMethodRelay *relay =
    wl_container_of (listener, relay, input_method_grab_keyboard);
  struct wlr_input_method_keyboard_grab_v2 *keyboard_grab = data;

  /* send modifier state to grab */
  struct wlr_keyboard *active_keyboard = wlr_seat_get_keyboard (relay->seat->seat);
  wlr_input_method_keyboard_grab_v2_set_keyboard (keyboard_grab, active_keyboard);

  wl_signal_add (&keyboard_grab->events.destroy, &relay->input_method_keyboard_grab_destroy);
  relay->input_method_keyboard_grab_destroy.notify = handle_im_keyboard_grab_destroy;
}


static void
text_input_clear_pending_focused_surface (PhocTextInput *text_input)
{
  wl_list_remove (&text_input->pending_focused_surface_destroy.link);
  wl_list_init (&text_input->pending_focused_surface_destroy.link);
  text_input->pending_focused_surface = NULL;
}


static void
text_input_set_pending_focused_surface (PhocTextInput *text_input, struct wlr_surface *surface)
{
  text_input_clear_pending_focused_surface (text_input);
  g_assert (surface);
  text_input->pending_focused_surface = surface;
  wl_signal_add (&surface->events.destroy, &text_input->pending_focused_surface_destroy);
}


static void
handle_im_destroy (struct wl_listener *listener, void *data)
{
  PhocInputMethodRelay *relay = wl_container_of (listener, relay, input_method_destroy);
  struct wlr_input_method_v2 *context = data;

  g_assert (context == relay->input_method);
  relay->input_method = NULL;
  PhocTextInput *text_input = relay_get_focused_text_input (relay);
  if (text_input) {
    /* keyboard focus is still there, so keep the surface at hand in case
     * the input method returns */
    g_assert (text_input->pending_focused_surface == NULL);
    text_input_set_pending_focused_surface (text_input,
                                            text_input->input->focused_surface);
    wlr_text_input_v3_send_leave (text_input->input);
  }
}


static bool
text_input_is_focused (struct wlr_text_input_v3 *text_input)
{
  /* phoc_input_method_relay_set_focus ensures
   * that focus sits on the single text input with focused_surface set. */
  return text_input->focused_surface != NULL;
}


static void
relay_send_im_done (PhocInputMethodRelay *relay, struct wlr_text_input_v3 *input)
{
  struct wlr_input_method_v2 *input_method = relay->input_method;

  if (!input_method) {
    g_debug ("Sending IM_DONE but im is gone");
    return;
  }
  if (!text_input_is_focused (input)) {
    /* Don't let input method know about events from unfocused surfaces. */
    return;
  }
  /* TODO: only send each of those if they were modified */
  if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
    wlr_input_method_v2_send_surrounding_text (input_method,
                                               input->current.surrounding.text,
                                               input->current.surrounding.cursor,
                                               input->current.surrounding.anchor);
  }
  wlr_input_method_v2_send_text_change_cause (input_method,
                                              input->current.text_change_cause);
  if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
    wlr_input_method_v2_send_content_type (input_method,
                                           input->current.content_type.hint,
                                           input->current.content_type.purpose);
  }
  wlr_input_method_v2_send_done (input_method);
  /* TODO: pass intent, display popup size */
}


static void
handle_text_input_enable (struct wl_listener *listener, void *data)
{
  PhocTextInput *text_input = wl_container_of (listener, text_input, enable);
  PhocInputMethodRelay *relay = text_input->relay;

  if (relay->input_method == NULL) {
    g_debug ("Enabling text input when input method is gone");
    return;
  }
  /* relay_send_im_done protects from receiving unfocussed done,
   * but activate must be prevented too.
   * TODO: when enter happens? */
  if (!text_input_is_focused (text_input->input))
    return;

  wlr_input_method_v2_send_activate (relay->input_method);
  relay_send_im_done (relay, text_input->input);

  elevate_osk (text_input->input->focused_surface);
}


static void
handle_text_input_commit (struct wl_listener *listener, void *data)
{
  PhocTextInput *text_input = wl_container_of (listener, text_input, commit);
  PhocInputMethodRelay *relay = text_input->relay;

  if (!text_input->input->current_enabled) {
    g_debug ("Inactive text input tried to commit an update");
    return;
  }
  g_debug ("Text input committed update");
  if (relay->input_method == NULL) {
    g_debug ("Text input committed, but input method is gone");
    return;
  }
  relay_send_im_done (relay, text_input->input);
}


static void
relay_disable_text_input (PhocInputMethodRelay *relay, PhocTextInput *text_input)
{
  if (relay->input_method == NULL) {
    g_debug ("Disabling text input, but input method is gone");
    return;
  }
  /* relay_send_im_done protects from receiving unfocussed done,
   * but deactivate must be prevented too */
  if (!text_input_is_focused (text_input->input))
    return;

  wlr_input_method_v2_send_deactivate (relay->input_method);
  relay_send_im_done (relay, text_input->input);
}


static void
submit_preedit (PhocInputMethodRelay *self, PhocTextInput *text_input)
{
  struct wlr_input_method_v2_preedit_string *preedit;

  if (!self->input_method)
    return;

  preedit = &self->input_method->current.preedit;

  if (gm_str_is_null_or_empty (preedit->text))
    return;

  g_debug ("Submitting preedit: %s", preedit->text);
  wlr_text_input_v3_send_commit_string (text_input->input, preedit->text);
  g_clear_pointer (&preedit->text, g_free);
  wlr_text_input_v3_send_done (text_input->input);
}


static void
text_input_relay_unset_focus (PhocInputMethodRelay *self, PhocTextInput *text_input)
{
  /* Submit preedit so it doesn't get lost on focus change */
  submit_preedit (self, text_input);

  relay_disable_text_input (self, text_input);
  wlr_text_input_v3_send_leave (text_input->input);
}


static void
handle_text_input_disable (struct wl_listener *listener, void *data)
{
  PhocTextInput *text_input = wl_container_of (listener, text_input, disable);
  PhocInputMethodRelay *relay = text_input->relay;

  relay_disable_text_input (relay, text_input);
}


static void
handle_text_input_destroy (struct wl_listener *listener, void *data)
{
  PhocTextInput *text_input = wl_container_of (listener, text_input, destroy);
  PhocInputMethodRelay *relay = text_input->relay;

  if (text_input->input->current_enabled)
    relay_disable_text_input (relay, text_input);

  text_input_clear_pending_focused_surface (text_input);
  wl_list_remove (&text_input->commit.link);
  wl_list_remove (&text_input->destroy.link);
  wl_list_remove (&text_input->disable.link);
  wl_list_remove (&text_input->enable.link);
  wl_list_remove (&text_input->link);
  text_input->input = NULL;
  free (text_input);
}


static void
handle_pending_focused_surface_destroy (struct wl_listener *listener, void *data)
{
  PhocTextInput *text_input = wl_container_of (listener, text_input,
                                               pending_focused_surface_destroy);
  struct wlr_surface *surface = data;

  g_assert (text_input->pending_focused_surface == surface);
  text_input_clear_pending_focused_surface (text_input);
}


static PhocTextInput *
phoc_text_input_create (PhocInputMethodRelay *relay, struct wlr_text_input_v3 *text_input)
{
  PhocTextInput *input = g_new0 (PhocTextInput, 1);

  g_debug ("New text input %p", input);

  input->input = text_input;
  input->relay = relay;

  wl_signal_add (&text_input->events.enable, &input->enable);
  input->enable.notify = handle_text_input_enable;

  wl_signal_add (&text_input->events.commit, &input->commit);
  input->commit.notify = handle_text_input_commit;

  wl_signal_add (&text_input->events.disable, &input->disable);
  input->disable.notify = handle_text_input_disable;

  wl_signal_add (&text_input->events.destroy, &input->destroy);
  input->destroy.notify = handle_text_input_destroy;

  input->pending_focused_surface_destroy.notify = handle_pending_focused_surface_destroy;
  wl_list_init (&input->pending_focused_surface_destroy.link);

  return input;
}


static void
relay_handle_text_input (struct wl_listener *listener, void *data)
{
  PhocInputMethodRelay *relay = wl_container_of (listener, relay, text_input_new);
  struct wlr_text_input_v3 *wlr_text_input = data;

  if (relay->seat->seat != wlr_text_input->seat) {
    g_warning ("Can't create text-input. Incorrect seat");
    return;
  }

  PhocTextInput *text_input = phoc_text_input_create (relay, wlr_text_input);
  g_assert (text_input);

  wl_list_insert (&relay->text_inputs, &text_input->link);

  /* If the current focus surface of the seat is the same client make sure we send
     an enter event */
  PhocView *focus_view = phoc_seat_get_focus_view (relay->seat);
  if (phoc_view_is_mapped (focus_view)) {
    if (wl_resource_get_client (wlr_text_input->resource) ==
        wl_resource_get_client (focus_view->wlr_surface->resource))
      phoc_input_method_relay_set_focus (relay, focus_view->wlr_surface);
  }
}


static void
relay_handle_input_method (struct wl_listener *listener, void *data)
{
  PhocInputMethodRelay *relay = wl_container_of (listener, relay, input_method_new);
  struct wlr_input_method_v2 *input_method = data;

  if (relay->seat->seat != input_method->seat) {
    g_warning ("Attempted to input method for wrong seat");
    return;
  }

  if (relay->input_method != NULL) {
    g_debug ("Attempted to connect second input method to a seat");
    wlr_input_method_v2_send_unavailable (input_method);
    return;
  }

  g_debug ("Input method available");
  relay->input_method = input_method;

  wl_signal_add (&relay->input_method->events.commit, &relay->input_method_commit);
  relay->input_method_commit.notify = handle_im_commit;

  wl_signal_add (&relay->input_method->events.grab_keyboard, &relay->input_method_grab_keyboard);
  relay->input_method_grab_keyboard.notify = handle_im_grab_keyboard;

  wl_signal_add (&relay->input_method->events.destroy, &relay->input_method_destroy);
  relay->input_method_destroy.notify = handle_im_destroy;

  PhocTextInput *text_input = relay_get_focusable_text_input (relay);
  if (text_input) {
    wlr_text_input_v3_send_enter (text_input->input, text_input->pending_focused_surface);
    text_input_clear_pending_focused_surface (text_input);
  }
}


void
phoc_input_method_relay_init (PhocSeat *seat, PhocInputMethodRelay *relay)
{
  PhocDesktop *desktop = phoc_server_get_desktop (phoc_server_get_default ());

  g_assert (PHOC_IS_SEAT (seat));
  relay->seat = seat;
  wl_list_init (&relay->text_inputs);

  relay->text_input_new.notify = relay_handle_text_input;
  wl_signal_add (&desktop->text_input->events.text_input, &relay->text_input_new);

  relay->input_method_new.notify = relay_handle_input_method;
  wl_signal_add (&desktop->input_method->events.input_method, &relay->input_method_new);
}


void
phoc_input_method_relay_destroy (PhocInputMethodRelay *relay)
{
  wl_list_remove (&relay->text_input_new.link);
  wl_list_remove (&relay->input_method_new.link);
}

/**
 * phoc_input_method_relay_set_focus:
 * @relay: The input method relay
 * @surface: The surface to focus
 *
 * Updates the currently focused surface. Surface must belong to the
 * same seat.
 */
void
phoc_input_method_relay_set_focus (PhocInputMethodRelay *relay, struct wlr_surface *surface)
{
  PhocTextInput *text_input;

  wl_list_for_each (text_input, &relay->text_inputs, link) {

    if (text_input->pending_focused_surface) {
      g_assert (text_input->input->focused_surface == NULL);
      if (surface != text_input->pending_focused_surface)
        text_input_clear_pending_focused_surface (text_input);
    } else if (text_input->input->focused_surface) {
      g_assert (text_input->pending_focused_surface == NULL);
      if (surface != text_input->input->focused_surface)
        text_input_relay_unset_focus (relay, text_input);
    }

    if (surface
        && wl_resource_get_client (text_input->input->resource)
        == wl_resource_get_client (surface->resource)) {

      if (relay->input_method) {
        if (surface != text_input->input->focused_surface)
          wlr_text_input_v3_send_enter (text_input->input, surface);

      } else if (surface != text_input->pending_focused_surface) {
        text_input_set_pending_focused_surface (text_input, surface);
      }
    }
  }
}

/**
 * phoc_input_method_relay_is_enabled:
 * @relay: The input method relay
 * @surface: The surface to check
 *
 * Checks whether input method is currently enabled for surface.
 *
 * Returns: `true` if input method is enabled, otherwise `false`.
 */
bool
phoc_input_method_relay_is_enabled (PhocInputMethodRelay *relay, struct wlr_surface *surface)
{
  PhocTextInput *text_input;

  g_return_val_if_fail (surface, false);

  surface = wlr_surface_get_root_surface (surface);
  wl_list_for_each (text_input, &relay->text_inputs, link) {
    if (!text_input->input->focused_surface)
      continue;

    struct wlr_surface *focused =
      wlr_surface_get_root_surface (text_input->input->focused_surface);

    if (focused == surface && text_input->input->current_enabled)
      return true;
  }
  return false;
}

/**
 * phoc_input_method_relay_im_submit:
 * @self: The relay
 *
 * Submit the input method's state if the given surface has focus.
 * This allows to e.g. submit any preedit when needed.
 */
void
phoc_input_method_relay_im_submit (PhocInputMethodRelay *self,
                                   struct wlr_surface   *surface)
{
  PhocTextInput *text_input;

  g_assert (self);
  g_assert (surface);

  if (!self->input_method)
    return;

  text_input = relay_get_focused_text_input (self);
  if (!text_input)
    return;

  if (text_input->input->focused_surface != surface)
    return;

  submit_preedit (self, text_input);
}
