#define G_LOG_DOMAIN "phoc-tablet"

#include "config.h"

#include "input-device.h"
#include "tablet.h"


G_DEFINE_TYPE (PhocTablet, phoc_tablet, PHOC_TYPE_INPUT_DEVICE);

static void
phoc_tablet_class_init (PhocTabletClass *klass)
{
}


static void
phoc_tablet_init (PhocTablet *self)
{
}


PhocTablet *
phoc_tablet_new (struct wlr_input_device *device, PhocSeat *seat)
{
  return g_object_new (PHOC_TYPE_TABLET,
                       "device", device,
                       "seat", seat,
                       NULL);
}
