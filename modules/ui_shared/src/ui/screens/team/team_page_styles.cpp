/**
 * @file team_page_styles.cpp
 * @brief Team page styles
 */

#include "ui/screens/team/team_page_styles.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"

namespace team
{
namespace ui
{
namespace style
{
namespace
{
static constexpr uint32_t kBtnBg = 0xFFF7E9;
static constexpr uint32_t kBtnBgFoc = 0xEBA341;
static constexpr uint32_t kBtnBorder = 0xD9B06A;
static constexpr uint32_t kTextMain = 0x3A2A1A;
static constexpr uint32_t kTextSub = 0x6A5646;
static constexpr uint32_t kListBg = 0xFFF7E9;
static constexpr uint32_t kListBorder = 0xD9B06A;

static lv_style_t s_root;
static lv_style_t s_header;
static lv_style_t s_content;
static lv_style_t s_body;
static lv_style_t s_actions;
static lv_style_t s_section_label;
static lv_style_t s_meta_label;
static lv_style_t s_list_item;
static lv_style_t s_btn_basic;
static lv_style_t s_btn_focused;
static bool s_inited = false;

lv_style_selector_t selector_for_state(lv_state_t state)
{
    return static_cast<lv_style_selector_t>(static_cast<unsigned>(LV_PART_MAIN) | static_cast<unsigned>(state));
}
} // namespace

void init_once()
{
    if (s_inited)
    {
        return;
    }
    s_inited = true;
    const bool dense = ::ui::page_profile::is_dense();
    const lv_coord_t body_pad = dense ? 3 : 6;
    const lv_coord_t row_gap = dense ? 2 : 6;
    const lv_coord_t action_pad = dense ? 1 : 4;
    const lv_coord_t action_gap = dense ? 4 : 6;
    const lv_coord_t button_radius = dense ? 7 : 12;
    const lv_coord_t list_radius = dense ? 5 : 8;
    const lv_font_t* body_font = ::ui::page_profile::resolve_body_font();
    const lv_font_t* meta_font = ::ui::page_profile::resolve_caption_font();

    lv_style_init(&s_root);
    lv_style_set_bg_color(&s_root, lv_color_hex(0xFFF3DF));
    lv_style_set_bg_opa(&s_root, LV_OPA_COVER);
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);

    lv_style_init(&s_header);
    lv_style_set_bg_color(&s_header, lv_color_hex(0xFFF3DF));
    lv_style_set_bg_opa(&s_header, LV_OPA_COVER);
    lv_style_set_border_width(&s_header, 0);
    lv_style_set_pad_all(&s_header, 0);

    lv_style_init(&s_content);
    lv_style_set_bg_color(&s_content, lv_color_hex(0xFFF3DF));
    lv_style_set_bg_opa(&s_content, LV_OPA_COVER);
    lv_style_set_border_width(&s_content, 0);
    lv_style_set_pad_all(&s_content, 0);
    lv_style_set_pad_top(&s_content, dense ? 0 : 3);

    lv_style_init(&s_body);
    lv_style_set_bg_color(&s_body, lv_color_hex(0xFFF3DF));
    lv_style_set_bg_opa(&s_body, LV_OPA_COVER);
    lv_style_set_border_width(&s_body, 0);
    lv_style_set_pad_all(&s_body, body_pad);
    lv_style_set_pad_row(&s_body, row_gap);
    lv_style_set_pad_column(&s_body, 0);

    lv_style_init(&s_actions);
    lv_style_set_bg_color(&s_actions, lv_color_hex(0xFFF3DF));
    lv_style_set_bg_opa(&s_actions, LV_OPA_COVER);
    lv_style_set_border_width(&s_actions, 0);
    lv_style_set_pad_all(&s_actions, action_pad);
    lv_style_set_pad_row(&s_actions, 0);
    lv_style_set_pad_column(&s_actions, action_gap);

    lv_style_init(&s_section_label);
    lv_style_set_text_color(&s_section_label, lv_color_hex(kTextMain));
    lv_style_set_text_align(&s_section_label, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&s_section_label, ::ui::fonts::localized_font(body_font));

    lv_style_init(&s_meta_label);
    lv_style_set_text_color(&s_meta_label, lv_color_hex(kTextSub));
    lv_style_set_text_align(&s_meta_label, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&s_meta_label, ::ui::fonts::localized_font(meta_font));

    lv_style_init(&s_list_item);
    lv_style_set_bg_color(&s_list_item, lv_color_hex(kListBg));
    lv_style_set_bg_opa(&s_list_item, LV_OPA_COVER);
    // Explicit dark text color. Without this, list-item label text fell back to the
    // LVGL default theme color (light), which made UNFOCUSED rows invisible on the
    // cream background -- the real cause of "blank member" rows (a peer member shows
    // blank while "You" holds focus). Applies to both states; on the focused yellow
    // bg dark text reads fine too.
    lv_style_set_text_color(&s_list_item, lv_color_hex(kTextMain));
    lv_style_set_border_width(&s_list_item, 1);
    lv_style_set_border_color(&s_list_item, lv_color_hex(kListBorder));
    lv_style_set_radius(&s_list_item, list_radius);
    lv_style_set_pad_all(&s_list_item, dense ? 3 : 6);

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_color(&s_btn_basic, lv_color_hex(kBtnBg));
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_text_color(&s_btn_basic, lv_color_hex(kTextMain));
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, lv_color_hex(kBtnBorder));
    lv_style_set_radius(&s_btn_basic, button_radius);

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, lv_color_hex(kBtnBgFoc));
    lv_style_set_bg_opa(&s_btn_focused, LV_OPA_COVER);
}

void apply_root(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_root, 0);
}

void apply_header(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_header, 0);
}

void apply_content(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_content, 0);
}

void apply_body(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_body, 0);
}

void apply_actions(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_actions, 0);
}

void apply_section_label(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_section_label, 0);
}

void apply_meta_label(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_meta_label, 0);
}

void apply_list_item(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_list_item, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, selector_for_state(LV_STATE_FOCUSED));
}

void apply_button_primary(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, selector_for_state(LV_STATE_FOCUSED));
}

void apply_button_secondary(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, selector_for_state(LV_STATE_FOCUSED));
}

} // namespace style
} // namespace ui
} // namespace team
