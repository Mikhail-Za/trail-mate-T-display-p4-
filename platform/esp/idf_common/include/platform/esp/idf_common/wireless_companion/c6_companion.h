#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::idf_common::wireless_companion
{

enum class BleBackend : uint8_t
{
    None = 0,
    Local = 1,
    C6Companion = 2,
};

// BLE profile served by the C6, mapped 1:1 onto the HostLink BLE channels
// (Meshtastic / MeshCore / Trail-Mate private). Values match tm_c6_ble_profile.
enum class BleProfile : uint8_t
{
    Meshtastic = 1,
    MeshCore = 2,
    TrailMate = 3,
};

enum class CompanionState : uint8_t
{
    Unsupported = 0,
    NotStarted = 1,
    Missing = 2,
    TransportPending = 3,
    Present = 4,
    Error = 5,
};

// Wi-Fi station/AP commands, values match tm_c6_wifi_command in c6_protocol.h.
enum class WifiCommand : uint8_t
{
    Scan = 1,
    Connect = 2,
    Disconnect = 3,
    GetIp = 4,
    ApStart = 5,
    ApStop = 6,
};

// Wi-Fi event kinds reported UP from the C6, values match tm_c6_wifi_event_kind.
enum class WifiEventKind : uint8_t
{
    Started = 1,
    Stopped = 2,
    ScanDone = 3,
    StaConnected = 4,
    StaDisconnected = 5,
    StaGotIp = 6,
    ApStarted = 7,
    ApStopped = 8,
    Error = 9,
};

// One scan result. ssid is NUL-terminated (max 32 chars + NUL).
struct WifiScanEntry
{
    char ssid[33] = {};
    int8_t rssi = 0;
    uint8_t channel = 0;
    uint8_t authmode = 0;
};

// A decoded Wi-Fi event. `results` is valid for ScanDone (result_count entries);
// `ipv4` (network byte order) and `ssid` are valid for StaGotIp / StaConnected.
struct WifiEventInfo
{
    WifiEventKind kind = WifiEventKind::Error;
    uint16_t error_code = 0;
    uint16_t op_id = 0; // echoes the control op_id that caused this event (0 = none)
    uint32_t ipv4 = 0;
    char ssid[33] = {};
    uint8_t result_count = 0;
    WifiScanEntry results[6] = {};
};

struct C6CompanionStatus
{
    bool board_capable = false;
    bool started = false;
    bool present = false;
    CompanionState state = CompanionState::Unsupported;
    uint16_t protocol_min = 1;
    uint16_t protocol_max = 1;
    uint16_t selected_protocol = 0;
    uint32_t supported_features = 0;
    uint32_t firmware_version = 0;
    uint32_t free_heap = 0;
    uint32_t enabled_features = 0;
    uint32_t config_seq = 0;
    uint16_t config_error = 0;
    uint16_t selected_mtu = 0;
    uint8_t ble_state = 0;
    uint8_t espnow_state = 0;
    uint8_t wifi_state = 0;
    uint32_t ble_passkey = 0; // 6-digit fixed BLE pairing PIN the P4 configured
    uint32_t ping_nonce = 0;
    uint32_t ping_count = 0;
    uint32_t pong_count = 0;
    const char* detail = "unsupported";
};

// Sink for data + events travelling UP from the C6 to the P4 (BLE GATT writes,
// BLE connection events, received ESP-NOW frames). Delivered synchronously from
// WirelessCompanion::poll(). All methods default to no-ops so a consumer can
// override only the surfaces it cares about. The pointers are valid only for the
// duration of the call; copy anything you need to keep. Not owned by the companion.
class WirelessUplinkSink
{
  public:
    virtual void onBleUplink(BleProfile profile, uint8_t connection_id,
                             const uint8_t* data, size_t len)
    {
        (void)profile;
        (void)connection_id;
        (void)data;
        (void)len;
    }
    virtual void onBleEvent(BleProfile profile, uint8_t event_kind,
                            uint8_t connection_id, uint16_t mtu, uint16_t error_code)
    {
        (void)profile;
        (void)event_kind;
        (void)connection_id;
        (void)mtu;
        (void)error_code;
    }
    virtual void onEspNowReceive(const uint8_t mac[6], int8_t rssi,
                                 const uint8_t* data, size_t len)
    {
        (void)mac;
        (void)rssi;
        (void)data;
        (void)len;
    }
    virtual void onWifiEvent(const WifiEventInfo& event)
    {
        (void)event;
    }
    virtual ~WirelessUplinkSink() = default;
};

class WirelessCompanion
{
  public:
    virtual bool begin() = 0;
    virtual bool isPresent() const = 0;
    virtual uint32_t capabilities() const = 0;
    virtual C6CompanionStatus status() const = 0;
    virtual void poll() = 0;

    // Data channel, P4 -> C6. Returns false if the companion is not present or the
    // send failed. `data`/`len` is the raw GATT / ESP-NOW payload; the companion
    // adds the HostLink framing. ESP-NOW payloads are capped at the protocol max.
    virtual bool sendBleDownlink(BleProfile profile, uint8_t connection_id,
                                 const uint8_t* data, size_t len) = 0;
    virtual bool sendEspNow(const uint8_t mac[6], const uint8_t* data, size_t len) = 0;

    // Wi-Fi control, P4 -> C6. `ssid`/`password` may be nullptr for commands that
    // do not need them (Scan/Disconnect/GetIp). `op_id` is echoed on the resulting
    // events so the caller can correlate async replies (0 = don't-care). Returns
    // false if not present or the send failed. The C6 answers asynchronously via
    // WirelessUplinkSink::onWifiEvent.
    virtual bool sendWifiControl(WifiCommand command, const char* ssid,
                                 const char* password, uint8_t channel,
                                 uint16_t op_id = 0) = 0;

    // Register (or clear, with nullptr) the sink that receives uplink data/events.
    // The sink is not owned and must outlive the companion or be cleared first.
    virtual void setUplinkSink(WirelessUplinkSink* sink) = 0;

    virtual ~WirelessCompanion() = default;
};

WirelessCompanion& c6_companion();
bool ensure_c6_companion_started();
C6CompanionStatus get_c6_companion_status();
const char* companion_state_name(CompanionState state);

} // namespace platform::esp::idf_common::wireless_companion
