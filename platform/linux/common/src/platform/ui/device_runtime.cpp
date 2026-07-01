#include "platform/ui/device_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#include "platform/linux/env_config.h"
#include "platform/linux/runtime_paths.h"

namespace platform::ui::device
{
namespace
{

using namespace ::platform::linux_runtime;

constexpr const char* kFirmwareVersionEnv = "TRAIL_MATE_FIRMWARE_VERSION";
constexpr const char* kBatteryLevelEnv = "TRAIL_MATE_BATTERY_LEVEL";
constexpr const char* kBatteryChargingEnv = "TRAIL_MATE_BATTERY_CHARGING";
constexpr const char* kSdRootEnv = "TRAIL_MATE_SD_ROOT";
constexpr const char* kSettingsRootEnv = "TRAIL_MATE_SETTINGS_ROOT";
constexpr const char* kGpsSupportedEnv = "TRAIL_MATE_GPS_SUPPORTED";
constexpr const char* kGpsReadyEnv = "TRAIL_MATE_GPS_READY";
constexpr const char* kPowerTierEnv = "TRAIL_MATE_POWER_TIER";

uint8_t s_screen_brightness = 0;
uint8_t s_message_tone_volume = 45;

bool path_exists_from_env(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(value), ec);
}

bool ensure_storage_root()
{
    const auto paths = ::platform::linux_runtime::resolve_paths();
    return ::platform::linux_runtime::ensure_directory(paths.sd_root);
}

std::string trim_copy(std::string value)
{
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' ||
            value.back() == ' ' || value.back() == '\t'))
    {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() &&
           (value[start] == ' ' || value[start] == '\t'))
    {
        ++start;
    }
    return start == 0 ? value : value.substr(start);
}

std::optional<std::string> read_text_file(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        return std::nullopt;
    }

    std::string value;
    std::getline(stream, value);
    return trim_copy(value);
}

std::optional<int> read_int_file(const std::filesystem::path& path)
{
    const auto text = read_text_file(path);
    if (!text)
    {
        return std::nullopt;
    }

    char* end = nullptr;
    const long parsed = std::strtol(text->c_str(), &end, 10);
    if (end == text->c_str())
    {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

bool status_is_charging(const std::string& status)
{
    return status == "Charging" ||
           status == "Full" ||
           status == "Not charging";
}

std::optional<BatteryInfo> read_linux_power_supply_battery()
{
#if defined(__linux__)
    const std::filesystem::path root("/sys/class/power_supply");
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec)
    {
        return std::nullopt;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
        {
            break;
        }
        const std::filesystem::path dir = entry.path();
        const auto type = read_text_file(dir / "type");
        if (type && *type != "Battery")
        {
            continue;
        }

        const auto capacity = read_int_file(dir / "capacity");
        if (!capacity)
        {
            continue;
        }

        BatteryInfo info{};
        info.available = true;
        info.level = std::clamp(*capacity, 0, 100);
        if (const auto status = read_text_file(dir / "status"))
        {
            info.charging = status_is_charging(*status);
        }
        return info;
    }
#endif
    return std::nullopt;
}

std::optional<bool> read_linux_external_power_online()
{
#if defined(__linux__)
    const std::filesystem::path root("/sys/class/power_supply");
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec)
    {
        return std::nullopt;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
        {
            break;
        }
        const std::filesystem::path dir = entry.path();
        const auto type = read_text_file(dir / "type");
        if (!type || *type == "Battery")
        {
            continue;
        }
        const auto online = read_int_file(dir / "online");
        if (online)
        {
            return *online > 0;
        }
    }
#endif
    return std::nullopt;
}

#if defined(__linux__)
bool read_meminfo_kb(const char* label, std::size_t* out_kb)
{
    if (!label || !out_kb)
    {
        return false;
    }

    std::ifstream stream("/proc/meminfo");
    if (!stream.is_open())
    {
        return false;
    }

    std::string key;
    std::size_t value_kb = 0;
    std::string unit;
    while (stream >> key >> value_kb >> unit)
    {
        if (!key.empty() && key.back() == ':')
        {
            key.pop_back();
        }
        if (key == label)
        {
            *out_kb = value_kb;
            return true;
        }
    }
    return false;
}
#endif

} // namespace

void delay_ms(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void restart()
{
    std::fflush(nullptr);
    std::exit(0);
}

bool rtc_ready()
{
    return true;
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
    const bool level_env_set = std::getenv(kBatteryLevelEnv) != nullptr;
    if (level_env_set)
    {
        const int level = platform::linux_runtime::env_int(kBatteryLevelEnv, -1);
        info.available = level >= 0;
        info.level = level;
        info.charging = platform::linux_runtime::env_flag(kBatteryChargingEnv);
        return info;
    }

    if (const auto battery = read_linux_power_supply_battery())
    {
        return *battery;
    }

    info.available = false;
    info.level = -1;
    info.charging = read_linux_external_power_online().value_or(
        platform::linux_runtime::env_flag(kBatteryChargingEnv));
    return info;
}

MemoryStats memory_stats()
{
    MemoryStats stats{};

#if defined(__linux__)
    std::size_t mem_total_kb = 0;
    std::size_t mem_available_kb = 0;
    if (read_meminfo_kb("MemTotal", &mem_total_kb))
    {
        stats.ram_total_bytes = mem_total_kb * 1024U;
    }
    if (read_meminfo_kb("MemAvailable", &mem_available_kb))
    {
        stats.ram_free_bytes = mem_available_kb * 1024U;
    }
#endif

    stats.psram_total_bytes = 0;
    stats.psram_free_bytes = 0;
    stats.psram_available = false;
    return stats;
}

const char* firmware_version()
{
    const char* configured = std::getenv(kFirmwareVersionEnv);
    return (configured && configured[0] != '\0') ? configured : "linux-dev";
}

void handle_low_battery(const BatteryInfo&)
{
}

bool supports_screen_brightness()
{
    return false;
}

bool supports_configurable_battery_gauge()
{
    return false;
}

void reload_configurable_battery_gauge()
{
}

uint8_t screen_brightness()
{
    return s_screen_brightness;
}

void set_screen_brightness(uint8_t level)
{
    s_screen_brightness = level;
}

void trigger_haptic()
{
}

uint8_t default_message_tone_volume()
{
    return s_message_tone_volume;
}

void set_message_tone_volume(uint8_t volume_percent)
{
    s_message_tone_volume = volume_percent;
}

void play_message_tone()
{
}

bool sd_ready()
{
    return path_exists_from_env(kSdRootEnv) || ensure_storage_root();
}

bool card_ready()
{
    return sd_ready();
}

bool gps_ready()
{
    return platform::linux_runtime::env_flag(kGpsReadyEnv, true);
}

bool gps_supported()
{
    return platform::linux_runtime::env_flag(kGpsSupportedEnv, true);
}

int power_tier()
{
    return platform::linux_runtime::env_int(kPowerTierEnv, 0);
}

uint8_t touch_points(int32_t out_x[2], int32_t out_y[2])
{
    // The desktop sims have no multi-touch source; pinch stays inert there.
    (void)out_x;
    (void)out_y;
    return 0;
}

} // namespace platform::ui::device
