#ifndef ROOTSTON_CONFIG_H
#define ROOTSTON_CONFIG_H

#include "keybindings.h"

#include <xf86drmMode.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_output_layout.h>

#define ROOTS_CONFIG_DEFAULT_SEAT_NAME "seat0"

struct roots_output_mode_config {
	drmModeModeInfo info;
	struct wl_list link;
};

struct roots_output_config {
	char *name;
	bool enable;
	enum wl_output_transform transform;
	int x, y;
	float scale;
	struct wl_list link;
	struct {
		int width, height;
		float refresh_rate;
	} mode;
	struct wl_list modes;
};

struct roots_config {
	bool xwayland;
	bool xwayland_lazy;

	PhocKeybindings *keybindings;

	struct wl_list outputs;

	char *config_path;
};

/**
 * Create a roots config from the given arguments.
 */
struct roots_config *roots_config_create(const char *config_path);

/**
 * Destroy the config and free its resources.
 */
void roots_config_destroy(struct roots_config *config);

/**
 * Get configuration for the output. If the output is not configured, returns
 * NULL.
 */
struct roots_output_config *roots_config_get_output(struct roots_config *config,
	struct wlr_output *output);

/**
 * Get configuration for the keyboard. If the keyboard is not configured,
 * returns NULL. A NULL device returns the default config for keyboards.
 */
struct roots_keyboard_config *roots_config_get_keyboard(
	struct roots_config *config, struct wlr_input_device *device);

/**
 * Get configuration for the cursor. If the cursor is not configured, returns
 * NULL. A NULL seat_name returns the default config for cursors.
 */
struct roots_cursor_config *roots_config_get_cursor(struct roots_config *config,
	const char *seat_name);

#endif
