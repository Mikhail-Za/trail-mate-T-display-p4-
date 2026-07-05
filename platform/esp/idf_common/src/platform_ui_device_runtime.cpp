#include "platform/ui/device_runtime.h"

#include <cstring>
#include <ctime>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "boards/tab5/tab5_board.h"
#include "bsp/trail_mate_t_display_p4_runtime.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/common/build_info.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/gps_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "bsp/m5stack_tab5.h"
#endif

namespace platform::ui::device
{

namespace
{

uint8_t s_brightness_level = DEVICE_MAX_BRIGHTNESS_LEVEL;
constexpr ::time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC

bool is_valid_epoch(::time_t value)
{
    return value >= kMinValidEpochSeconds;
}

} // namespace

void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void restart()
{
    esp_restart();
}

bool rtc_ready()
{
    return is_valid_epoch(std::time(nullptr));
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    info.charging = bsp_usb_c_detect();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
    info.available = true;
    info.charging = board.isCharging();
    info.level = board.getBatteryLevel();
#endif
    return info;
}

BatteryPowerDetail battery_power_detail()
{
    BatteryPowerDetail detail{};
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    // Tab5 exposes no numeric fuel-gauge voltage/current here; leave valid=false and
    // current_valid=false (struct defaults).
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    auto& b = ::boards::t_display_p4::TDisplayP4Board::instance();
    detail.voltage_mv = b.getBatteryVoltageMv();
    bool cok = false;
    detail.current_ma = b.getBatteryCurrentMa(&cok);
    detail.current_valid = cok;
    detail.valid = (detail.voltage_mv >= 0);
#endif
    return detail;
}

MemoryStats memory_stats()
{
    MemoryStats stats{};
    stats.ram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    stats.ram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    stats.psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    stats.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    stats.psram_available = stats.psram_total_bytes > 0;
    return stats;
}

const char* firmware_version()
{
    const char* configured = ::platform::esp::common::build_info::firmwareVersion();
    if (configured && configured[0] != '\0' && std::strcmp(configured, "unknown") != 0)
    {
        return configured;
    }

    const esp_app_desc_t* desc = esp_app_get_description();
    return (desc && desc->version[0] != '\0') ? desc->version : "unknown";
}

void handle_low_battery(const BatteryInfo& info)
{
    (void)info;
}

bool supports_configurable_battery_gauge()
{
    // The configurable-gauge path exists only for boards with a user-writable
    // fuel-gauge capacity register exposed through prefs (e.g. the T-Lora-Pager
    // BQ27220 path in the Arduino runtime). The T-Display-P4 / Tab5 IDF targets
    // read level straight from the board, so there is no configurable gauge here.
    return false;
}

void reload_configurable_battery_gauge()
{
    // No configurable gauge on the IDF targets (see supports_* above): nothing to
    // reload from prefs.
}

bool supports_screen_brightness()
{
    return true;
}

uint8_t screen_brightness()
{
    return s_brightness_level;
}

void set_screen_brightness(uint8_t level)
{
    s_brightness_level = level;
    const int percent = (DEVICE_MAX_BRIGHTNESS_LEVEL <= 0)
                            ? 100
                            : static_cast<int>((static_cast<uint32_t>(level) * 100U) /
                                               static_cast<uint32_t>(DEVICE_MAX_BRIGHTNESS_LEVEL));
    (void)platform::esp::idf_common::bsp_runtime::set_display_brightness(percent);
}

void trigger_haptic() {}

uint8_t default_message_tone_volume()
{
    return 45;
}

void set_message_tone_volume(uint8_t volume_percent)
{
    (void)volume_percent;
}

void play_message_tone() {}

bool sd_ready()
{
    return platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
}

bool card_ready()
{
    return sd_ready();
}

bool gps_ready()
{
    return platform::esp::idf_common::gps_runtime::is_enabled() ||
           platform::esp::idf_common::gps_runtime::is_powered();
}

bool gps_supported()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasGpsUart();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::hasGpsUart();
#else
    return false;
#endif
}

int power_tier()
{
    const BatteryInfo info = battery_info();
    if (!info.available || info.level < 0)
    {
        return 0;
    }
    return info.level <= 15 ? 1 : 0;
}

uint8_t touch_points(int32_t out_x[2], int32_t out_y[2])
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return trail_mate_t_display_p4_touch_points(out_x, out_y);
#else
    // No multi-touch runtime wired on this board; pinch degrades to single-touch.
    (void)out_x;
    (void)out_y;
    return 0;
#endif
}

void set_multitouch_enabled(bool enabled)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    trail_mate_t_display_p4_set_multitouch(enabled);
#else
    (void)enabled;
#endif
}

} // namespace platform::ui::device
