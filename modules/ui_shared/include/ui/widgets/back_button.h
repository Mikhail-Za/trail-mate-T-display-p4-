#pragma once

#include "lvgl.h"

namespace ui
{

// Enlarge an app's "< Back" button into a reliable touch target: a comfortable
// fixed height, generous horizontal padding, rounded corners, an extended click
// area (so a near-miss at the tucked-in top-left corner still registers), and a
// readable label. This gives the utility apps the same easy-to-tap Back the
// shared top bar (SSTV / walkie-talkie pages) already has; they previously used
// a default LV_SIZE_CONTENT button that was tiny and hard to hit in the corner.
// Call right after creating the button and its "< Back" label.
inline void style_back_button(lv_obj_t* btn, lv_obj_t* lbl)
{
    if (btn == nullptr)
    {
        return;
    }
    lv_obj_set_height(btn, 48);
    lv_obj_set_style_pad_left(btn, 18, 0);
    lv_obj_set_style_pad_right(btn, 18, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    // Extend the tappable area beyond the visual bounds without moving anything,
    // so taps landing just outside the corner-tucked button still trigger it.
    lv_obj_set_ext_click_area(btn, 10);
#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
    if (lbl != nullptr)
    {
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    }
#else
    (void)lbl;
#endif
}

// Create a Back button laid out at the TOP-LEFT. Several utility screens parent
// the button directly on a column root whose cross-axis is centered, which pins
// the button to the top-CENTRE (the "wrong spot"). This wraps it in a full-width,
// left-aligned, transparent row so it sits top-left like the bar-based apps, and
// applies the standard large touch target. Returns the button (wired to `cb`).
inline lv_obj_t* make_back_button_row(lv_obj_t* root, lv_event_cb_t cb, void* user_data = nullptr)
{
    lv_obj_t* row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* btn = lv_button_create(row);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl);
    style_back_button(btn, lbl);
    if (cb != nullptr)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

} // namespace ui
