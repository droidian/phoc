#define G_LOG_DOMAIN "phoc-switch"

#include "config.h"

#include <stdlib.h>
#include <wlr/util/log.h>
#include "switch.h"

void roots_switch_handle_toggle(struct roots_switch *switch_device,
		struct wlr_event_switch_toggle *event) {
	PhocServer *server = phoc_server_get_default ();
	struct wl_list *bound_switches =
		&server->config->switches;
	struct roots_switch_config *sc;
	wl_list_for_each(sc, bound_switches, link) {
		if ((sc->name != NULL && strcmp(event->device->name, sc->name) != 0) &&
				(sc->name == NULL && event->switch_type != sc->switch_type)) {
			continue;
		}
		if (sc->switch_state != WLR_SWITCH_STATE_TOGGLE &&
				event->switch_state != sc->switch_state) {
			continue;
		}
		g_debug ("Handling switch event %i (state %i)", event->switch_type,
		         event->switch_state);

		if (!phosh_forward_switch_event (event)) {
			/* FIXME: If the switch reliability is set to RELIABILITY_RELIABLE
			 * and the lid/slide is closed, libinput will dispatch an
			 * event just after the device has been added -- but
			 * this means that the phosh private protocol is not ready
			 * yet.
			 *
			 * Workaround this by trying again after one second. This
			 * is less than ideal and should be changed for something
			 * better.
			*/
			/* TODO REIMPLEMENT THIS */
			g_warning ("Fail");
		}
	}
}
