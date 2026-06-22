/**
 * @file keyboard_button.h
 * @brief Global on-screen-keyboard launcher button (touch devices)
 *
 * A single bottom-right button that lives on lv_layer_top() so it survives
 * tileview screen swaps. Tapping it opens a shared ImeWidget bound to whatever
 * textarea currently holds focus (chat compose, WiFi password, username,
 * contact name, ...), making those fields typeable on keyboard-less hardware.
 * Re-tapping (or the keyboard's close affordance) hides it again.
 */

#pragma once

#include "lvgl.h"

namespace ui
{

class KeyboardButton
{
  public:
    /**
     * @brief Create the launcher button once on the top layer.
     * Must be called after LVGL is initialized and under the LVGL lock.
     * Subsequent calls are no-ops.
     */
    static void init();

    /**
     * @brief Hide and detach the shared keyboard if it is currently shown.
     * Safe to call when nothing is open.
     */
    static void hideKeyboard();

    /**
     * @brief Whether the shared (global) keyboard is currently shown.
     */
    static bool keyboardVisible();

  private:
    static void onButtonClicked(lv_event_t* e);
    static void onCloseClicked(lv_event_t* e);
    static void onBoundTextareaDeleted(lv_event_t* e);
    static void toggleKeyboard();
    static void showKeyboardFor(lv_obj_t* textarea);

    static lv_obj_t* bound_textarea_;

    static lv_obj_t* button_;
    static lv_obj_t* keyboard_host_;
    static bool keyboard_visible_;
};

} // namespace ui
