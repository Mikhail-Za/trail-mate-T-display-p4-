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

} // namespace ui
