#include "esp32_lvgl_startup_runtime.h"

#include "esp32_lvgl_idf_app_runtime_access.h"
#include "esp32_lvgl_loop_runtime.h"

#if defined(ESP_PLATFORM)
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "board/BoardBase.h"
#include "esp_log.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/debug/sd_coredump_export.h"
#include "platform/esp/idf_common/idf_chat_facade.h"
#include "platform/esp/idf_common/startup_support.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"
#include "ui/app_registry.h"
#include "ui/app_runtime.h"
#include "ui/startup_shell.h"
#endif

namespace trailmate::apps::esp32_lvgl
{
namespace
{

#if defined(ESP_PLATFORM)
bool lockUi(uint32_t timeout_ms)
{
    return platform::esp::boards::lockDisplay(timeout_ms);
}

void unlockUi()
{
    platform::esp::boards::unlockDisplay();
}

void applyPlatformRuntimeConfig(const Esp32LvglRuntimeConfig& config)
{
    if (!app::hasAppFacade())
    {
        return;
    }

    const app::AppConfig& app_config = app::appFacade().getConfig();
    platform::ui::gps::set_enabled(app_config.gps_enabled);
    platform::ui::gps::set_collection_interval(app_config.gps_interval_ms);
    platform::ui::gps::set_power_strategy(app_config.gps_strategy);
    platform::ui::gps::set_gnss_config(app_config.gps_mode, app_config.gps_sat_mask);
    platform::ui::gps::set_external_nmea_config(app_config.external_nmea_output_hz,
                                                app_config.external_nmea_sentence_mask);
    platform::ui::gps::set_motion_idle_timeout(app_config.motion_config.idle_timeout_ms);
    platform::ui::gps::set_motion_sensor_id(app_config.motion_config.sensor_id);
    ESP_LOGI(config.log_tag,
             "GNSS runtime config applied: enabled=%d interval_ms=%lu mode=%u sat_mask=0x%02X strategy=%u external_nmea=%u/%u motion_idle_ms=%lu sensor=%u startup_probe=deferred",
             app_config.gps_enabled ? 1 : 0,
             static_cast<unsigned long>(app_config.gps_interval_ms),
             app_config.gps_mode,
             app_config.gps_sat_mask,
             app_config.gps_strategy,
             app_config.external_nmea_output_hz,
             app_config.external_nmea_sentence_mask,
             static_cast<unsigned long>(app_config.motion_config.idle_timeout_ms),
             app_config.motion_config.sensor_id);
}

ui::startup_shell::Hooks buildShellHooks()
{
    ui::startup_shell::Hooks hooks{};
    hooks.messaging = app::hasAppFacade() ? &app::messagingFacade() : nullptr;
    hooks.apps = ui::appCatalog();
    hooks.show_main_menu = menu_show;
    hooks.watch_face = ui::startup_shell::defaultWatchFaceHooks();
    hooks.set_max_brightness = []()
    {
        const int saved = platform::ui::settings_store::get_int("settings", "screen_brightness",
                                                                DEVICE_MAX_BRIGHTNESS_LEVEL);
        const int clamped =
            saved < DEVICE_MIN_BRIGHTNESS_LEVEL
                ? DEVICE_MIN_BRIGHTNESS_LEVEL
                : (saved > DEVICE_MAX_BRIGHTNESS_LEVEL ? DEVICE_MAX_BRIGHTNESS_LEVEL : saved);
        (void)platform::esp::idf_common::bsp_runtime::wake_display();
        platform::ui::device::set_screen_brightness(static_cast<uint8_t>(clamped));
    };
    return hooks;
}
#endif

} // namespace

#if defined(ESP_PLATFORM)
extern "C" void trail_mate_idf_note_user_activity(void)
{
    platform::ui::screen::update_user_activity();
}
#endif

bool canRunEsp32LvglStartupRuntime(const Esp32LvglRuntimeConfig& config)
{
    return canStartEsp32LvglLoopRuntime(config);
}

void runEsp32LvglStartupRuntime(const Esp32LvglRuntimeConfig& config)
{
    if (!canRunEsp32LvglStartupRuntime(config))
    {
        return;
    }

#if defined(ESP_PLATFORM)
    constexpr bool waking_from_sleep = false;

    platform::esp::idf_common::startup_support::logStartupBanner(config.log_tag);
    (void)platform::esp::idf_common::bsp_runtime::ensure_nvs_ready();
    platform::esp::boards::initializeBoard(waking_from_sleep);
    platform::esp::boards::initializeDisplay();
    if (platform::esp::boards::syncSystemTimeFromBoardRtc())
    {
        ESP_LOGI(config.log_tag, "Boot time restored from hardware RTC");
    }
    (void)platform::esp::idf_common::wireless_companion::ensure_c6_companion_started();
    (void)platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
    (void)platform::esp::idf_common::debug::export_previous_coredump_to_sd();

    ESP_LOGI(config.log_tag, "prepareBootUi begin waking=%d", waking_from_sleep ? 1 : 0);
    if (lockUi(1000))
    {
        ui::startup_shell::prepareBootUi(waking_from_sleep);
        unlockUi();
        ESP_LOGI(config.log_tag, "prepareBootUi complete");
    }
    else
    {
        ESP_LOGW(config.log_tag, "prepareBootUi failed to acquire LVGL lock");
    }

    // Bind the minimal LoRa-chat app facade BEFORE idf_app_runtime_access::initialize.
    // That accessor latches app_context_bound from app::hasAppFacade(), and the
    // startup shell's messaging hook reads app::messagingFacade(), so the facade
    // must be live first. Board init above has resolved the LoraBoard, so the
    // factory can build the radio adapter now. The facade is a function-local
    // static: the loop task never returns, so this gives it process lifetime and a
    // single instance, and its initialize() calls app::bindAppFacade(*this).
    const platform::esp::boards::AppContextInitHandles board_handles =
        platform::esp::boards::resolveAppContextInitHandles();
    if (board_handles.lora_board != nullptr)
    {
        static platform::esp::idf_common::IdfChatFacade s_chat_facade(*board_handles.lora_board,
                                                                      board_handles.board);
        // Region: the default AppConfig seeds CN (region code 4); this is a US board, so set
        // US (1) before initialize() applies the radio config. The facade ctor explicitly
        // permits overwriting getConfig() before initialize(); region is a uint8 Meshtastic code.
        s_chat_facade.getConfig().meshtastic_config.region = 1; // US, 902-928 MHz ISM
        if (!s_chat_facade.initialize())
        {
            ESP_LOGW(config.log_tag, "LoRa-chat app facade failed to initialize for %s",
                     config.target_name);
        }
        else
        {
            ESP_LOGI(config.log_tag, "LoRa-chat app facade bound for %s", config.target_name);
        }
    }
    else
    {
        ESP_LOGW(config.log_tag,
                 "LoRa board unavailable; LoRa-chat app facade not bound for %s",
                 config.target_name);
    }

    idf_app_runtime_access::initialize(config);
    applyPlatformRuntimeConfig(config);

    const ui::startup_shell::Hooks shell_hooks = buildShellHooks();

    const size_t app_count = ui::catalogCount(shell_hooks.apps);
    ESP_LOGI(config.log_tag, "initializeShell begin app_count=%u", static_cast<unsigned>(app_count));
    if (lockUi(1000))
    {
        ui::startup_shell::initializeShell(shell_hooks);
        unlockUi();
        ESP_LOGI(config.log_tag, "initializeShell complete");
    }
    else
    {
        ESP_LOGW(config.log_tag, "initializeShell failed to acquire LVGL lock");
    }

    const auto& runtime_status = idf_app_runtime_access::status();
    ESP_LOGI(config.log_tag,
             "%s runtime status handles=%d lifecycle=%d bound=%d",
             config.target_name,
             runtime_status.board_handles_ready ? 1 : 0,
             runtime_status.lifecycle_ready ? 1 : 0,
             runtime_status.app_context_bound ? 1 : 0);

    ESP_LOGI(config.log_tag, "finalizeStartup begin waking=%d", waking_from_sleep ? 1 : 0);
    if (lockUi(5000))
    {
        ui::startup_shell::finalizeStartup(waking_from_sleep);
        unlockUi();
        ESP_LOGI(config.log_tag, "finalizeStartup complete");
    }
    else
    {
        ESP_LOGW(config.log_tag, "finalizeStartup failed to acquire LVGL lock");
    }

    startEsp32LvglLoopRuntime(config);

    ESP_LOGI(config.log_tag, "%s startup runtime initialized", config.target_name);
#else
    (void)config;
#endif
}

} // namespace trailmate::apps::esp32_lvgl
