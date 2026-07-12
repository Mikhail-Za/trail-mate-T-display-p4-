#pragma once

#include "platform/esp/idf_common/wireless_companion/c6_companion.h"

// Bridge between the C6 companion's Wi-Fi uplink events and the UI-facing Wi-Fi
// runtime (platform::ui::wifi in platform_ui_wifi_runtime.cpp). The single
// WirelessUplinkSink (the C6 BLE service) forwards every Wi-Fi event here so the
// UI runtime can cache scan results + connection status without owning the sink.
namespace platform::esp::idf_common::wireless_companion
{

// Called from the uplink sink (C6BleService::onWifiEvent) on the companion thread.
void c6_wifi_ingest_event(const WifiEventInfo& event);

} // namespace platform::esp::idf_common::wireless_companion
