#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include "platform/ui/wifi_runtime.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "nvs.h"

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
    uint16_t scan_op = 0;        // op_id of the in-flight scan; stale ScanDone ignored
    uint32_t scan_generation = 0; // bumped on each accepted ScanDone (content may differ)
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

// ---- Saved networks: one versioned NVS blob, written transactionally ----
constexpr const char* kSavedNvsNs = "wifi_saved";
constexpr const char* kSavedNvsKey = "nets";
constexpr uint16_t kSavedSchemaVersion = 1;
constexpr int kMaxSaved = 8;

struct SavedRecord
{
    char ssid[33];
    char password[65];
    uint8_t auto_join;
    uint8_t pad[3];
};
struct SavedBlob
{
    uint16_t version;
    uint16_t count;
    SavedRecord records[kMaxSaved];
};

SavedBlob g_saved{};
bool g_saved_loaded = false;

// A credential typed at connect time, held transient until it authenticates so a
// typo never overwrites a known-good saved password.
struct PendingSave
{
    bool active = false;
    char ssid[33] = {};
    char password[65] = {};
    bool auto_join = true;
};
PendingSave g_pending;

bool persist_saved_blob();

void load_saved_blob()
{
    if (g_saved_loaded)
    {
        return;
    }
    g_saved_loaded = true;
    g_saved = SavedBlob{};
    g_saved.version = kSavedSchemaVersion;
    bool found = false;
#if defined(ESP_PLATFORM)
    nvs_handle_t handle = 0;
    if (nvs_open(kSavedNvsNs, NVS_READONLY, &handle) == ESP_OK)
    {
        SavedBlob tmp{};
        size_t len = sizeof(tmp);
        if (nvs_get_blob(handle, kSavedNvsKey, &tmp, &len) == ESP_OK &&
            len == sizeof(SavedBlob) && tmp.version == kSavedSchemaVersion)
        {
            g_saved = tmp;
            if (g_saved.count > kMaxSaved)
            {
                g_saved.count = kMaxSaved;
            }
            found = true;
        }
        nvs_close(handle);
    }
#endif
    // Only on first run (no valid blob yet): migrate the legacy single credential,
    // then persist so a later "forget everything" is NOT undone on the next boot
    // (a deliberately-empty blob must be distinguishable from "never saved").
    if (!found)
    {
        std::string ssid;
        if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiSsidKey, ssid) &&
            !ssid.empty())
        {
            std::string pw;
            (void)::platform::ui::settings_store::get_string(kSettingsNs, kWifiPasswordKey, pw);
            SavedRecord& r = g_saved.records[0];
            r = SavedRecord{};
            copy_text(r.ssid, sizeof(r.ssid), ssid.c_str());
            copy_text(r.password, sizeof(r.password), pw.c_str());
            r.auto_join = 1;
            g_saved.count = 1;
        }
        (void)persist_saved_blob(); // create the blob so future loads see it exists
    }
}

bool persist_saved_blob()
{
#if defined(ESP_PLATFORM)
    nvs_handle_t handle = 0;
    if (nvs_open(kSavedNvsNs, NVS_READWRITE, &handle) != ESP_OK)
    {
        return false;
    }
    const esp_err_t rc = nvs_set_blob(handle, kSavedNvsKey, &g_saved, sizeof(g_saved));
    if (rc == ESP_OK)
    {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
    return rc == ESP_OK;
#else
    return false;
#endif
}

int find_saved(const char* ssid)
{
    if (ssid == nullptr || ssid[0] == '\0')
    {
        return -1;
    }
    for (uint16_t i = 0; i < g_saved.count && i < kMaxSaved; ++i)
    {
        if (std::strncmp(g_saved.records[i].ssid, ssid, sizeof(g_saved.records[i].ssid)) == 0)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool store_saved(const char* ssid, const char* password, bool auto_join)
{
    if (ssid == nullptr || ssid[0] == '\0')
    {
        return false;
    }
    load_saved_blob();
    int idx = find_saved(ssid);
    if (idx < 0)
    {
        if (g_saved.count >= kMaxSaved)
        {
            for (int i = 1; i < kMaxSaved; ++i) // evict oldest (index 0)
            {
                g_saved.records[i - 1] = g_saved.records[i];
            }
            g_saved.count = kMaxSaved - 1;
        }
        idx = g_saved.count++;
        g_saved.records[idx] = SavedRecord{};
    }
    SavedRecord& r = g_saved.records[idx];
    copy_text(r.ssid, sizeof(r.ssid), ssid);
    if (password != nullptr)
    {
        copy_text(r.password, sizeof(r.password), password);
    }
    r.auto_join = auto_join ? 1 : 0;
    return persist_saved_blob();
}

// Auto-join the strongest visible saved+auto-join network, single-flight: only
// when idle and not user-disabled. Called after a scan completes.
void try_auto_join()
{
    namespace uiw = platform::ui::wifi;
    if (g_cache.state == uiw::ConnectionState::Connected ||
        g_cache.state == uiw::ConnectionState::Connecting ||
        g_cache.state == uiw::ConnectionState::Disabled)
    {
        return;
    }
    if (g_cache.intent == Intent::UserDisconnect || g_cache.intent == Intent::RadioDisable)
    {
        return;
    }
    load_saved_blob();
    int best = -1;
    int best_rssi = -128;
    for (uint8_t i = 0; i < g_cache.result_count && i < 6; ++i)
    {
        const int idx = find_saved(g_cache.results[i].ssid);
        if (idx >= 0 && g_saved.records[idx].auto_join != 0 &&
            g_cache.results[i].rssi > best_rssi)
        {
            best = idx;
            best_rssi = g_cache.results[i].rssi;
        }
    }
    if (best >= 0)
    {
        uiw::Config cfg{};
        cfg.enabled = true;
        copy_text(cfg.ssid, sizeof(cfg.ssid), g_saved.records[best].ssid);
        copy_text(cfg.password, sizeof(cfg.password), g_saved.records[best].password);
        (void)uiw::connect(&cfg);
    }
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
        ++g_cache.scan_generation; // content changed even if the count did not
        try_auto_join(); // join the strongest saved auto-join AP, if idle
        break;
    case WifiEventKind::StaConnected:
        // Reject a connect response from a superseded attempt.
        if (event.op_id != 0 && g_cache.conn_op != 0 && event.op_id != g_cache.conn_op)
        {
            break;
        }
        g_cache.connected = true;
        g_cache.intent = Intent::None;
        copy_text(g_cache.ssid, sizeof(g_cache.ssid), event.ssid);
        if (g_cache.state != uiw::ConnectionState::Connected)
        {
            g_cache.state = uiw::ConnectionState::Connecting; // associated, await IP
        }
        break;
    case WifiEventKind::StaGotIp:
        // Reject an IP from a superseded attempt (would show/commit the wrong AP).
        if (event.op_id != 0 && g_cache.conn_op != 0 && event.op_id != g_cache.conn_op)
        {
            break;
        }
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
        // Auth succeeded -> commit the transient credential (op already matched
        // above; also require the SSID to match so we never persist the wrong one).
        if (g_pending.active &&
            (event.ssid[0] == '\0' ||
             std::strncmp(g_pending.ssid, event.ssid, sizeof(g_pending.ssid)) == 0))
        {
            store_saved(g_pending.ssid, g_pending.password, g_pending.auto_join);
            g_pending.active = false;
        }
        break;
    case WifiEventKind::StaDisconnected:
        // Ignore a disconnect for a DIFFERENT AP than the one we are on/attempting
        // (a stale drop from a superseded attempt during rapid A->B switching --
        // the C6 stamps events with the latest op, so SSID is the reliable key).
        // Radio-disable disconnects may not carry a matching SSID, so skip then.
        if (g_cache.intent != Intent::RadioDisable && event.ssid[0] != '\0' &&
            std::strncmp(event.ssid, g_cache.ssid, sizeof(event.ssid)) != 0 &&
            std::strncmp(event.ssid, g_cache.attempt_ssid, sizeof(event.ssid)) != 0)
        {
            break;
        }
        g_cache.connected = false;
        g_cache.has_ip = false;
        g_cache.ip[0] = '\0';
        if (g_cache.state == uiw::ConnectionState::Disabled)
        {
            // Already off: a second (synthetic + real) disconnect must not flip to Idle.
            g_cache.intent = Intent::None;
            break;
        }
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
            g_pending.active = false; // don't persist a credential that failed auth
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
    else
    {
        // Clear a stale radio-disable intent so a disable-era disconnect that is
        // still queued cannot put us back into Disabled after a quick re-enable.
        if (g_cache.intent == Intent::RadioDisable)
        {
            g_cache.intent = Intent::None;
        }
        if (g_cache.state == ConnectionState::Disabled)
        {
            g_cache.state = ConnectionState::Idle; // leave the "off" state on re-enable
        }
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
    // Hold the credential transient; committed to the saved store only on auth
    // success (StaGotIp), discarded on failure.
    g_pending.active = true;
    copy_text(g_pending.ssid, sizeof(g_pending.ssid), config.ssid);
    copy_text(g_pending.password, sizeof(g_pending.password), config.password);
    g_pending.auto_join = true;
    const bool sent = wc::c6_companion().sendWifiControl(wc::WifiCommand::Connect, config.ssid,
                                                         config.password, 0, g_cache.conn_op);
    if (!sent)
    {
        // Transport failed -> roll back so we do not sit at "Connecting..." forever
        // or let a late unrelated event commit this pending credential.
        g_pending.active = false;
        g_cache.intent = Intent::None;
        g_cache.state = ConnectionState::Error;
    }
    return sent;
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
    if (!wc::c6_companion().sendWifiControl(wc::WifiCommand::Scan, nullptr, nullptr, 0,
                                            g_cache.scan_op))
    {
        g_cache.scan_in_progress = false; // don't spin at "Scanning..." if send failed
        g_cache.state = g_cache.connected ? ConnectionState::Connected : ConnectionState::Idle;
    }
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

bool get_scan_results(std::vector<ScanResult>& out_results)
{
    out_results.clear();
    if (!companion_wifi_supported())
    {
        return false;
    }
    for (uint8_t i = 0; i < g_cache.result_count && i < 6; ++i)
    {
        ScanResult r{};
        copy_text(r.ssid, sizeof(r.ssid), g_cache.results[i].ssid);
        r.rssi = g_cache.results[i].rssi;
        r.requires_password = g_cache.results[i].authmode != 0;
        if (r.ssid[0] != '\0')
        {
            out_results.push_back(r);
        }
    }
    return !out_results.empty();
}

uint32_t get_scan_generation()
{
    return g_cache.scan_generation;
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

    // Reflect the user's actual Wi-Fi on/off setting (persisted wifi_enabled), not a
    // hardcoded true. Otherwise the top-bar Wi-Fi icon (ui_status keys off
    // status().enabled) stays lit even after Wi-Fi is disabled in settings. config
    // was just loaded from the settings store via load_config() above.
    out.enabled = config.enabled;
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

std::size_t list_saved(std::vector<SavedNetwork>& out)
{
    out.clear();
    load_saved_blob();
    for (uint16_t i = 0; i < g_saved.count && i < kMaxSaved; ++i)
    {
        SavedNetwork n{};
        copy_text(n.ssid, sizeof(n.ssid), g_saved.records[i].ssid);
        n.has_password = g_saved.records[i].password[0] != '\0';
        n.auto_join = g_saved.records[i].auto_join != 0;
        out.push_back(n);
    }
    return out.size();
}

bool save_network(const char* ssid, const char* password, bool auto_join)
{
    return store_saved(ssid, password, auto_join);
}

bool forget_network(const char* ssid)
{
    load_saved_blob();
    const int idx = find_saved(ssid);
    if (idx < 0)
    {
        return false;
    }
    const bool is_current = std::strncmp(g_cache.ssid, ssid, sizeof(g_cache.ssid)) == 0;
    for (int i = idx + 1; i < static_cast<int>(g_saved.count); ++i)
    {
        g_saved.records[i - 1] = g_saved.records[i];
    }
    --g_saved.count;
    g_saved.records[g_saved.count] = SavedRecord{};
    if (is_current && g_cache.connected)
    {
        g_cache.intent = Intent::Forget;
        (void)wc::c6_companion().sendWifiControl(wc::WifiCommand::Disconnect, nullptr, nullptr,
                                                 0, next_op());
    }
    return persist_saved_blob();
}

bool set_auto_join(const char* ssid, bool auto_join)
{
    load_saved_blob();
    const int idx = find_saved(ssid);
    if (idx < 0)
    {
        return false;
    }
    g_saved.records[idx].auto_join = auto_join ? 1 : 0;
    return persist_saved_blob();
}

bool is_saved(const char* ssid)
{
    load_saved_blob();
    return find_saved(ssid) >= 0;
}

bool connect_saved(const char* ssid)
{
    load_saved_blob();
    const int idx = find_saved(ssid);
    if (idx < 0)
    {
        return false;
    }
    Config cfg{};
    cfg.enabled = true;
    copy_text(cfg.ssid, sizeof(cfg.ssid), g_saved.records[idx].ssid);
    copy_text(cfg.password, sizeof(cfg.password), g_saved.records[idx].password);
    return connect(&cfg);
}

} // namespace platform::ui::wifi

#else

#include "platform/esp/common/wifi_runtime_impl.h"

#endif
