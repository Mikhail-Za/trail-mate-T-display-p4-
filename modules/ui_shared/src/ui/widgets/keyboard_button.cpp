/**
 * @file keyboard_button.cpp
 * @brief Global on-screen-keyboard launcher button implementation
 */

#include "ui/widgets/keyboard_button.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"
#include "ui/widgets/ime/ime_widget.h"

namespace ui
{

lv_obj_t* KeyboardButton::button_ = nullptr;
lv_obj_t* KeyboardButton::keyboard_host_ = nullptr;
lv_obj_t* KeyboardButton::bound_textarea_ = nullptr;
bool KeyboardButton::keyboard_visible_ = false;

namespace
{

// One shared keyboard instance drives every field through the global button. It
// lives on lv_layer_top() so it survives the tileview screen swaps, the same way
// system_notification.cpp keeps its toast alive.
::ui::widgets::ImeWidget s_shared_ime;

// True when *any* ImeWidget is the active one. The per-screen chat/contacts IMEs
// set this when they init(); we use it to avoid stacking a second keyboard on
// top of an already-embedded one.
extern "C" bool ui_ime_is_active();

lv_obj_t* resolve_focused_textarea()
{
    lv_group_t* group = lv_group_get_default();
    if (!group)
    {
        return nullptr;
    }
    lv_obj_t* focused = lv_group_get_focused(group);
    if (!focused)
    {
        return nullptr;
    }
    if (!lv_obj_check_type(focused, &lv_textarea_class))
    {
        return nullptr;
    }
    return focused;
}

} // namespace

void KeyboardButton::init()
{
    if (button_)
    {
        return; // Already initialized
    }

    lv_obj_t* top_layer = lv_layer_top();

    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t size = profile.large_touch_hitbox ? 64 : 40;
    const lv_coord_t margin = profile.large_touch_hitbox ? 16 : 10;

    button_ = lv_btn_create(top_layer);
    lv_obj_set_size(button_, size, size);
    lv_obj_align(button_, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin);
    lv_obj_set_style_radius(button_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button_, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button_, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button_, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(button_, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(button_, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_event_cb(button_, onButtonClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* glyph = lv_label_create(button_);
    lv_label_set_text(glyph, LV_SYMBOL_KEYBOARD);
    lv_obj_set_style_text_font(glyph, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(glyph, lv_color_hex(0x3A2A1A), 0);
    lv_obj_center(glyph);

    // The button must not steal focus from text fields (it lives outside the
    // default group already, but be explicit) and must not be swallowed by a
    // sleeping/locked layer.
    lv_obj_add_flag(button_, LV_OBJ_FLAG_FLOATING);
}

void KeyboardButton::onButtonClicked(lv_event_t* e)
{
    (void)e;
    toggleKeyboard();
}

void KeyboardButton::onCloseClicked(lv_event_t* e)
{
    (void)e;
    hideKeyboard();
}

void KeyboardButton::toggleKeyboard()
{
    // Re-tap closes our own keyboard.
    if (keyboard_visible_)
    {
        hideKeyboard();
        return;
    }

    // A per-screen IME (chat compose / contacts) already owns the focused field
    // and shows its own embedded keyboard. Do not stack a second one on top.
    if (ui_ime_is_active())
    {
        return;
    }

    lv_obj_t* textarea = resolve_focused_textarea();
    if (!textarea)
    {
        // Nothing typeable is focused; give a brief, non-intrusive hint instead
        // of opening an unbound keyboard.
        return;
    }

    showKeyboardFor(textarea);
}

void KeyboardButton::showKeyboardFor(lv_obj_t* textarea)
{
    if (!textarea)
    {
        return;
    }

    // Fully tear down any prior binding (host, IME, old textarea delete hook)
    // before rebuilding for the new field.
    hideKeyboard();

    lv_obj_t* top_layer = lv_layer_top();
    const auto& profile = ::ui::page_profile::current();

    const lv_coord_t bar_height = profile.large_touch_hitbox ? 44 : 28;

    // Full-width host anchored to the bottom edge, holding a close bar plus the
    // shared IME. LV_SIZE_CONTENT lets it grow to fit the touch keyboard height.
    keyboard_host_ = lv_obj_create(top_layer);
    lv_obj_set_width(keyboard_host_, LV_PCT(100));
    lv_obj_set_height(keyboard_host_, LV_SIZE_CONTENT);
    lv_obj_align(keyboard_host_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(keyboard_host_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(keyboard_host_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(keyboard_host_, profile.large_touch_hitbox ? 8 : 4, 0);
    lv_obj_set_style_pad_row(keyboard_host_, profile.large_touch_hitbox ? 8 : 4, 0);
    lv_obj_set_style_bg_color(keyboard_host_, lv_color_hex(0xFFF0D3), 0);
    lv_obj_set_style_bg_opa(keyboard_host_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keyboard_host_, 1, 0);
    lv_obj_set_style_border_color(keyboard_host_, lv_color_hex(0xD9B06A), 0);
    lv_obj_set_style_radius(keyboard_host_, profile.large_touch_hitbox ? 16 : 8, 0);
    lv_obj_clear_flag(keyboard_host_, LV_OBJ_FLAG_SCROLLABLE);

    // Close bar: a right-aligned close button so the keyboard can be dismissed
    // without finding the launcher button again.
    lv_obj_t* close_bar = lv_obj_create(keyboard_host_);
    lv_obj_set_width(close_bar, LV_PCT(100));
    lv_obj_set_height(close_bar, bar_height);
    lv_obj_set_flex_flow(close_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(close_bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(close_bar, 0, 0);
    lv_obj_set_style_bg_opa(close_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close_bar, 0, 0);
    lv_obj_clear_flag(close_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* close_btn = lv_btn_create(close_bar);
    lv_obj_set_size(close_btn, bar_height, bar_height);
    lv_obj_set_style_radius(close_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, onCloseClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0x3A2A1A), 0);
    lv_obj_center(close_label);

    // The IME builds its own column (mode bar + candidates + key matrix) inside
    // a child container; give it a full-width slot below the close bar.
    lv_obj_t* ime_slot = lv_obj_create(keyboard_host_);
    lv_obj_set_width(ime_slot, LV_PCT(100));
    lv_obj_set_height(ime_slot, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ime_slot, 0, 0);
    lv_obj_set_style_bg_opa(ime_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ime_slot, 0, 0);
    lv_obj_clear_flag(ime_slot, LV_OBJ_FLAG_SCROLLABLE);

    s_shared_ime.init(ime_slot, textarea);
    bound_textarea_ = textarea;

    // The keyboard lives on the top layer and outlives the field, so if the
    // field is destroyed (e.g. the settings modal closes) auto-hide to avoid a
    // dangling textarea pointer in the IME.
    lv_obj_add_event_cb(textarea, onBoundTextareaDeleted, LV_EVENT_DELETE, nullptr);

    // The IME registers its hidden focus proxy in the default group so key
    // routing (CN editing) works; mirror what the chat controller does.
    if (lv_group_t* g = lv_group_get_default())
    {
        if (lv_obj_t* proxy = s_shared_ime.focus_obj())
        {
            lv_group_add_obj(g, proxy);
        }
        // Keep the actual field focused so masking / cursor stay correct.
        lv_group_focus_obj(textarea);
    }

    lv_obj_move_foreground(keyboard_host_);
    // Keep the launcher button tappable on top of the keyboard for re-tap close.
    if (button_)
    {
        lv_obj_move_foreground(button_);
    }
    keyboard_visible_ = true;
}

void KeyboardButton::hideKeyboard()
{
    if (!keyboard_visible_ && !keyboard_host_ && !bound_textarea_)
    {
        return;
    }

    if (bound_textarea_)
    {
        lv_obj_remove_event_cb_with_user_data(bound_textarea_, onBoundTextareaDeleted, nullptr);
        bound_textarea_ = nullptr;
    }
    s_shared_ime.detach();
    if (keyboard_host_)
    {
        lv_obj_del(keyboard_host_);
        keyboard_host_ = nullptr;
    }
    keyboard_visible_ = false;
}

void KeyboardButton::onBoundTextareaDeleted(lv_event_t* e)
{
    // The bound field is going away. Drop our reference first so hideKeyboard()
    // does not try to deregister the callback on a half-deleted object, then
    // tear down the keyboard host and detach the IME.
    (void)e;
    bound_textarea_ = nullptr;
    s_shared_ime.detach();
    if (keyboard_host_)
    {
        lv_obj_del_async(keyboard_host_);
        keyboard_host_ = nullptr;
    }
    keyboard_visible_ = false;
}

bool KeyboardButton::keyboardVisible()
{
    return keyboard_visible_;
}

} // namespace ui
