#include "esp32_lvgl_runtime_config.h"

#include "product_composition/target_profile.h"

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

namespace trailmate::apps::esp32_lvgl
{

const Esp32LvglRuntimeConfig& esp32LvglRuntimeConfig()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    static const Esp32LvglRuntimeConfig kConfig = {
        "tab5",
        "trail-mate-tab5",
        "Tab5",
        "tab5_app_loop",
        10,
        4096,
        5,
    };
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PANEL_RM69A10) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4_AMOLED)
    static const Esp32LvglRuntimeConfig kConfig = {
        "tdisplayp4_amoled",
        "trail-mate-t-display-p4-amoled",
        "T-Display-P4 AMOLED",
        "t_display_p4_amoled_app_loop",
        10,
        4096,
        5,
    };
#else
    static const Esp32LvglRuntimeConfig kConfig = {
        "tdisplayp4_tft",
        "trail-mate-t-display-p4-tft",
        "T-Display-P4 TFT",
        "t_display_p4_tft_app_loop",
        10,
        // The MeshCore facade pump (IdfChatFacade::pumpMeshAndDrainEvents ->
        // MeshCoreAdapter::processSendQueue) runs on THIS app-loop task. When the
        // SX1262 has gone dark in continuous RX, the first self-advert's TX path
        // runs the full dead-chip revive (Sx126xRadio::startTransmit ->
        // reestablish_lora_locked -> reset_chip_locked -> init_locked +
        // configure_lora_locked) INLINE on this stack. That re-init sequence was
        // designed to run from main_task (a large stack); at the default 4096
        // bytes it overflows the app-loop stack and trips the canary
        // ('Stack protection fault' in task 't_display_p4_tf'), reboot-looping.
        // Give the task enough headroom to absorb the inline revive on top of the
        // LVGL/loop-shell/facade frames already resident. Measured worst-case via
        // uxTaskGetStackHighWaterMark during the advert revive fits well inside
        // this; 16 KB leaves a comfortable margin.
        16384,
        5,
    };
#endif
#else
    static const Esp32LvglRuntimeConfig kConfig = {
        "esp_idf",
        "trail-mate-idf",
        "esp-idf",
        "idf_app_loop",
        10,
        4096,
        5,
    };
#endif
    return kConfig;
}

const product_composition::TargetProfile* esp32LvglRuntimeTargetProfile()
{
    return product_composition::findTargetProfile(esp32LvglRuntimeConfig().target_id);
}

bool hasEsp32LvglRuntimeTargetProfile()
{
    return esp32LvglRuntimeTargetProfile() != nullptr;
}

} // namespace trailmate::apps::esp32_lvgl
