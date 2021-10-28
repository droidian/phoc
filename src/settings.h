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

struct roots_config *roots_config_create(const char *config_path);
void roots_config_destroy(struct roots_config *config);
struct roots_output_config *roots_config_get_output(struct roots_config *config,
	struct wlr_output *output);

#endif
