#pragma once

#include "keybindings.h"

#include <xf86drmMode.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_output_layout.h>

G_BEGIN_DECLS

#define PHOC_CONFIG_DEFAULT_SEAT_NAME "seat0"

typedef struct _PhocOutputModeConfig {
	drmModeModeInfo info;
	struct wl_list link;
} PhocOutputModeConfig;

typedef struct _PhocOutputConfig {
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
} PhocOutputConfig;

typedef struct _PhocConfig {
	bool xwayland;
	bool xwayland_lazy;

	PhocKeybindings *keybindings;

	struct wl_list outputs;

	char *config_path;
} PhocConfig;

PhocConfig       *phoc_config_create (const char *config_path);
void              phoc_config_destroy (PhocConfig *config);
PhocOutputConfig *phoc_config_get_output (PhocConfig *config,
                                          struct wlr_output *output);

G_END_DECLS
