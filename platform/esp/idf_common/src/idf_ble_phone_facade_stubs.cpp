// IDF-only inert definitions for the arduino-layer helpers that AppPhoneFacade
// references but which are not compiled for the IDF target: GPS-over-BLE position
// (gps_service_api) and the Meshtastic MQTT-proxy bridge (MtAdapter). Both surfaces
// are optional for the Meshtastic/MeshCore BLE phone sync, so provide no-op
// definitions so AppPhoneFacade links into the IDF C6 BLE pump. Replace with real
// wiring if GPS-to-phone position or MQTT-proxy-over-BLE are ever wanted on IDF.
//
// (mt_adapter.cpp is not built for IDF, so these two MtAdapter methods are the only
// undefined MtAdapter symbols; defining them here fills the gap without ODR conflict.)
#include "gps/domain/location_fix.h"
#include "gps/ports/i_location_source.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"

namespace gps
{
namespace
{
class NullLocationSource final : public ILocationSource
{
  public:
    bool latestFix(LocationFix& out) const override
    {
        (void)out;
        return false;
    }
};

NullLocationSource g_null_location_source;
} // namespace

const ILocationSource& gps_location_source()
{
    return g_null_location_source;
}

bool gps_is_enabled()
{
    return false;
}

bool gps_is_powered()
{
    return false;
}
} // namespace gps

namespace chat::meshtastic
{
bool MtAdapter::handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg)
{
    (void)msg;
    return false;
}

bool MtAdapter::pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out)
{
    (void)out;
    return false;
}
} // namespace chat::meshtastic
