// Portable `show_toast` free-function provider for the pure ESP-IDF build.
//
// The node_info per-node detail screen (modules/ui_shared/.../node_info/
// node_info_page_components.cpp) shows transient notices via a free
// `show_toast(const char*, uint32_t)`. In the Arduino/PlatformIO build that
// symbol is owned by the GPS screen (platform/esp/arduino_common/.../gps/
// gps_page_components.cpp), which is always compiled there. The minimal IDF
// contacts scope binds node_info WITHOUT the GPS screen, so the symbol is
// supplied here with the same real implementation the Linux production build
// uses (platform/linux/common/src/ui/gps_shared_compat.cpp): forward to the
// shared SystemNotification widget, which is already compiled into the IDF
// image (ui/widgets/system_notification.cpp).

#include "ui/widgets/system_notification.h"

#include <cstdint>

void show_toast(const char* message, uint32_t duration_ms)
{
    ::ui::SystemNotification::show(message ? message : "", duration_ms);
}
