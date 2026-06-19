#include "ui/app_registry.h"

#include "platform/ui/wireless_companion_runtime.h"
#include "ui/app_catalog.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/localization.h"
#include "ui/page/page_host.h"
#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/contacts/contacts_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_shell.h"
#include "ui/screens/settings/settings_page_shell.h"
#include "ui/ui_theme.h"

#include <cstdio>

namespace
{

extern "C"
{
    extern const lv_image_dsc_t Setting;
    extern const lv_image_dsc_t Chat;
}

struct CompanionPageState
{
    lv_obj_t* root = nullptr;
};

CompanionPageState s_companion_page_state;

void add_label(lv_obj_t* parent,
               const char* text,
               const lv_font_t* font,
               lv_color_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

void add_status_line(lv_obj_t* parent, const char* label, const char* value)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: %s", label ? label : "", value ? value : "");
    add_label(parent, buf, &lv_font_montserrat_14, ui::theme::text());
}

void add_u32_line(lv_obj_t* parent, const char* label, unsigned long value)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: %lu", label ? label : "", value);
    add_label(parent, buf, &lv_font_montserrat_14, ui::theme::text());
}

void add_hex_line(lv_obj_t* parent, const char* label, uint32_t value)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(value));
    add_status_line(parent, label, buf);
}

void companion_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<CompanionPageState*>(user_data);
    if (!state || !parent || (state->root && lv_obj_is_valid(state->root)))
    {
        return;
    }

    state->root = lv_obj_create(parent);
    lv_obj_set_size(state->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(state->root, ui::theme::white(), 0);
    lv_obj_set_style_bg_opa(state->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->root, 0, 0);
    lv_obj_set_style_radius(state->root, 0, 0);
    lv_obj_set_style_pad_left(state->root, 18, 0);
    lv_obj_set_style_pad_right(state->root, 18, 0);
    lv_obj_set_style_pad_top(state->root, 18, 0);
    lv_obj_set_style_pad_bottom(state->root, 18, 0);
    lv_obj_set_flex_flow(state->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->root, 8, 0);

    // Back button: the C6 page previously had no way out (forced a reboot).
    // Route to the launcher menu via the same exit the Chat app uses.
    lv_obj_t* back_btn = lv_button_create(state->root);
    lv_obj_set_width(back_btn, LV_PCT(45));
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    add_label(state->root, ::ui::i18n::tr("C6 Companion"), &lv_font_montserrat_14, ui::theme::text());

    const auto st = platform::ui::wireless_companion::status();
    add_status_line(state->root, "State", platform::ui::wireless_companion::state_name(st.state));
    add_status_line(state->root, "Message", st.message);
    add_status_line(state->root, "Detail", st.detail);
    add_status_line(state->root, "Board capable", st.board_capable ? "yes" : "no");
    add_status_line(state->root, "Started", st.started ? "yes" : "no");
    add_status_line(state->root, "Present", st.present ? "yes" : "no");
    add_u32_line(state->root, "Protocol min", st.protocol_min);
    add_u32_line(state->root, "Protocol max", st.protocol_max);
    add_u32_line(state->root, "Selected protocol", st.selected_protocol);

    add_hex_line(state->root, "Supported features", st.supported_features);
    add_hex_line(state->root, "Enabled features", st.enabled_features);
    add_u32_line(state->root, "Config seq", st.config_seq);
    add_u32_line(state->root, "Config error", st.config_error);
    add_u32_line(state->root, "Selected MTU", st.selected_mtu);
    add_status_line(state->root,
                    "BLE",
                    platform::ui::wireless_companion::service_state_name(st.ble_state));
    add_status_line(state->root,
                    "ESP-NOW",
                    platform::ui::wireless_companion::service_state_name(st.espnow_state));
    add_status_line(state->root,
                    "Wi-Fi",
                    platform::ui::wireless_companion::service_state_name(st.wifi_state));

    add_u32_line(state->root, "Firmware", st.firmware_version);
    add_u32_line(state->root, "Free heap", st.free_heap);
}

void companion_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<CompanionPageState*>(user_data);
    if (!state || !state->root || !lv_obj_is_valid(state->root))
    {
        if (state)
        {
            state->root = nullptr;
        }
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
}

ui::CallbackAppScreen s_companion_app("c6_companion",
                                      "C6 Companion",
                                      &Setting,
                                      companion_enter,
                                      companion_exit,
                                      &s_companion_page_state);

// Chat shell entry. Mirrors modules/ui_shared/src/ui/app_catalog_builder.cpp:
// the chat page shell's enter/exit take a ui::page::Host* as user_data, and the
// menu host routes the page's back/exit request to ui_request_exit_to_menu().
void request_menu_exit(void*)
{
    ::ui_request_exit_to_menu();
}

ui::page::Host make_menu_host()
{
    ui::page::Host host{};
    host.request_exit = request_menu_exit;
    return host;
}

ui::page::Host s_chat_menu_host = make_menu_host();

ui::CallbackAppScreen s_chat_app("chat",
                                 "Chat",
                                 &Chat,
                                 chat::ui::shell::enter,
                                 chat::ui::shell::exit,
                                 &s_chat_menu_host);

// Contacts (mesh node list). Mirrors the chat binding: the contacts page shell's
// enter/exit take a ui::page::Host* as user_data, and the page routes its back
// request through ui_request_exit_to_menu() via the menu host. Backing data
// (ContactService + node store) is provided live by IdfChatFacade.
ui::page::Host s_contacts_menu_host = make_menu_host();

ui::CallbackAppScreen s_contacts_app("contacts",
                                     "Contacts",
                                     &Chat,
                                     contacts::ui::shell::enter,
                                     contacts::ui::shell::exit,
                                     &s_contacts_menu_host);

// Settings (device/radio/channel/GPS config editor + broadcast-nodeinfo action).
// Mirrors the chat/contacts binding: the settings page shell's enter/exit take a
// ui::page::Host* as user_data and route the back request through
// ui_request_exit_to_menu() via the menu host. The editor reads/writes live config
// through app::configFacade().getConfig() (provided by IdfChatFacade) and the
// platform::ui::* runtime services (time/device/tracker/wifi/gps/screen/
// firmware_update/settings_backup/settings_store/team_ui_store/wireless_companion).
ui::page::Host s_settings_menu_host = make_menu_host();

ui::CallbackAppScreen s_settings_app("settings",
                                     "Settings",
                                     &Setting,
                                     settings::ui::shell::enter,
                                     settings::ui::shell::exit,
                                     &s_settings_menu_host);

// Sky-plot / GNSS satellite view (sky-plot of satellites in view + per-satellite
// signal table). stable_id = 'sky_plot'. Mirrors the chat/contacts/settings
// binding: the gnss page shell's enter/exit take a ui::page::Host* (gnss::ui::
// shell::Host is an alias of ::ui::page::Host) as user_data and route the back
// request through ui_request_exit_to_menu() via the menu host. The shell falls
// back to a placeholder when device::gps_supported() is false; on this board it is
// true, so the live runtime reads platform::ui::gps::get_gnss_snapshot() (already a
// compiled producer). Self-contained: no map/SD dependency.
ui::page::Host s_skyplot_menu_host = make_menu_host();

ui::CallbackAppScreen s_skyplot_app("sky_plot",
                                    "Satellites",
                                    &Setting,
                                    gnss::ui::shell::enter,
                                    gnss::ui::shell::exit,
                                    &s_skyplot_menu_host);

AppScreen* s_apps[] = {
    &s_chat_app, &s_contacts_app, &s_settings_app, &s_skyplot_app, &s_companion_app};
ui::StaticAppCatalogState s_catalog_state = ui::makeStaticAppCatalogState(s_apps);
ui::AppCatalog s_catalog = ui::makeStaticAppCatalog(&s_catalog_state);

} // namespace

namespace ui
{

AppCatalog appCatalog()
{
    return s_catalog;
}

} // namespace ui
