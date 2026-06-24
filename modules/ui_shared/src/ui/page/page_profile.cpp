#include "ui/page/page_profile.h"

#include <algorithm>

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif
#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace ui::page_profile
{
namespace
{

lv_coord_t fallback_screen_width(lv_obj_t* screen)
{
    if (screen)
    {
        lv_coord_t width = lv_obj_get_width(screen);
        if (width > 0)
        {
            return width;
        }
    }

    lv_coord_t width = lv_display_get_physical_horizontal_resolution(nullptr);
    return width > 0 ? width : 320;
}

lv_coord_t fallback_screen_height(lv_obj_t* screen)
{
    if (screen)
    {
        lv_coord_t height = lv_obj_get_height(screen);
        if (height > 0)
        {
            return height;
        }
    }

    lv_coord_t height = lv_display_get_physical_vertical_resolution(nullptr);
    return height > 0 ? height : 240;
}

} // namespace

PageLayoutProfile make_pager_profile()
{
    PageLayoutProfile profile{};
    profile.name = "pager";
    profile.variant = LayoutVariant::EncoderCompact;
    profile.top_bar_height = 30;
    profile.top_content_gap = 3;
    profile.title_font = &lv_font_montserrat_16;
    profile.body_font = &lv_font_montserrat_14;
    profile.caption_font = &lv_font_montserrat_14;
    profile.tiny_font = &lv_font_montserrat_12;
    profile.filter_panel_width = 90;
    profile.filter_panel_pad_row = 2;
    profile.filter_button_height = 28;
    profile.list_panel_pad_row = 6;
    profile.list_panel_pad_left = 1;
    profile.list_panel_pad_right = 2;
    profile.list_panel_margin_bottom = 3;
    profile.list_item_height = 28;
    profile.control_button_height = 28;
    profile.control_button_min_width = 90;
    profile.compact_button_min_width = 56;
    profile.modal_min_width = 180;
    profile.modal_min_height = 132;
    profile.modal_margin = 12;
    profile.modal_pad = 8;
    profile.popup_title_height = 28;
    profile.icon_picker_button_size = 60;
    profile.ime_bar_height = 24;
    profile.ime_toggle_width = 44;
    profile.ime_toggle_height = 18;
    profile.ime_candidate_button_height = 28;
    profile.ime_keyboard_height = 0;
    profile.large_touch_hitbox = false;
    return profile;
}

PageLayoutProfile make_tdeck_profile()
{
    PageLayoutProfile profile{};
    profile.name = "tdeck";
    profile.variant = LayoutVariant::HybridCompact;
    profile.top_bar_height = 30;
    profile.top_content_gap = 3;
    profile.title_font = &lv_font_montserrat_16;
    profile.body_font = &lv_font_montserrat_14;
    profile.caption_font = &lv_font_montserrat_14;
    profile.tiny_font = &lv_font_montserrat_12;
    profile.filter_panel_width = 96;
    profile.filter_panel_pad_row = 2;
    profile.filter_button_height = 28;
    profile.list_panel_pad_row = 6;
    profile.list_panel_pad_left = 1;
    profile.list_panel_pad_right = 2;
    profile.list_panel_margin_bottom = 3;
    profile.list_item_height = 28;
    profile.control_button_height = 28;
    profile.control_button_min_width = 90;
    profile.compact_button_min_width = 56;
    profile.modal_min_width = 180;
    profile.modal_min_height = 132;
    profile.modal_margin = 12;
    profile.modal_pad = 8;
    profile.popup_title_height = 28;
    profile.icon_picker_button_size = 60;
    profile.ime_bar_height = 24;
    profile.ime_toggle_width = 44;
    profile.ime_toggle_height = 18;
    profile.ime_candidate_button_height = 28;
    profile.ime_keyboard_height = 0;
    profile.large_touch_hitbox = false;
    return profile;
}

PageLayoutProfile make_tab5_profile()
{
    PageLayoutProfile profile{};
    profile.name = "tab5";
    profile.variant = LayoutVariant::HybridTouchLarge;
    profile.top_bar_height = 64;
    profile.top_content_gap = 12;
    profile.title_font = &lv_font_montserrat_16;
    profile.body_font = &lv_font_montserrat_16;
    profile.caption_font = &lv_font_montserrat_14;
    profile.tiny_font = &lv_font_montserrat_12;
    profile.content_pad_left = 16;
    profile.content_pad_right = 16;
    profile.content_pad_top = 6;
    profile.content_pad_bottom = 12;
    profile.filter_panel_width = 196;
    profile.filter_panel_pad_row = 8;
    profile.filter_button_height = 60;
    profile.list_panel_pad_row = 14;
    profile.list_panel_pad_left = 8;
    profile.list_panel_pad_right = 8;
    profile.list_panel_margin_bottom = 10;
    profile.list_item_height = 64;
    profile.control_button_height = 54;
    profile.control_button_min_width = 128;
    profile.compact_button_min_width = 72;
    profile.modal_min_width = 360;
    profile.modal_min_height = 190;
    profile.modal_margin = 28;
    profile.modal_pad = 18;
    profile.popup_title_height = 44;
    profile.icon_picker_button_size = 88;
    profile.ime_bar_height = 44;
    profile.ime_toggle_width = 92;
    profile.ime_toggle_height = 36;
    profile.ime_candidate_button_height = 42;
    profile.ime_keyboard_height = 320;
    profile.large_touch_hitbox = true;
    return profile;
}

PageLayoutProfile make_cardputer_zero_profile()
{
    PageLayoutProfile profile = make_pager_profile();
    profile.name = "cardputer_zero";
    profile.variant = LayoutVariant::HybridCompact;
    profile.dense = true;
    profile.top_bar_height = 22;
    profile.top_content_gap = 1;
    profile.title_font = &lv_font_montserrat_12;
    profile.body_font = &lv_font_montserrat_12;
    profile.caption_font = &lv_font_montserrat_10;
    profile.tiny_font = &lv_font_montserrat_10;
    profile.content_pad_left = 0;
    profile.content_pad_right = 0;
    profile.content_pad_top = 0;
    profile.content_pad_bottom = 0;
    profile.filter_panel_width = 78;
    profile.filter_panel_pad_row = 1;
    profile.filter_button_height = 24;
    profile.list_panel_pad_row = 2;
    profile.list_panel_pad_left = 0;
    profile.list_panel_pad_right = 0;
    profile.list_panel_margin_bottom = 1;
    profile.list_item_height = 24;
    profile.control_button_height = 24;
    profile.control_button_min_width = 68;
    profile.compact_button_min_width = 46;
    profile.modal_min_width = 150;
    profile.modal_min_height = 108;
    profile.modal_margin = 6;
    profile.modal_pad = 5;
    profile.popup_title_height = 22;
    profile.icon_picker_button_size = 44;
    profile.ime_bar_height = 20;
    profile.ime_toggle_width = 36;
    profile.ime_toggle_height = 16;
    profile.ime_candidate_button_height = 22;
    profile.ime_keyboard_height = 0;
    profile.large_touch_hitbox = false;
    return profile;
}

PageLayoutProfile make_default_profile(lv_coord_t width, lv_coord_t height)
{
    if (width >= 700 || height >= 700)
    {
        PageLayoutProfile profile = make_tab5_profile();
        // A runtime-detected tall display here is the T-Display P4, which has a
        // top-centre camera punch-out; reserve a top safe area so the bar controls
        // and notifications clear the lens. (The real M5Stack Tab5 reaches
        // make_tab5_profile via the macro path in current() and is unaffected.)
        profile.top_safe_inset = 60;
        profile.top_bar_height = static_cast<lv_coord_t>(profile.top_bar_height + 60);
        return profile;
    }
    if (width >= 400)
    {
        return make_pager_profile();
    }
    return make_tdeck_profile();
}

const PageLayoutProfile& current()
{
    static PageLayoutProfile profile = []()
    {
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
        return make_tab5_profile();
#elif defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        return make_cardputer_zero_profile();
#elif defined(ARDUINO_T_LORA_PAGER)
        return make_pager_profile();
#elif defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
        return make_tdeck_profile();
#else
        lv_coord_t width = lv_display_get_physical_horizontal_resolution(nullptr);
        if (width <= 0) width = 320;
        lv_coord_t height = lv_display_get_physical_vertical_resolution(nullptr);
        if (height <= 0) height = 240;
        return make_default_profile(width, height);
#endif
    }();
    return profile;
}

bool is_dense()
{
    return current().dense;
}

ResolvedSize resolve_modal_size(lv_coord_t requested_width,
                                lv_coord_t requested_height,
                                lv_obj_t* screen)
{
    const auto& profile = current();
    lv_obj_t* target_screen = screen ? screen : lv_screen_active();
    const lv_coord_t screen_w = fallback_screen_width(target_screen);
    const lv_coord_t screen_h = fallback_screen_height(target_screen);
    const lv_coord_t margin = std::max<lv_coord_t>(8, profile.modal_margin);

    const lv_coord_t max_width = std::max<lv_coord_t>(120, screen_w - (margin * 2));
    const lv_coord_t max_height = std::max<lv_coord_t>(100, screen_h - (margin * 2));
    const lv_coord_t desired_width = std::max(requested_width, profile.modal_min_width);
    const lv_coord_t desired_height = std::max(requested_height, profile.modal_min_height);

    ResolvedSize size{};
    size.width = std::min(desired_width, max_width);
    size.height = std::min(desired_height, max_height);
    return size;
}

lv_coord_t resolve_control_button_height()
{
    const auto& profile = current();
    return std::max(profile.control_button_height, profile.filter_button_height);
}

lv_coord_t resolve_control_button_min_width()
{
    return current().control_button_min_width;
}

lv_coord_t resolve_compact_button_min_width()
{
    return current().compact_button_min_width;
}

lv_coord_t resolve_modal_pad()
{
    return current().modal_pad;
}

lv_coord_t resolve_popup_title_height()
{
    return current().popup_title_height;
}

lv_coord_t resolve_icon_picker_button_size()
{
    return current().icon_picker_button_size;
}

const lv_font_t* resolve_title_font()
{
    return current().title_font ? current().title_font : &lv_font_montserrat_14;
}

const lv_font_t* resolve_body_font()
{
    return current().body_font ? current().body_font : &lv_font_montserrat_14;
}

const lv_font_t* resolve_caption_font()
{
    return current().caption_font ? current().caption_font : resolve_body_font();
}

const lv_font_t* resolve_tiny_font()
{
    return current().tiny_font ? current().tiny_font : resolve_caption_font();
}

} // namespace ui::page_profile
