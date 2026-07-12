#pragma once

#include <cstdint>

// UI-facing seam for managing the device's own BLE peripheral (the phone connects
// to us). Mirrors platform/ui/wifi_runtime.h. On T-Display-P4 / Tab5 this is
// backed by the ESP32-C6 companion; other targets get an unsupported stub. This
// is NOT BLE central -- there is no scanning for / connecting out to other
// devices.
namespace platform::ui::ble
{

constexpr std::size_t kMessageLength = 95;

enum class LinkState : uint8_t
{
    Unsupported = 0,
    Off,          // radio disabled
    Advertising,  // on, no phone connected
    Pairing,      // a phone is entering the PIN
    Connected,    // a phone is connected/paired
};

struct Status
{
    bool supported = false;
    bool enabled = false;     // BLE radio on
    bool connected = false;   // a phone is connected
    bool pairing = false;     // pairing in progress
    uint32_t passkey = 0;     // current 6-digit pairing PIN
    bool meshtastic = true;   // per-profile advertise enables
    bool meshcore = true;
    bool trailmate = true;
    LinkState state = LinkState::Unsupported;
    char message[kMessageLength + 1] = {};
};

bool is_supported();
Status status();
bool set_enabled(bool enabled);
bool set_profiles(bool meshtastic, bool meshcore, bool trailmate);
bool disconnect();               // drop the connected phone
uint32_t rotate_pin();           // new random PIN + reset bonds; returns the PIN
// Push the persisted enable/profile prefs to the C6 (call once after handshake).
void apply_persisted();

} // namespace platform::ui::ble
