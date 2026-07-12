#include "platform/ui/ble_runtime.h" // needed by both branches (Status type)

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include <cstdio>

#include "app/app_facade_access.h"
#include "ble/ble_manager.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/ui/settings_store.h"

namespace wc = platform::esp::idf_common::wireless_companion;

namespace
{
constexpr const char* kNs = "settings";
constexpr const char* kEnabledKey = "ble_enabled";
constexpr const char* kMtKey = "ble_prof_mt";
constexpr const char* kMcKey = "ble_prof_mc";
constexpr const char* kTmKey = "ble_prof_tm";

bool companion_present()
{
    return wc::c6_companion().status().present;
}

bool get_pairing(::ble::BlePairingStatus& out)
{
    if (auto* manager = app::runtimeFacade().getBleManager())
    {
        return manager->getPairingStatus(&out);
    }
    return false;
}
} // namespace

namespace platform::ui::ble
{

bool is_supported()
{
    return companion_present();
}

Status status()
{
    Status s{};
    s.supported = companion_present();
    if (!s.supported)
    {
        s.state = LinkState::Unsupported;
        std::snprintf(s.message, sizeof(s.message), "Bluetooth companion (C6) not detected");
        return s;
    }
    s.enabled = ::platform::ui::settings_store::get_bool(kNs, kEnabledKey, true);
    s.meshtastic = ::platform::ui::settings_store::get_bool(kNs, kMtKey, true);
    s.meshcore = ::platform::ui::settings_store::get_bool(kNs, kMcKey, true);
    s.trailmate = ::platform::ui::settings_store::get_bool(kNs, kTmKey, true);
    s.passkey = wc::c6_companion().status().ble_passkey;

    ::ble::BlePairingStatus pairing{};
    const bool have_link = get_pairing(pairing);
    if (have_link)
    {
        s.connected = pairing.is_connected;
        s.pairing = pairing.is_pairing_active;
        if (pairing.passkey != 0)
        {
            s.passkey = pairing.passkey;
        }
    }

    if (!s.enabled)
    {
        s.state = LinkState::Off;
        std::snprintf(s.message, sizeof(s.message), "Bluetooth off");
    }
    else if (s.connected)
    {
        s.state = LinkState::Connected;
        std::snprintf(s.message, sizeof(s.message), "Phone connected");
    }
    else if (s.pairing)
    {
        s.state = LinkState::Pairing;
        std::snprintf(s.message, sizeof(s.message), "Pairing... enter %06lu",
                      static_cast<unsigned long>(s.passkey % 1000000u));
    }
    else
    {
        s.state = LinkState::Advertising;
        std::snprintf(s.message, sizeof(s.message),
                      have_link ? "Discoverable - not connected" : "Bluetooth on");
    }
    return s;
}

bool set_enabled(bool enabled)
{
    ::platform::ui::settings_store::put_bool(kNs, kEnabledKey, enabled);
    if (!companion_present())
    {
        return false;
    }
    return wc::c6_companion().setBleEnabled(enabled);
}

bool set_profiles(bool meshtastic, bool meshcore, bool trailmate)
{
    ::platform::ui::settings_store::put_bool(kNs, kMtKey, meshtastic);
    ::platform::ui::settings_store::put_bool(kNs, kMcKey, meshcore);
    ::platform::ui::settings_store::put_bool(kNs, kTmKey, trailmate);
    if (!companion_present())
    {
        return false;
    }
    return wc::c6_companion().setBleProfiles(meshtastic, meshcore, trailmate);
}

bool disconnect()
{
    return companion_present() && wc::c6_companion().bleDisconnect();
}

uint32_t rotate_pin()
{
    return companion_present() ? wc::c6_companion().rotateBlePin() : 0;
}

void apply_persisted()
{
    if (!companion_present())
    {
        return;
    }
    const bool enabled = ::platform::ui::settings_store::get_bool(kNs, kEnabledKey, true);
    const bool mt = ::platform::ui::settings_store::get_bool(kNs, kMtKey, true);
    const bool mc = ::platform::ui::settings_store::get_bool(kNs, kMcKey, true);
    const bool tm = ::platform::ui::settings_store::get_bool(kNs, kTmKey, true);
    (void)wc::c6_companion().setBleProfiles(mt, mc, tm);
    (void)wc::c6_companion().setBleEnabled(enabled);
}

} // namespace platform::ui::ble

#else

namespace platform::ui::ble
{
bool is_supported() { return false; }
Status status()
{
    Status s{};
    return s;
}
bool set_enabled(bool) { return false; }
bool set_profiles(bool, bool, bool) { return false; }
bool disconnect() { return false; }
uint32_t rotate_pin() { return 0; }
void apply_persisted() {}
} // namespace platform::ui::ble

#endif
