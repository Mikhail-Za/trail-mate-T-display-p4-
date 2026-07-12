#pragma once

#include <cstdint>
#include <vector>

namespace platform::ui::wifi
{

constexpr std::size_t kMaxSsidLength = 32;
constexpr std::size_t kMaxPasswordLength = 64;
constexpr std::size_t kMaxIpLength = 47;
constexpr std::size_t kMaxStatusMessageLength = 95;

enum class ConnectionState : uint8_t
{
    Unsupported = 0,
    Disabled,
    Idle,
    Scanning,
    Connecting,
    Connected,
    Error,
};

struct Config
{
    bool enabled = false;
    char ssid[kMaxSsidLength + 1] = {};
    char password[kMaxPasswordLength + 1] = {};
};

struct Status
{
    bool supported = false;
    bool enabled = false;
    bool connected = false;
    bool scanning = false;
    bool has_credentials = false;
    int rssi = -127;
    char ssid[kMaxSsidLength + 1] = {};
    char ip[kMaxIpLength + 1] = {};
    char message[kMaxStatusMessageLength + 1] = {};
    ConnectionState state = ConnectionState::Unsupported;
};

struct ScanResult
{
    char ssid[kMaxSsidLength + 1] = {};
    int rssi = -127;
    bool requires_password = true;
};

// A remembered network. The password itself is never surfaced through the list.
struct SavedNetwork
{
    char ssid[kMaxSsidLength + 1] = {};
    bool has_password = false;
    bool auto_join = true;
};

bool is_supported();
bool load_config(Config& out);
bool save_config(const Config& config);
bool apply_enabled(bool enabled);
bool connect(const Config* override_config = nullptr);
void disconnect();
bool scan(std::vector<ScanResult>& out_results);
// Read the most recent completed scan WITHOUT starting a new one (so a refresh
// timer can pick up async results without re-triggering scans).
bool get_scan_results(std::vector<ScanResult>& out_results);
Status status();

// Saved-networks store (persisted, versioned). connect_saved joins using the
// stored password; a network connected with a freshly typed password is only
// committed to the store once it authenticates successfully.
std::size_t list_saved(std::vector<SavedNetwork>& out);
bool save_network(const char* ssid, const char* password, bool auto_join);
bool forget_network(const char* ssid);
bool set_auto_join(const char* ssid, bool auto_join);
bool is_saved(const char* ssid);
bool connect_saved(const char* ssid);

} // namespace platform::ui::wifi
