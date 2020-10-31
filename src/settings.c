#define G_LOG_DOMAIN "phoc-settings"

#include "config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <unistd.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/util/log.h>
#include "settings.h"
#include "ini.h"
#include "utils.h"

static bool parse_modeline(const char *s, drmModeModeInfo *mode) {
	char hsync[16];
	char vsync[16];
	float fclock;

	mode->type = DRM_MODE_TYPE_USERDEF;

	if (sscanf(s, "%f %hd %hd %hd %hd %hd %hd %hd %hd %15s %15s",
		   &fclock,
		   &mode->hdisplay,
		   &mode->hsync_start,
		   &mode->hsync_end,
		   &mode->htotal,
		   &mode->vdisplay,
		   &mode->vsync_start,
		   &mode->vsync_end,
		   &mode->vtotal, hsync, vsync) != 11) {
		return false;
	}

	mode->clock = fclock * 1000;
	mode->vrefresh = mode->clock * 1000.0 * 1000.0
		/ mode->htotal / mode->vtotal;
	if (strcasecmp(hsync, "+hsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	} else if (strcasecmp(hsync, "-hsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_NHSYNC;
	} else {
		return false;
	}

	if (strcasecmp(vsync, "+vsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	} else if (strcasecmp(vsync, "-vsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_NVSYNC;
	} else {
		return false;
	}

	snprintf(mode->name, sizeof(mode->name), "%dx%d@%d",
		 mode->hdisplay, mode->vdisplay, mode->vrefresh / 1000);

	return true;
}

static void add_switch_config(struct wl_list *switches, const char *switch_name,
		const char *action, const char *command) {
	struct roots_switch_config *sc =
		calloc(1, sizeof(struct roots_switch_config));

	if (strcmp(switch_name, "tablet") == 0) {
		sc->switch_type = WLR_SWITCH_TYPE_TABLET_MODE;
	} else if (strcmp(switch_name, "lid") == 0) {
		sc->switch_type = WLR_SWITCH_TYPE_LID;
	} else if (strcmp(switch_name, "keypad_slide") == 0) {
		sc->switch_type = WLR_SWITCH_TYPE_KEYPAD_SLIDE;
	} else {
		sc->switch_type = -1;
		sc->name = strdup(switch_name);
	}

	if (strcmp(action, "on") == 0) {
		sc->switch_state = WLR_SWITCH_STATE_ON;
	} else if (strcmp(action, "off") == 0) {
		sc->switch_state = WLR_SWITCH_STATE_OFF;
	} else if (strcmp(action, "toggle") == 0) {
		sc->switch_state = WLR_SWITCH_STATE_TOGGLE;
	} else {
		wlr_log(WLR_ERROR, "Invalid switch action %s for switch %s:%s",
			action, switch_name, action);
		free(sc);
		return;
	}

	sc->command = strdup(command);
	wl_list_insert(switches, &sc->link);
}

static void config_handle_cursor(struct roots_config *config,
		const char *seat_name, const char *name, const char *value) {
	struct roots_cursor_config *cc;
	bool found = false;
	wl_list_for_each(cc, &config->cursors, link) {
		if (strcmp(cc->seat, seat_name) == 0) {
			found = true;
			break;
		}
	}

	if (!found) {
		cc = calloc(1, sizeof(struct roots_cursor_config));
		cc->seat = strdup(seat_name);
		wl_list_insert(&config->cursors, &cc->link);
	}

	if (strcmp(name, "theme") == 0) {
		free(cc->theme);
		cc->theme = strdup(value);
	} else if (strcmp(name, "default-image") == 0) {
		free(cc->default_image);
		cc->default_image = strdup(value);
	} else {
		wlr_log(WLR_ERROR, "got unknown cursor config: %s", name);
	}
}

static const char *output_prefix = "output:";
static const char *device_prefix = "device:";
static const char *cursor_prefix = "cursor:";
static const char *switch_prefix = "switch:";

static int config_ini_handler(void *user, const char *section, const char *name,
		const char *value) {
	struct roots_config *config = user;
	if (strcmp(section, "core") == 0) {
		if (strcmp(name, "xwayland") == 0) {
			if (strcasecmp(value, "true") == 0) {
				config->xwayland = true;
			} else if (strcasecmp(value, "immediate") == 0) {
				config->xwayland = true;
				config->xwayland_lazy = false;
			} else if (strcasecmp(value, "false") == 0) {
				config->xwayland = false;
			} else {
				wlr_log(WLR_ERROR, "got unknown xwayland value: %s", value);
			}
		} else {
			wlr_log(WLR_ERROR, "got unknown core config: %s", name);
		}
	} else if (strncmp(output_prefix, section, strlen(output_prefix)) == 0) {
		const char *output_name = section + strlen(output_prefix);
		struct roots_output_config *oc;
		bool found = false;

		wl_list_for_each(oc, &config->outputs, link) {
			if (strcmp(oc->name, output_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			oc = calloc(1, sizeof(struct roots_output_config));
			oc->name = strdup(output_name);
			oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			oc->scale = 1;
			oc->enable = true;
			wl_list_init(&oc->modes);
			wl_list_insert(&config->outputs, &oc->link);
		}

		if (strcmp(name, "enable") == 0) {
			if (strcasecmp(value, "true") == 0) {
				oc->enable = true;
			} else if (strcasecmp(value, "false") == 0) {
				oc->enable = false;
			} else {
				wlr_log(WLR_ERROR, "got invalid output enable value: %s", value);
			}
		} else if (strcmp(name, "x") == 0) {
			oc->x = strtol(value, NULL, 10);
		} else if (strcmp(name, "y") == 0) {
			oc->y = strtol(value, NULL, 10);
		} else if (strcmp(name, "scale") == 0) {
			oc->scale = strtof(value, NULL);
			assert(oc->scale > 0);
		} else if (strcmp(name, "rotate") == 0) {
			if (strcmp(value, "normal") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			} else if (strcmp(value, "90") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_90;
			} else if (strcmp(value, "180") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_180;
			} else if (strcmp(value, "270") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_270;
			} else if (strcmp(value, "flipped") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED;
			} else if (strcmp(value, "flipped-90") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
			} else if (strcmp(value, "flipped-180") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
			} else if (strcmp(value, "flipped-270") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
			} else {
				wlr_log(WLR_ERROR, "got unknown transform value: %s", value);
			}
			/* Make sure we rotate clockwise */
			phoc_utils_fix_transform(&oc->transform);
		} else if (strcmp(name, "mode") == 0) {
			char *end;
			oc->mode.width = strtol(value, &end, 10);
			assert(*end == 'x');
			++end;
			oc->mode.height = strtol(end, &end, 10);
			if (*end) {
				assert(*end == '@');
				++end;
				oc->mode.refresh_rate = strtof(end, &end);
				assert(strcmp("Hz", end) == 0);
			}
			wlr_log(WLR_DEBUG, "Configured output %s with mode %dx%d@%f",
					oc->name, oc->mode.width, oc->mode.height,
					oc->mode.refresh_rate);
		} else if (strcmp(name, "modeline") == 0) {
			struct roots_output_mode_config *mode = calloc(1, sizeof(*mode));

			if (parse_modeline(value, &mode->info)) {
				wl_list_insert(&oc->modes, &mode->link);
			} else {
				free(mode);
				wlr_log(WLR_ERROR, "Invalid modeline: %s", value);
			}
		}
	} else if (strncmp(cursor_prefix, section, strlen(cursor_prefix)) == 0) {
		const char *seat_name = section + strlen(cursor_prefix);
		config_handle_cursor(config, seat_name, name, value);
	} else if (strcmp(section, "cursor") == 0) {
		config_handle_cursor(config, ROOTS_CONFIG_DEFAULT_SEAT_NAME, name,
			value);
	} else if (strncmp(device_prefix, section, strlen(device_prefix)) == 0) {
		const char *device_name = section + strlen(device_prefix);

		struct roots_device_config *dc;
		bool found = false;
		wl_list_for_each(dc, &config->devices, link) {
			if (strcmp(dc->name, device_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			dc = calloc(1, sizeof(struct roots_device_config));
			dc->name = strdup(device_name);
			dc->seat = strdup(ROOTS_CONFIG_DEFAULT_SEAT_NAME);
			wl_list_insert(&config->devices, &dc->link);
		}

		if (strcmp(name, "seat") == 0) {
			free(dc->seat);
			dc->seat = strdup(value);
		} else if (strcmp(name, "tap_enabled") == 0) {
			if (strcasecmp(value, "true") == 0) {
				dc->tap_enabled = true;
			} else if (strcasecmp(value, "false") == 0) {
				dc->tap_enabled = false;
			} else {
				wlr_log(WLR_ERROR,
					"got unknown tap_enabled value: %s",
					value);
			}
		} else {
			wlr_log(WLR_ERROR, "got unknown device config: %s", name);
		}
	} else if (strncmp(switch_prefix, section, strlen(switch_prefix)) == 0) {
		const char *switch_name = section + strlen(switch_prefix);
		add_switch_config(&config->switches, switch_name, name, value);
	} else {
		wlr_log(WLR_ERROR, "got unknown config section: %s", section);
	}

	return 1;
}

struct roots_config *roots_config_create(const char *config_path) {
	struct roots_config *config = calloc(1, sizeof(struct roots_config));
	if (config == NULL) {
		return NULL;
	}

	config->xwayland = true;
	config->xwayland_lazy = true;
	wl_list_init(&config->outputs);
	wl_list_init(&config->devices);
	wl_list_init(&config->cursors);
	wl_list_init(&config->switches);

	config->config_path = g_strdup(config_path);

	if (!config->config_path) {
		// get the config path from the current directory
		char cwd[MAXPATHLEN];
		if (getcwd(cwd, sizeof(cwd)) != NULL) {
			char buf[MAXPATHLEN];
			if (snprintf(buf, MAXPATHLEN, "%s/%s", cwd, "phoc.ini") >= MAXPATHLEN) {
				wlr_log(WLR_ERROR, "config path too long");
				exit(1);
			}
			config->config_path = strdup(buf);
		} else {
			wlr_log(WLR_ERROR, "could not get cwd");
			exit(1);
		}
	}

	int result = ini_parse(config->config_path, config_ini_handler, config);

	if (result == -1) {
		wlr_log(WLR_DEBUG, "No config file found. Using sensible defaults.");
	} else if (result == -2) {
		wlr_log(WLR_ERROR, "Could not allocate memory to parse config file");
		exit(1);
	} else if (result != 0) {
		wlr_log(WLR_ERROR, "Could not parse config file");
		exit(1);
	}

	config->keybindings = phoc_keybindings_new ();

	return config;
}

void roots_config_destroy(struct roots_config *config) {
	struct roots_output_config *oc, *otmp = NULL;
	wl_list_for_each_safe(oc, otmp, &config->outputs, link) {
		struct roots_output_mode_config *omc, *omctmp = NULL;
		wl_list_for_each_safe(omc, omctmp, &oc->modes, link) {
			free(omc);
		}
		free(oc->name);
		free(oc);
	}

	struct roots_device_config *dc, *dtmp = NULL;
	wl_list_for_each_safe(dc, dtmp, &config->devices, link) {
		free(dc->name);
		free(dc->seat);
		free(dc);
	}

	struct roots_cursor_config *cc, *ctmp = NULL;
	wl_list_for_each_safe(cc, ctmp, &config->cursors, link) {
		free(cc->seat);
		free(cc->theme);
		free(cc->default_image);
		free(cc);
	}

	g_object_unref (config->keybindings);

	free(config->config_path);
	free(config);
}

struct roots_output_config *roots_config_get_output(struct roots_config *config,
		struct wlr_output *output) {
	char name[88];
	snprintf(name, sizeof(name), "%s %s %s", output->make, output->model,
		output->serial);

	struct roots_output_config *oc;
	wl_list_for_each(oc, &config->outputs, link) {
		if (strcmp(oc->name, output->name) == 0 ||
				strcmp(oc->name, name) == 0) {
			return oc;
		}
	}

	return NULL;
}

struct roots_device_config *roots_config_get_device(struct roots_config *config,
		struct wlr_input_device *device) {
	struct roots_device_config *d_config;
	wl_list_for_each(d_config, &config->devices, link) {
		if (strcmp(d_config->name, device->name) == 0) {
			return d_config;
		}
	}

	return NULL;
}

struct roots_cursor_config *roots_config_get_cursor(struct roots_config *config,
		const char *seat_name) {
	if (seat_name == NULL) {
		seat_name = ROOTS_CONFIG_DEFAULT_SEAT_NAME;
	}

	struct roots_cursor_config *cc;
	wl_list_for_each(cc, &config->cursors, link) {
		if (strcmp(cc->seat, seat_name) == 0) {
			return cc;
		}
	}

	return NULL;
}
