#define G_LOG_DOMAIN "phoc-touch"

#include "config.h"

#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/libinput.h>

#include "input-device.h"
#include "touch.h"
#include "seat.h"

/**
 * PhocTouch:
 *
 * A touch input device
 */
struct _PhocTouch {
  PhocInputDevice                parent;
};

G_DEFINE_TYPE (PhocTouch, phoc_touch, PHOC_TYPE_INPUT_DEVICE);

static void
phoc_touch_class_init (PhocTouchClass *klass)
{
}


static void
phoc_touch_init (PhocTouch *self)
{
}


PhocTouch *
phoc_touch_new (struct wlr_input_device *device, PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_TOUCH,
                       "device", device,
                       "seat", seat,
                       NULL);
}
