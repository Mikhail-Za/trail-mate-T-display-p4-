#include "platform/ui/device_runtime.h"

#include <cstring>

#include "board/BoardBase.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/battery_guard.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/build_info.h"
#if defined(ARDUINO_T_LORA_PAGER)
#include "boards/tlora_pager/tlora_pager_board.h"
#endif

namespace platform::ui::device
{

namespace
{

constexpr uint32_t kRestartBlackoutDelayUs = 50000;

void prepare_for_restart()
{
    if (board.hasKeyboard())
    {
        board.keyboardSetBrightness(0);
    }
    board.setBrightness(0);
    esp_rom_delay_us(kRestartBlackoutDelayUs);
}

} // namespace

void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void restart()
{
    prepare_for_restart();
    esp_restart();
}

bool rtc_ready()
{
    return board.isRTCReady();
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
    info.available = true;
    info.charging = board.isCharging();
    info.level = board.getBatteryLevel();
    return info;
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

    const esp_app_desc_t* desc = esp_ota_get_app_description();
    return (desc && desc->version[0] != '\0') ? desc->version : "unknown";
}

void handle_low_battery(const BatteryInfo& info)
{
    if (!info.available || info.level < 0)
    {
        return;
    }
    platform::esp::arduino_common::handleLowBattery(info.level, info.charging);
}

bool supports_screen_brightness()
{
    return true;
}

bool supports_configurable_battery_gauge()
{
#if defined(ARDUINO_T_LORA_PAGER)
    return true;
#else
    return false;
#endif
}

void reload_configurable_battery_gauge()
{
#if defined(ARDUINO_T_LORA_PAGER)
    ::boards::tlora_pager::instance.reloadGaugeCapacityFromPrefs();
#endif
}

uint8_t screen_brightness()
{
    return board.getBrightness();
}

void set_screen_brightness(uint8_t level)
{
    board.setBrightness(level);
}

void trigger_haptic()
{
    board.vibrator();
}

uint8_t default_message_tone_volume()
{
    return board.getMessageToneVolume();
}

void set_message_tone_volume(uint8_t volume_percent)
{
    board.setMessageToneVolume(volume_percent);
}

void play_message_tone()
{
    board.playMessageTone();
}

bool sd_ready()
{
    return board.isSDReady();
}

bool card_ready()
{
    return ::platform::esp::arduino_common::storage::sd_card_ready() || board.isCardReady();
}

bool gps_ready()
{
    return board.isGPSReady();
}

bool gps_supported()
{
    return board.hasGPSHardware();
}

int power_tier()
{
    return board.getPowerTier();
}

uint8_t touch_points(int32_t out_x[2], int32_t out_y[2])
{
    // The Arduino boards' touch runtimes read a single point; report none so
    // multi-finger gestures (map pinch) stay inert and single-touch paths are
    // driven by the LVGL pointer exactly as before.
    (void)out_x;
    (void)out_y;
    return 0;
}

void set_multitouch_enabled(bool enabled)
{
    // No multi-touch runtime on the Arduino boards; nothing to toggle.
    (void)enabled;
}

} // namespace platform::ui::device
