// IDF-side definition of the `gps::gps_get_data()` free function that the
// shared (arduino_common) hostlink service references when it emits GPS events
// to a connected PC host (hostlink_service.cpp -> send_gps_event()).
//
// In the Arduino build that symbol is provided by gps/gps_service_api.cpp over
// the Arduino GPS HAL. The pure ESP-IDF build does not compile that Arduino GPS
// service: its GPS is driven by platform::esp::idf_common::gps_runtime and read
// through the platform::ui::gps facade. This translation unit bridges the two so
// the hostlink USB-CDC bridge reports the live IDF GPS fix without dragging the
// Arduino GPS stack into the IDF image. platform::ui::gps::GpsState IS
// ::gps::GpsState (a using-alias), so this is a direct, lossless forward.

#include "platform/esp/arduino_common/gps/gps_service_api.h"

#include "platform/ui/gps_runtime.h"

namespace gps
{

GpsState gps_get_data()
{
    return ::platform::ui::gps::get_data();
}

} // namespace gps
