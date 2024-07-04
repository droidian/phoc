#define G_LOG_DOMAIN "phoc-settings"

#include "phoc-config.h"

#include <stdio.h>
#include <strings.h>
#include <sys/param.h>

#include "phoc-enums.h"
#include "settings.h"
#include "utils.h"


static bool
parse_boolean (const char *s, bool default_)
{
  g_return_val_if_fail (s, default_);

  if (strcasecmp (s, "true") == 0)
    return true;

  if (strcasecmp (s, "false") == 0)
    return false;

  g_critical ("got invalid output enable value: %s", s);
  return default_;
}


static bool
parse_modeline (const char *s, drmModeModeInfo *mode)
{
  char hsync[16];
  char vsync[16];
  float fclock;

  mode->type = DRM_MODE_TYPE_USERDEF;

  if (sscanf (s, "%f %hd %hd %hd %hd %hd %hd %hd %hd %15s %15s",
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
  if (strcasecmp (hsync, "+hsync") == 0) {
    mode->flags |= DRM_MODE_FLAG_PHSYNC;
  } else if (strcasecmp (hsync, "-hsync") == 0) {
    mode->flags |= DRM_MODE_FLAG_NHSYNC;
  } else {
    return false;
  }

  if (strcasecmp (vsync, "+vsync") == 0) {
    mode->flags |= DRM_MODE_FLAG_PVSYNC;
  } else if (strcasecmp (vsync, "-vsync") == 0) {
    mode->flags |= DRM_MODE_FLAG_NVSYNC;
  } else {
    return false;
  }

  snprintf (mode->name, sizeof(mode->name), "%dx%d@%d",
            mode->hdisplay, mode->vdisplay, mode->vrefresh / 1000);

  return true;
}


static
PhocOutputScaleFilter
parse_scale_filter (const char *value)
{
  GEnumValue *ev;
  g_autoptr (GEnumClass) eclass = NULL;

  eclass = G_ENUM_CLASS (g_type_class_ref (phoc_output_scale_filter_get_type ()));
  ev = g_enum_get_value_by_nick (eclass, value);
  if (!ev) {
    g_critical ("Got invalid output scale-filter value: %s", value);
    return PHOC_OUTPUT_SCALE_FILTER_AUTO;
  }

  return ev->value;
}


static const char *output_prefix = "output:";

static PhocOutputConfig *
phoc_output_config_new (const char *name)
{
  PhocOutputConfig *oc;

  oc = g_new0 (PhocOutputConfig, 1);
  oc->name = g_strdup (name);
  oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
  oc->scale = 0;
  oc->enable = true;
  oc->x = -1;
  oc->y = -1;
  oc->scale_filter = PHOC_OUTPUT_SCALE_FILTER_AUTO;
  oc->drm_panel_orientation = false;

  return oc;
}


static void
phoc_output_config_destroy (PhocOutputConfig *oc)
{
  g_slist_free_full (oc->modes, g_free);
  g_free (oc->name);
  g_free (oc);
}


static int
config_ini_handler (PhocConfig *config, const char *section, const char *name, const char *value)
{
  if (strcmp (section, "core") == 0) {
    if (strcmp (name, "xwayland") == 0) {
      if (strcasecmp (value, "true") == 0) {
        config->xwayland = true;
      } else if (strcasecmp (value, "immediate") == 0) {
        config->xwayland = true;
        config->xwayland_lazy = false;
      } else if (strcasecmp (value, "false") == 0) {
        config->xwayland = false;
      } else {
        g_critical ("got unknown xwayland value: %s", value);
      }
    } else {
      g_critical ("got unknown core config: %s", name);
    }
  } else if (strncmp (output_prefix, section, strlen (output_prefix)) == 0) {
    const char *output_name = section + strlen (output_prefix);
    PhocOutputConfig *oc = NULL;

    for (GSList *l = config->outputs; l; l = l->next) {
      PhocOutputConfig *tmp = l->data;

      if (g_str_equal (tmp->name, output_name)) {
        oc = tmp;
        break;
      }
    }

    if (oc == NULL) {
      oc = phoc_output_config_new (output_name);
      config->outputs = g_slist_prepend (config->outputs, oc);
    }

    if (strcmp (name, "enable") == 0) {
      oc->enable = parse_boolean (value, oc->enable);
    } else if (strcmp (name, "x") == 0) {
      oc->x = strtol (value, NULL, 10);
    } else if (strcmp (name, "y") == 0) {
      oc->y = strtol (value, NULL, 10);
    } else if (strcmp (name, "scale") == 0) {
      if (strcmp (value, "auto") == 0) {
        oc->scale = 0;
      } else {
        oc->scale = strtof (value, NULL);
        g_assert (oc->scale > 0);
      }
    } else if (strcmp (name, "rotate") == 0) {
      if (strcmp (value, "normal") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
      } else if (strcmp (value, "90") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_90;
      } else if (strcmp (value, "180") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_180;
      } else if (strcmp (value, "270") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_270;
      } else if (strcmp (value, "flipped") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED;
      } else if (strcmp (value, "flipped-90") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
      } else if (strcmp (value, "flipped-180") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
      } else if (strcmp (value, "flipped-270") == 0) {
        oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
      } else {
        g_critical ("got unknown transform value: %s", value);
      }
      /* Make sure we rotate clockwise */
      phoc_utils_fix_transform (&oc->transform);
    } else if (strcmp (name, "mode") == 0) {
      char *end;
      oc->mode.width = strtol (value, &end, 10);
      g_assert (*end == 'x');
      ++end;
      oc->mode.height = strtol (end, &end, 10);
      if (*end) {
        g_assert (*end == '@');
        ++end;
        oc->mode.refresh_rate = strtof (end, &end);
        g_assert (strcmp ("Hz", end) == 0);
      }
      g_debug ("Parsed mode %dx%d@%f for output %s",
               oc->mode.width, oc->mode.height,
               oc->mode.refresh_rate, oc->name);
    } else if (strcmp (name, "modeline") == 0) {
      g_autofree PhocOutputModeConfig *mode = g_new0 (PhocOutputModeConfig, 1);

      if (parse_modeline (value, &mode->info))
        oc->modes = g_slist_prepend (oc->modes, g_steal_pointer (&mode));
      else
        g_critical ("Invalid modeline: %s", value);
    } else if (strcmp (name, "scale-filter") == 0) {
      oc->scale_filter = parse_scale_filter (value);
    } else if (strcmp (name, "drm-panel-orientation") == 0) {
      oc->drm_panel_orientation = parse_boolean (value, true);
    } else if (g_str_equal (name, "phys_width")) {
      oc->phys_width = strtol (value, NULL, 10);
    } else if (g_str_equal (name, "phys_height")) {
      oc->phys_height = strtol (value, NULL, 10);
    } else {
      g_warning ("Unknown key '%s' in section '%s'", name, section);
    }
  } else {
    g_critical ("Found unknown config section: %s", section);
  }

  return 1;
}

static PhocConfig *
phoc_config_new_from_keyfile (GKeyFile *keyfile)
{
  g_autoptr (GError) err = NULL;
  g_auto (GStrv) sections = NULL;
  PhocConfig *config = g_new0 (PhocConfig, 1);

  config->xwayland = true;
  config->xwayland_lazy = true;
  config->keybindings = phoc_keybindings_new ();

  sections = g_key_file_get_groups (keyfile, NULL);
  for (int i = 0; i < g_strv_length (sections); i++) {
    const char *section = sections[i];
    g_auto (GStrv) keys = g_key_file_get_keys (keyfile, section, NULL, &err);

    if (!keys) {
      g_critical ("Failed to get keys for %s: %s", section, err->message);
      g_clear_error (&err);
      continue;
    }

    for (int j = 0; j < g_strv_length (keys); j++) {
      const char *key = keys[j];
      g_autofree char *value = NULL;

      value = g_key_file_get_value (keyfile, section, key, &err);
      if (value == NULL) {
        g_critical ("Failed to key value for %s.%s: %s", section, key, err->message);
        g_clear_error (&err);
        continue;
      }
      g_strstrip (value);

      config_ini_handler (config, section, key, value);
    }
  }

  return config;
}

/**
 * phoc_config_new_from_file: (skip)
 * @config_path: (nullable): The config file location
 *
 * Parse the file at the given location into a configuration.
 *
 * Returns: The parsed configuration
 */
PhocConfig *
phoc_config_new_from_file (const char *config_path)
{
  PhocConfig *config;
  g_autoptr (GError) err = NULL;
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  g_autofree char *path = g_strdup (config_path);

  if (path == NULL) {
    // get the config path from the current directory
    char cwd[MAXPATHLEN];
    if (getcwd (cwd, sizeof(cwd)) != NULL) {
      path = g_build_path ("/", cwd, "phoc.ini", NULL);
    } else {
      g_critical ("could not get cwd");
      return NULL;
    }
  }

  if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, &err)) {
    if (g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      g_debug ("No config file found. Using sensible defaults.");
      goto out;
    }

    g_critical ("Failed to parse config %s: %s", path, err->message);
    return NULL;
  }

 out:
  config = phoc_config_new_from_keyfile (keyfile);
  config->config_path = g_steal_pointer (&path);

  return config;
}


/**
 * phoc_config_new_from_data: (skip)
 * @data: (nullable): The config data
 *
 * Parse the given config data
 *
 * Returns: The parsed configuration
 */
PhocConfig *
phoc_config_new_from_data (const char *data)
{
  PhocConfig *config;
  g_autoptr (GError) err = NULL;
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();

  if (!g_key_file_load_from_data (keyfile, data, -1, G_KEY_FILE_NONE, &err)) {
    g_critical ("Failed to parse config data from memory: %s", err->message);
    return NULL;
  }

  config = phoc_config_new_from_keyfile (keyfile);
  return config;
}


/**
 * phoc_config_destroy:
 * config: The #PhocConfig.
 *
 * Destroy the config and free its resources.
 */
void
phoc_config_destroy (PhocConfig *config)
{
  g_slist_free_full (config->outputs, (GDestroyNotify)phoc_output_config_destroy);
  g_object_unref (config->keybindings);

  g_free (config->config_path);
  g_free (config);
}


static gboolean
output_is_match (PhocOutputConfig *oc, PhocOutput *output)
{
  g_auto (GStrv) vmm = NULL;

  if (g_strcmp0 (oc->name, phoc_output_get_name (output)) == 0)
    return TRUE;

  /* "make%model%serial" match */
  vmm = g_strsplit (oc->name, "%", 4);
  if (g_strv_length (vmm) != 3)
    return FALSE;

  if (!(g_strcmp0 (vmm[0], "*") == 0 || g_strcmp0 (vmm[0], output->wlr_output->make) == 0))
    return FALSE;

  if (!(g_strcmp0 (vmm[1], "*") == 0 || g_strcmp0 (vmm[1], output->wlr_output->model) == 0))
    return FALSE;

  if (!(g_strcmp0 (vmm[2], "*") == 0 || g_strcmp0 (vmm[2], output->wlr_output->serial) == 0))
    return FALSE;

  return TRUE;
}

/**
 * phoc_config_get_output: (skip)
 * config: The #PhocConfig
 * output: The output to get the configuration for
 *
 * Get intended configuration for the given output.
 *
 * Returns: The intended output configuration or %NULL or not
 *     configuration is found.
 */
PhocOutputConfig *
phoc_config_get_output (PhocConfig *config, PhocOutput *output)
{
  g_assert (PHOC_IS_OUTPUT (output));

  for (GSList *l = config->outputs; l; l = l->next) {
    PhocOutputConfig *oc = l->data;

    if (output_is_match (oc, output))
      return oc;
  }

  return NULL;
}
