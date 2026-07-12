#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include "platform/ui/wifi_runtime.h"

#include <cstdio>
#include <cstring>

#include "hostlink/c6/c6_protocol.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/esp/idf_common/wireless_companion/c6_wifi_bridge.h"
#include "platform/ui/settings_store.h"

// Wi-Fi on T-Display-P4 / Tab5 lives on the ESP32-C6 companion, reached over
// HostLink. This runtime drives it: scan/connect/disconnect become WIFI_CONTROL
// frames, and the C6's asynchronous WIFI_EVENT replies are fed back here through
// c6_wifi_ingest_event() (called by the single uplink sink). scan() issues the
// command and pumps the companion for a bounded window until the ScanDone event
// arrives -- matching the synchronous UI contract while the radio work happens on
// the C6.
namespace wc = platform::esp::idf_common::wireless_companion;

namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr const char* kUnsupportedMessage = "Wi-Fi companion (C6) not detected";

// Why the last STA_DISCONNECTED / transition happened, so an event can be
// interpreted correctly (a user-requested disconnect must not read as a failure,
// and must not trigger auto-join).
enum class Intent : uint8_t
{
    None,          // idle / unsolicited
    Connecting,    // a connect is in flight
    UserDisconnect,
    RadioDisable,
    Forget,
};

// Latest Wi-Fi state, updated from C6 events on the companion thread and read by
// the UI thread. All access is on the single UI/runtime thread (the companion is
// polled there), so no locking is required.
struct WifiCache
{
    bool scan_in_progress = false;
    bool scan_done = false;
    uint16_t scan_op = 0; // op_id of the in-flight scan; stale ScanDone is ignored
    uint8_t result_count = 0;
    wc::WifiScanEntry results[6] = {};
    bool connected = false;
    bool has_ip = false;
    char ip[16] = {};
    char ssid[33] = {};          // current/last connected SSID
    char attempt_ssid[33] = {};  // SSID of the in-flight connect
    int rssi = -127;
    uint16_t last_error = 0;
    uint16_t conn_op = 0;   // op_id of the in-flight connect/disconnect
    Intent intent = Intent::None;
    platform::ui::wifi::ConnectionState state = platform::ui::wifi::ConnectionState::Idle;
};

WifiCache g_cache;
uint16_t g_op_seq = 0; // monotonic op_id source (0 reserved for "don't care")

uint16_t next_op()
{
    if (++g_op_seq == 0)
    {
        g_op_seq = 1;
    }
    return g_op_seq;
}

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

bool companion_wifi_supported()
{
    const wc::C6CompanionStatus st = wc::c6_companion().status();
    return st.present && (st.supported_features & TM_C6_FEATURE_WIFI_STA) != 0;
}

} // namespace

namespace platform::esp::idf_common::wireless_companion
{

void c6_wifi_ingest_event(const WifiEventInfo& event)
{
    namespace uiw = platform::ui::wifi;
    switch (event.kind)
    {
    case WifiEventKind::ScanDone:
        // Ignore a stale scan (e.g. one issued before the user disabled Wi-Fi or
        // started a newer scan) so it cannot repopulate the list or drive auto-join.
        if (event.op_id != 0 && g_cache.scan_op != 0 && event.op_id != g_cache.scan_op)
        {
            break;
        }
        g_cache.result_count = event.result_count;
        for (uint8_t i = 0; i < event.result_count && i < 6; ++i)
        {
            g_cache.results[i] = event.results[i];
        }
        g_cache.scan_done = true;
        g_cache.scan_in_progress = false;
        break;
    case WifiEventKind::StaConnected:
        g_cache.connected = true;
        g_cache.intent = Intent::None;
        copy_text(g_cache.ssid, sizeof(g_cache.ssid), event.ssid);
        if (g_cache.state != uiw::ConnectionState::Connected)
        {
            g_cache.state = uiw::ConnectionState::Connecting; // associated, await IP
        }
        break;
    case WifiEventKind::StaGotIp:
        g_cache.connected = true;
        g_cache.has_ip = true;
        g_cache.intent = Intent::None;
        g_cache.last_error = 0;
        std::snprintf(g_cache.ip, sizeof(g_cache.ip), "%u.%u.%u.%u",
                      static_cast<unsigned>(event.ipv4 & 0xff),
                      static_cast<unsigned>((event.ipv4 >> 8) & 0xff),
                      static_cast<unsigned>((event.ipv4 >> 16) & 0xff),
                      static_cast<unsigned>((event.ipv4 >> 24) & 0xff));
        if (event.ssid[0] != '\0')
        {
            copy_text(g_cache.ssid, sizeof(g_cache.ssid), event.ssid);
        }
        g_cache.state = uiw::ConnectionState::Connected;
        break;
    case WifiEventKind::StaDisconnected:
        g_cache.connected = false;
        g_cache.has_ip = false;
        g_cache.ip[0] = '\0';
        if (g_cache.intent == Intent::UserDisconnect || g_cache.intent == Intent::Forget)
        {
            g_cache.state = uiw::ConnectionState::Idle; // intentional -> clean, no error
        }
        else if (g_cache.intent == Intent::RadioDisable)
        {
            g_cache.state = uiw::ConnectionState::Disabled;
        }
        else if (g_cache.state == uiw::ConnectionState::Connecting)
        {
            // Never associated -> the attempt failed (bad password / AP not found).
            g_cache.state = uiw::ConnectionState::Error;
            if (g_cache.last_error == 0)
            {
                g_cache.last_error = event.error_code;
            }
        }
        else
        {
            g_cache.state = uiw::ConnectionState::Idle; // lost an established link
        }
        g_cache.intent = Intent::None;
        break;
    case WifiEventKind::Error:
        g_cache.last_error = event.error_code;
        if (g_cache.state == uiw::ConnectionState::Connecting)
        {
            g_cache.state = uiw::ConnectionState::Error;
        }
        break;
    default:
        break;
    }
}

} // namespace platform::esp::idf_common::wireless_companion

namespace platform::ui::wifi
{

bool is_supported()
{
    return companion_wifi_supported();
}

bool load_config(Config& out)
{
    out = Config{};
    out.enabled = ::platform::ui::settings_store::get_bool(kSettingsNs, kWifiEnabledKey, false);

    std::string value;
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiSsidKey, value))
    {
        copy_text(out.ssid, sizeof(out.ssid), value.c_str());
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiPasswordKey, value))
    {
        copy_text(out.password, sizeof(out.password), value.c_str());
    }

    return true;
}

bool save_config(const Config& config)
{
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, config.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, config.enabled);
    return ssid_ok && password_ok;
}

bool apply_enabled(bool enabled)
{
    // Actually gate the C6 STA radio (was a no-op). Disabling records the intent so
    // the resulting disconnect reads as a clean radio-off rather than a failure.
    if (!companion_wifi_supported())
    {
        return false;
    }
    if (!enabled)
    {
        g_cache.intent = Intent::RadioDisable;
    }
    return wc::c6_companion().setWifiEnabled(enabled);
}

bool connect(const Config* override_config)
{
    Config config{};
    if (override_config != nullptr)
    {
        config = *override_config;
    }
    else
    {
        (void)load_config(config);
    }
    if (!companion_wifi_supported() || config.ssid[0] == '\0')
    {
        return false;
    }
    g_cache.conn_op = next_op();
    g_cache.intent = Intent::Connecting;
    g_cache.state = ConnectionState::Connecting;
    g_cache.last_error = 0;
    copy_text(g_cache.attempt_ssid, sizeof(g_cache.attempt_ssid), config.ssid);
    copy_text(g_cache.ssid, sizeof(g_cache.ssid), config.ssid);
    return wc::c6_companion().sendWifiControl(wc::WifiCommand::Connect, config.ssid,
                                              config.password, 0, g_cache.conn_op);
}

void disconnect()
{
    g_cache.conn_op = next_op();
    g_cache.intent = Intent::UserDisconnect;
    (void)wc::c6_companion().sendWifiControl(wc::WifiCommand::Disconnect, nullptr, nullptr, 0,
                                             g_cache.conn_op);
    g_cache.connected = false;
    g_cache.has_ip = false;
    g_cache.ip[0] = '\0';
    g_cache.state = ConnectionState::Idle;
}

bool scan(std::vector<ScanResult>& out_results)
{
    out_results.clear();
    if (!companion_wifi_supported())
    {
        return false;
    }
    // Kick an ASYNCHRONOUS scan and return immediately. The C6 answers ~5s later
    // with a ScanDone event that c6_wifi_ingest_event() folds into g_cache on the
    // normal runtime loop -- blocking here would freeze the UI and stall BLE
    // downlinks. The caller reflects progress via status().scanning and re-reads
    // results (this returns the most recent completed scan, empty until the first
    // ScanDone). The op_id lets a stale ScanDone be discarded.
    g_cache.scan_in_progress = true;
    g_cache.scan_op = next_op();
    if (g_cache.state != ConnectionState::Connected)
    {
        g_cache.state = ConnectionState::Scanning;
    }
    (void)wc::c6_companion().sendWifiControl(wc::WifiCommand::Scan, nullptr, nullptr, 0,
                                             g_cache.scan_op);
    for (uint8_t i = 0; i < g_cache.result_count && i < 6; ++i)
    {
        ScanResult r{};
        copy_text(r.ssid, sizeof(r.ssid), g_cache.results[i].ssid);
        r.rssi = g_cache.results[i].rssi;
        r.requires_password = g_cache.results[i].authmode != 0; // 0 == open
        if (r.ssid[0] != '\0')
        {
            out_results.push_back(r);
        }
    }
    return !out_results.empty();
}

Status status()
{
    Status out{};
    out.supported = companion_wifi_supported();
    if (!out.supported)
    {
        out.state = ConnectionState::Unsupported;
        copy_text(out.message, sizeof(out.message), kUnsupportedMessage);
        return out;
    }

    Config config{};
    (void)load_config(config);

    out.enabled = true;
    out.connected = g_cache.connected;
    out.scanning = g_cache.scan_in_progress;
    out.has_credentials = config.ssid[0] != '\0';
    out.rssi = g_cache.rssi;
    out.state = g_cache.state;
    copy_text(out.ssid, sizeof(out.ssid),
              g_cache.ssid[0] != '\0' ? g_cache.ssid : config.ssid);
    copy_text(out.ip, sizeof(out.ip), g_cache.ip);

    if (g_cache.connected && g_cache.has_ip)
    {
        std::snprintf(out.message, sizeof(out.message), "Connected: %s", out.ip);
    }
    else if (g_cache.scan_in_progress)
    {
        copy_text(out.message, sizeof(out.message), "Scanning...");
    }
    else if (g_cache.state == ConnectionState::Connecting)
    {
        std::snprintf(out.message, sizeof(out.message), "Connecting to %s...",
                      g_cache.attempt_ssid[0] != '\0' ? g_cache.attempt_ssid : out.ssid);
    }
    else if (g_cache.state == ConnectionState::Error)
    {
        std::snprintf(out.message, sizeof(out.message), "Couldn't connect to %s",
                      g_cache.attempt_ssid[0] != '\0' ? g_cache.attempt_ssid : out.ssid);
    }
    else if (g_cache.state == ConnectionState::Disabled)
    {
        copy_text(out.message, sizeof(out.message), "Wi-Fi off");
    }
    else
    {
        copy_text(out.message, sizeof(out.message), "Ready");
    }
    return out;
}

} // namespace platform::ui::wifi

#else

#include "platform/esp/common/wifi_runtime_impl.h"

#endif
