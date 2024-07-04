#pragma once

#include "keybindings.h"
#include "output.h"

#include <xf86drmMode.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output_layout.h>

G_BEGIN_DECLS

#define PHOC_CONFIG_DEFAULT_SEAT_NAME "seat0"

typedef struct _PhocOutputModeConfig {
  drmModeModeInfo info;
} PhocOutputModeConfig;

typedef struct _PhocOutputConfig {
  char                    *name;
  bool                     enable;
  enum wl_output_transform transform;
  int                      x, y;
  float                    scale;
  PhocOutputScaleFilter    scale_filter;
  bool                     drm_panel_orientation;

  struct PhocMode {
    int   width, height;
    float refresh_rate;
  } mode;
  GSList                  *modes;

  guint                    phys_width, phys_height;
} PhocOutputConfig;

typedef struct _PhocConfig {
  bool             xwayland;
  bool             xwayland_lazy;

  PhocKeybindings *keybindings;

  GSList          *outputs;

  char            *config_path;
} PhocConfig;

PhocConfig       *phoc_config_new_from_file (const char *config_path);
PhocConfig       *phoc_config_new_from_data (const char *data);
void              phoc_config_destroy       (PhocConfig *config);
PhocOutputConfig *phoc_config_get_output    (PhocConfig *config, PhocOutput *output);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PhocConfig, phoc_config_destroy)

G_END_DECLS
