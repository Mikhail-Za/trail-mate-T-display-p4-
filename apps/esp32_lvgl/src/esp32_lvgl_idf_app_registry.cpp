#include "ui/app_registry.h"

#include "platform/ui/gps_runtime.h"
#include "platform/ui/wireless_companion_runtime.h"
#include "ui/app_catalog.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/localization.h"
#include "ui/page/page_host.h"
#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/contacts/contacts_page_shell.h"
#include "ui/screens/energy_sweep/energy_sweep_page_shell.h"
#include "ui/screens/extensions/extensions_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_shell.h"
#include "ui/screens/gps/gps_page_shell.h"
#include "ui/screens/pc_link/pc_link_page_shell.h"
#include "ui/screens/settings/settings_page_shell.h"
#include "ui/screens/tracker/tracker_page_shell.h"
#include "ui/ui_theme.h"

#include "platform/ui/device_runtime.h"

// Node Radar reads the same nearby-node source the Contacts app uses
// (app::messagingFacade().getContactService().getNearby()).
#include "app/app_facade_access.h"
#include "chat/domain/contact_types.h"
#include "chat/usecase/contact_service.h"
#include "sys/clock.h"

#include "esp_heap_caps.h"
#include "esp_system.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

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

// ---------------------------------------------------------------------------
// Snake: a fully self-contained touch-only Snake game, built inline the same
// way as the C6 Companion app above (file-static state, enter()/exit() that
// build/tear-down a root sized LV_PCT(100), and a CallbackAppScreen added to
// s_apps[]). The boot self-test enters then immediately exits this screen once;
// snake_exit must therefore delete the repeating game timer and the root with
// the same null/validity guards companion_exit uses so nothing dangles.
// ---------------------------------------------------------------------------
struct SnakePageState
{
    static constexpr int kCols = 16;
    static constexpr int kRows = 20;
    // Cell size derived from the usable width: the 540px portrait panel minus the
    // root's 2*kRootPad side margins, divided by the column count. 16 cols over
    // (540 - 2*10) = 520px -> 32px cells, so the board fills the screen width.
    static constexpr int kRootPad = 10;
    static constexpr int kScreenW = 540;
    static constexpr int kCellPx = (kScreenW - 2 * kRootPad) / kCols;  // = 32
    static constexpr int kCellCount = kCols * kRows;
    // Step pacing: start at a comfortable 220ms and ease 6ms faster per point of
    // score, but never quicker than 120ms so a long snake never becomes frantic.
    static constexpr uint32_t kBasePeriodMs = 220;
    static constexpr uint32_t kMinPeriodMs = 120;
    static constexpr uint32_t kSpeedupPerPointMs = 6;

    lv_obj_t* root = nullptr;
    lv_obj_t* score_label = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_timer_t* timer = nullptr;
    lv_obj_t* cells[kCellCount] = {nullptr};

    // Snake body stored as a ring of (x,y) cells, head at index 0..length-1
    // in body[] order (body[0] = head). Max length is the whole grid.
    int16_t body_x[kCellCount] = {0};
    int16_t body_y[kCellCount] = {0};
    int length = 0;

    int dir_x = 1;  // current movement direction
    int dir_y = 0;
    int next_dir_x = 1;  // queued direction (applied at next tick)
    int next_dir_y = 0;

    int food_x = 0;
    int food_y = 0;

    unsigned int score = 0;
    bool game_over = false;
    uint32_t rng = 0;  // small LCG state
};

SnakePageState s_snake_state;

uint32_t snake_rand(SnakePageState* st)
{
    // Numerical Recipes LCG; deterministic per-seed, no global state.
    st->rng = st->rng * 1664525u + 1013904223u;
    return st->rng;
}

int snake_cell_index(int x, int y)
{
    return y * SnakePageState::kCols + x;
}

bool snake_body_contains(SnakePageState* st, int x, int y, int ignore_tail)
{
    // ignore_tail: when moving, the tail cell is about to vacate, so it does
    // not count as a collision (unless the snake just ate and grew).
    const int n = st->length - (ignore_tail ? 1 : 0);
    for (int i = 0; i < n; ++i)
    {
        if (st->body_x[i] == x && st->body_y[i] == y)
        {
            return true;
        }
    }
    return false;
}

void snake_place_food(SnakePageState* st)
{
    // Pick a uniformly random empty cell. The grid is far larger than the
    // snake early on, so rejection sampling terminates quickly; the bounded
    // fallback scan guarantees termination even on a near-full board.
    const int free_cells = SnakePageState::kCellCount - st->length;
    if (free_cells <= 0)
    {
        st->food_x = -1;
        st->food_y = -1;
        return;
    }
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const int x = static_cast<int>(snake_rand(st) % SnakePageState::kCols);
        const int y = static_cast<int>(snake_rand(st) % SnakePageState::kRows);
        if (!snake_body_contains(st, x, y, 0))
        {
            st->food_x = x;
            st->food_y = y;
            return;
        }
    }
    // Fallback: deterministic scan for the first empty cell.
    for (int y = 0; y < SnakePageState::kRows; ++y)
    {
        for (int x = 0; x < SnakePageState::kCols; ++x)
        {
            if (!snake_body_contains(st, x, y, 0))
            {
                st->food_x = x;
                st->food_y = y;
                return;
            }
        }
    }
}

void snake_render(SnakePageState* st)
{
    const lv_color_t empty = lv_color_hex(0xDDDDDD);
    const lv_color_t green = ui::theme::status_green();
    const lv_color_t red = ui::theme::error();
    for (int i = 0; i < SnakePageState::kCellCount; ++i)
    {
        if (st->cells[i] && lv_obj_is_valid(st->cells[i]))
        {
            lv_obj_set_style_bg_color(st->cells[i], empty, 0);
        }
    }
    if (st->food_x >= 0 && st->food_y >= 0)
    {
        const int fi = snake_cell_index(st->food_x, st->food_y);
        if (st->cells[fi] && lv_obj_is_valid(st->cells[fi]))
        {
            lv_obj_set_style_bg_color(st->cells[fi], red, 0);
        }
    }
    for (int i = 0; i < st->length; ++i)
    {
        const int ci = snake_cell_index(st->body_x[i], st->body_y[i]);
        if (ci >= 0 && ci < SnakePageState::kCellCount && st->cells[ci] &&
            lv_obj_is_valid(st->cells[ci]))
        {
            lv_obj_set_style_bg_color(st->cells[ci], green, 0);
        }
    }
}

void snake_update_score(SnakePageState* st)
{
    if (st->score_label && lv_obj_is_valid(st->score_label))
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Score: %u", st->score);
        lv_label_set_text(st->score_label, buf);
    }
}

void snake_reset(SnakePageState* st)
{
    st->length = 3;
    const int start_x = SnakePageState::kCols / 2;
    const int start_y = SnakePageState::kRows / 2;
    // body[0] = head, growing to the left so initial rightward motion is legal.
    for (int i = 0; i < st->length; ++i)
    {
        st->body_x[i] = static_cast<int16_t>(start_x - i);
        st->body_y[i] = static_cast<int16_t>(start_y);
    }
    st->dir_x = 1;
    st->dir_y = 0;
    st->next_dir_x = 1;
    st->next_dir_y = 0;
    st->score = 0;
    st->game_over = false;
    snake_place_food(st);
    snake_update_score(st);
    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        lv_label_set_text(st->status_label, "");
    }
    if (st->timer)
    {
        lv_timer_set_period(st->timer, SnakePageState::kBasePeriodMs);
        lv_timer_resume(st->timer);
    }
    snake_render(st);
}

void snake_tick(lv_timer_t* timer)
{
    auto* st = static_cast<SnakePageState*>(lv_timer_get_user_data(timer));
    if (!st || st->game_over)
    {
        return;
    }

    // Apply the queued direction (already validated against 180-degree reversal
    // when it was set, but re-guard here in case length collapsed).
    st->dir_x = st->next_dir_x;
    st->dir_y = st->next_dir_y;

    const int new_x = st->body_x[0] + st->dir_x;
    const int new_y = st->body_y[0] + st->dir_y;

    const bool ate = (new_x == st->food_x && new_y == st->food_y);

    // Wall collision.
    if (new_x < 0 || new_x >= SnakePageState::kCols || new_y < 0 ||
        new_y >= SnakePageState::kRows)
    {
        st->game_over = true;
    }
    // Self collision (the tail cell is vacated this step unless we just ate).
    else if (snake_body_contains(st, new_x, new_y, ate ? 0 : 1))
    {
        st->game_over = true;
    }

    if (st->game_over)
    {
        lv_timer_pause(timer);
        if (st->status_label && lv_obj_is_valid(st->status_label))
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Game Over - Score %u", st->score);
            lv_label_set_text(st->status_label, buf);
        }
        return;
    }

    // Advance the body: shift cells back, then set the new head at index 0.
    int new_length = st->length;
    if (ate)
    {
        new_length = st->length + 1;
        if (new_length > SnakePageState::kCellCount)
        {
            new_length = SnakePageState::kCellCount;
        }
    }
    for (int i = new_length - 1; i > 0; --i)
    {
        st->body_x[i] = st->body_x[i - 1];
        st->body_y[i] = st->body_y[i - 1];
    }
    st->body_x[0] = static_cast<int16_t>(new_x);
    st->body_y[0] = static_cast<int16_t>(new_y);
    st->length = new_length;

    if (ate)
    {
        st->score += 1;
        snake_update_score(st);
        snake_place_food(st);
        // Ease the step interval faster as the score climbs (clamped so it never
        // drops below kMinPeriodMs). Applied only when the score changes.
        if (st->timer)
        {
            uint32_t period = SnakePageState::kBasePeriodMs;
            const uint32_t speedup = st->score * SnakePageState::kSpeedupPerPointMs;
            if (speedup < period - SnakePageState::kMinPeriodMs)
            {
                period -= speedup;
            }
            else
            {
                period = SnakePageState::kMinPeriodMs;
            }
            lv_timer_set_period(st->timer, period);
        }
    }

    snake_render(st);
}

void snake_set_dir(SnakePageState* st, int dx, int dy)
{
    if (!st || st->game_over)
    {
        return;
    }
    // Forbid an immediate 180-degree reversal (compare against the CURRENT
    // direction, the one the last tick actually moved).
    if (dx == -st->dir_x && dy == -st->dir_y)
    {
        return;
    }
    st->next_dir_x = dx;
    st->next_dir_y = dy;
}

lv_obj_t* snake_make_dpad_button(lv_obj_t* parent,
                                 SnakePageState* st,
                                 const char* text,
                                 lv_event_cb_t cb)
{
    // Large touch target (96x88) with the callback bound to PRESSED so the
    // direction is captured the instant the finger lands, not on release. The
    // hitbox is the whole button, not just the glyph, so taps register reliably.
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 96, 88);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, st);
    return btn;
}

void snake_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<SnakePageState*>(user_data);
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
    lv_obj_set_style_pad_all(state->root, SnakePageState::kRootPad, 0);
    lv_obj_set_flex_flow(state->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->root, 8, 0);
    // Vertically scrollable as a safety net: if the board + controls ever exceed
    // the 1168px height, the page can be dragged so no control is unreachable
    // (same pattern as the Help page).
    lv_obj_set_scroll_dir(state->root, LV_DIR_VER);
    lv_obj_add_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // Top row: Back button + score label.
    lv_obj_t* top = lv_obj_create(state->root);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* back_btn = lv_button_create(top);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    state->score_label = lv_label_create(top);
    lv_obj_set_style_text_color(state->score_label, ui::theme::text(), 0);
    lv_obj_set_style_text_font(state->score_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(state->score_label, "Score: 0");

    // Status (Game Over) line.
    state->status_label = lv_label_create(state->root);
    lv_obj_set_style_text_color(state->status_label, ui::theme::error(), 0);
    lv_obj_set_style_text_font(state->status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(state->status_label, "");

    // Play grid: a fixed-size container holding kCols x kRows cell rectangles,
    // positioned absolutely (created once, recolored each tick).
    const int grid_w = SnakePageState::kCols * SnakePageState::kCellPx;
    const int grid_h = SnakePageState::kRows * SnakePageState::kCellPx;
    lv_obj_t* grid = lv_obj_create(state->root);
    lv_obj_set_size(grid, grid_w, grid_h);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid, 1, 0);
    lv_obj_set_style_border_color(grid, ui::theme::border(), 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int y = 0; y < SnakePageState::kRows; ++y)
    {
        for (int x = 0; x < SnakePageState::kCols; ++x)
        {
            lv_obj_t* cell = lv_obj_create(grid);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, SnakePageState::kCellPx - 1, SnakePageState::kCellPx - 1);
            lv_obj_set_pos(cell, x * SnakePageState::kCellPx, y * SnakePageState::kCellPx);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0xDDDDDD), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(cell, 2, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            state->cells[snake_cell_index(x, y)] = cell;
        }
    }

    // D-pad: Up on its own row, then Left/Down/Right, then Restart.
    lv_obj_t* pad_up_row = lv_obj_create(state->root);
    lv_obj_set_size(pad_up_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pad_up_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad_up_row, 0, 0);
    lv_obj_set_style_pad_all(pad_up_row, 0, 0);
    lv_obj_set_flex_flow(pad_up_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pad_up_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    snake_make_dpad_button(
        pad_up_row, state, LV_SYMBOL_UP,
        [](lv_event_t* e)
        { snake_set_dir(static_cast<SnakePageState*>(lv_event_get_user_data(e)), 0, -1); });

    lv_obj_t* pad_mid_row = lv_obj_create(state->root);
    lv_obj_set_size(pad_mid_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pad_mid_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad_mid_row, 0, 0);
    lv_obj_set_style_pad_all(pad_mid_row, 0, 0);
    lv_obj_set_flex_flow(pad_mid_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pad_mid_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pad_mid_row, 12, 0);
    snake_make_dpad_button(
        pad_mid_row, state, LV_SYMBOL_LEFT,
        [](lv_event_t* e)
        { snake_set_dir(static_cast<SnakePageState*>(lv_event_get_user_data(e)), -1, 0); });
    snake_make_dpad_button(
        pad_mid_row, state, LV_SYMBOL_DOWN,
        [](lv_event_t* e)
        { snake_set_dir(static_cast<SnakePageState*>(lv_event_get_user_data(e)), 0, 1); });
    snake_make_dpad_button(
        pad_mid_row, state, LV_SYMBOL_RIGHT,
        [](lv_event_t* e)
        { snake_set_dir(static_cast<SnakePageState*>(lv_event_get_user_data(e)), 1, 0); });

    lv_obj_t* restart_btn = lv_button_create(state->root);
    lv_obj_t* restart_lbl = lv_label_create(restart_btn);
    lv_label_set_text(restart_lbl, LV_SYMBOL_REFRESH " Restart");
    lv_obj_center(restart_lbl);
    lv_obj_add_event_cb(
        restart_btn,
        [](lv_event_t* e)
        { snake_reset(static_cast<SnakePageState*>(lv_event_get_user_data(e))); },
        LV_EVENT_CLICKED, state);

    // Seed the LCG from the tick counter, then start the game and the tick timer.
    // ~220ms/step is a comfortable touchscreen pace (eased faster as the score
    // climbs in snake_tick, but never below kMinPeriodMs so it never feels frantic).
    state->rng = lv_tick_get() ^ 0x9E3779B9u;
    state->timer = lv_timer_create(snake_tick, SnakePageState::kBasePeriodMs, state);
    snake_reset(state);
}

void snake_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<SnakePageState*>(user_data);
    if (!state)
    {
        return;
    }
    if (state->timer)
    {
        lv_timer_del(state->timer);
        state->timer = nullptr;
    }
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->score_label = nullptr;
        state->status_label = nullptr;
        for (int i = 0; i < SnakePageState::kCellCount; ++i)
        {
            state->cells[i] = nullptr;
        }
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->score_label = nullptr;
    state->status_label = nullptr;
    for (int i = 0; i < SnakePageState::kCellCount; ++i)
    {
        state->cells[i] = nullptr;
    }
}

ui::CallbackAppScreen s_snake_app("snake", "Snake", &Chat, snake_enter, snake_exit, &s_snake_state);

// ---------------------------------------------------------------------------
// Tetris: a fully self-contained touch-only Tetris game, built inline the same
// way as the C6 Companion / Snake apps above (file-static state, enter()/exit()
// that build/tear-down a root sized LV_PCT(100), and a CallbackAppScreen added
// to s_apps[]). The boot self-test enters then immediately exits this screen
// once; tetris_exit must therefore delete the repeating drop timer and the root
// with the same null/validity guards companion_exit/snake_exit use so nothing
// dangles. The 7 standard tetrominoes are encoded as 4 explicit rotation states
// each (a list of 4 (x,y) cell offsets inside a 4x4 box), which keeps rotation,
// wall/floor/stack collision and full-row clearing simple and correct.
// ---------------------------------------------------------------------------
struct TetrisPageState
{
    static constexpr int kCols = 10;
    static constexpr int kRows = 20;
    // Cell size derived from the real usable area. By width: (540 - 2*10)/10 = 52px,
    // but 20 rows * 52 = 1040px would crowd out the controls on the 1168px panel.
    // So also bound the cell by a board-height budget (kBoardHBudget) that leaves
    // room for the header + the bottom control pad, and take the smaller of the
    // two. 44px wins -> a 440x880 board: far larger than the old 220px-wide board,
    // fills most of the width, and still leaves room for big controls below (with
    // the scrollable root as a safety net). Recomputed positions/sizes follow.
    static constexpr int kRootPad = 10;
    static constexpr int kScreenW = 540;
    static constexpr int kScreenH = 1168;
    static constexpr int kBoardHBudget = 880;  // px reserved for the board itself
    static constexpr int kCellByW = (kScreenW - 2 * kRootPad) / kCols;  // = 52
    static constexpr int kCellByH = kBoardHBudget / kRows;              // = 44
    static constexpr int kCellPx = (kCellByW < kCellByH) ? kCellByW : kCellByH;  // = 44
    static constexpr int kCellCount = kCols * kRows;
    static constexpr int kEmpty = -1;
    // Gravity pacing: ~550ms per row. Soft-drop is immediate on tap (and the timer
    // also speeds up while Down is held, restored on release).
    static constexpr uint32_t kBasePeriodMs = 550;
    static constexpr uint32_t kSoftDropPeriodMs = 60;

    lv_obj_t* root = nullptr;
    lv_obj_t* score_label = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_timer_t* timer = nullptr;
    lv_obj_t* cells[kCellCount] = {nullptr};

    // Settled board: each cell holds a piece-type index (0..6) or kEmpty.
    int8_t board[kCellCount] = {0};

    // Active falling piece.
    int piece = 0;       // 0..6 tetromino type
    int rotation = 0;    // 0..3 rotation state
    int piece_x = 0;     // origin column of the 4x4 box
    int piece_y = 0;     // origin row of the 4x4 box
    int next_piece = 0;  // type spawned next

    unsigned int score = 0;
    unsigned int lines = 0;
    bool game_over = false;
    uint32_t rng = 0;  // small LCG state
};

TetrisPageState s_tetris_state;

// Each tetromino: [type][rotation][4 blocks] = {x,y} offsets within a 4x4 box.
// Standard SRS-like footprints; all four rotations listed explicitly so no
// rotation math is needed at runtime (O = same shape 4x, that is intentional).
struct TetrisCellOffset
{
    int8_t x;
    int8_t y;
};

const TetrisCellOffset kTetrominoes[7][4][4] = {
    // I
    {
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
        {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
        {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
        {{1, 0}, {1, 1}, {1, 2}, {1, 3}},
    },
    // J
    {
        {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {2, 1}, {2, 2}},
        {{1, 0}, {1, 1}, {0, 2}, {1, 2}},
    },
    // L
    {
        {{2, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
        {{0, 1}, {1, 1}, {2, 1}, {0, 2}},
        {{0, 0}, {1, 0}, {1, 1}, {1, 2}},
    },
    // O
    {
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
    },
    // S
    {
        {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
        {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
        {{1, 1}, {2, 1}, {0, 2}, {1, 2}},
        {{0, 0}, {0, 1}, {1, 1}, {1, 2}},
    },
    // T
    {
        {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{1, 0}, {1, 1}, {2, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {2, 1}, {1, 2}},
        {{1, 0}, {0, 1}, {1, 1}, {1, 2}},
    },
    // Z
    {
        {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
        {{2, 0}, {1, 1}, {2, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {1, 2}, {2, 2}},
        {{1, 0}, {0, 1}, {1, 1}, {0, 2}},
    },
};

lv_color_t tetris_piece_color(int piece)
{
    // Distinct, high-contrast colors per tetromino type.
    switch (piece)
    {
    case 0: return lv_color_hex(0x00BCD4);  // I - cyan
    case 1: return lv_color_hex(0x3F51B5);  // J - blue
    case 2: return lv_color_hex(0xFF9800);  // L - orange
    case 3: return lv_color_hex(0xFFC107);  // O - yellow
    case 4: return lv_color_hex(0x4CAF50);  // S - green
    case 5: return lv_color_hex(0x9C27B0);  // T - purple
    case 6: return lv_color_hex(0xF44336);  // Z - red
    default: return lv_color_hex(0xDDDDDD);
    }
}

uint32_t tetris_rand(TetrisPageState* st)
{
    // Numerical Recipes LCG; deterministic per-seed, no global state.
    st->rng = st->rng * 1664525u + 1013904223u;
    return st->rng;
}

int tetris_cell_index(int x, int y)
{
    return y * TetrisPageState::kCols + x;
}

// Returns true if the piece at (rot, ox, oy) collides with a wall, the floor,
// or a settled block. Cells above the top (y < 0) are allowed (spawn overhang).
bool tetris_collides(TetrisPageState* st, int piece, int rot, int ox, int oy)
{
    for (int i = 0; i < 4; ++i)
    {
        const int x = ox + kTetrominoes[piece][rot][i].x;
        const int y = oy + kTetrominoes[piece][rot][i].y;
        if (x < 0 || x >= TetrisPageState::kCols || y >= TetrisPageState::kRows)
        {
            return true;
        }
        if (y >= 0 && st->board[tetris_cell_index(x, y)] != TetrisPageState::kEmpty)
        {
            return true;
        }
    }
    return false;
}

void tetris_update_score(TetrisPageState* st)
{
    if (st->score_label && lv_obj_is_valid(st->score_label))
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Score %u  Lines %u", st->score, st->lines);
        lv_label_set_text(st->score_label, buf);
    }
}

void tetris_render(TetrisPageState* st)
{
    const lv_color_t empty = lv_color_hex(0x222222);
    // Paint the settled board.
    for (int i = 0; i < TetrisPageState::kCellCount; ++i)
    {
        if (!st->cells[i] || !lv_obj_is_valid(st->cells[i]))
        {
            continue;
        }
        const int8_t v = st->board[i];
        lv_obj_set_style_bg_color(
            st->cells[i], v == TetrisPageState::kEmpty ? empty : tetris_piece_color(v), 0);
    }
    // Overlay the active falling piece (only while a game is in progress).
    if (!st->game_over)
    {
        const lv_color_t col = tetris_piece_color(st->piece);
        for (int i = 0; i < 4; ++i)
        {
            const int x = st->piece_x + kTetrominoes[st->piece][st->rotation][i].x;
            const int y = st->piece_y + kTetrominoes[st->piece][st->rotation][i].y;
            if (x < 0 || x >= TetrisPageState::kCols || y < 0 || y >= TetrisPageState::kRows)
            {
                continue;
            }
            const int ci = tetris_cell_index(x, y);
            if (st->cells[ci] && lv_obj_is_valid(st->cells[ci]))
            {
                lv_obj_set_style_bg_color(st->cells[ci], col, 0);
            }
        }
    }
}

void tetris_spawn(TetrisPageState* st)
{
    st->piece = st->next_piece;
    st->next_piece = static_cast<int>(tetris_rand(st) % 7u);
    st->rotation = 0;
    // Center the 4x4 box horizontally; start with the box top at row 0.
    st->piece_x = (TetrisPageState::kCols - 4) / 2;
    st->piece_y = 0;
    // Game over when the freshly spawned piece immediately collides.
    if (tetris_collides(st, st->piece, st->rotation, st->piece_x, st->piece_y))
    {
        st->game_over = true;
        if (st->timer)
        {
            lv_timer_pause(st->timer);
        }
        if (st->status_label && lv_obj_is_valid(st->status_label))
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Game Over - Score %u", st->score);
            lv_label_set_text(st->status_label, buf);
        }
    }
}

void tetris_clear_lines(TetrisPageState* st)
{
    int cleared = 0;
    // Scan from the bottom up; when a row is full, collapse everything above it.
    for (int y = TetrisPageState::kRows - 1; y >= 0;)
    {
        bool full = true;
        for (int x = 0; x < TetrisPageState::kCols; ++x)
        {
            if (st->board[tetris_cell_index(x, y)] == TetrisPageState::kEmpty)
            {
                full = false;
                break;
            }
        }
        if (!full)
        {
            --y;
            continue;
        }
        ++cleared;
        // Shift every row above y down by one.
        for (int yy = y; yy > 0; --yy)
        {
            for (int x = 0; x < TetrisPageState::kCols; ++x)
            {
                st->board[tetris_cell_index(x, yy)] =
                    st->board[tetris_cell_index(x, yy - 1)];
            }
        }
        // Clear the now-duplicated top row, then re-test the same y (a new row
        // has fallen into it).
        for (int x = 0; x < TetrisPageState::kCols; ++x)
        {
            st->board[tetris_cell_index(x, 0)] = TetrisPageState::kEmpty;
        }
    }
    if (cleared > 0)
    {
        st->lines += static_cast<unsigned int>(cleared);
        // Classic-style scoring: more points for multi-line clears.
        static const unsigned int kLineScore[5] = {0, 100, 300, 500, 800};
        st->score += kLineScore[cleared];
        tetris_update_score(st);
    }
}

void tetris_lock_piece(TetrisPageState* st)
{
    for (int i = 0; i < 4; ++i)
    {
        const int x = st->piece_x + kTetrominoes[st->piece][st->rotation][i].x;
        const int y = st->piece_y + kTetrominoes[st->piece][st->rotation][i].y;
        if (x >= 0 && x < TetrisPageState::kCols && y >= 0 && y < TetrisPageState::kRows)
        {
            st->board[tetris_cell_index(x, y)] = static_cast<int8_t>(st->piece);
        }
    }
    tetris_clear_lines(st);
    tetris_spawn(st);
}

// Try to step the active piece down one row. Returns true if it moved, false if
// it landed (and was therefore locked + a new piece spawned).
bool tetris_step_down(TetrisPageState* st)
{
    if (st->game_over)
    {
        return false;
    }
    if (!tetris_collides(st, st->piece, st->rotation, st->piece_x, st->piece_y + 1))
    {
        st->piece_y += 1;
        return true;
    }
    tetris_lock_piece(st);
    return false;
}

void tetris_tick(lv_timer_t* timer)
{
    auto* st = static_cast<TetrisPageState*>(lv_timer_get_user_data(timer));
    if (!st || st->game_over)
    {
        return;
    }
    tetris_step_down(st);
    tetris_render(st);
}

void tetris_move(TetrisPageState* st, int dx)
{
    if (!st || st->game_over)
    {
        return;
    }
    if (!tetris_collides(st, st->piece, st->rotation, st->piece_x + dx, st->piece_y))
    {
        st->piece_x += dx;
        tetris_render(st);
    }
}

void tetris_rotate(TetrisPageState* st)
{
    if (!st || st->game_over)
    {
        return;
    }
    const int new_rot = (st->rotation + 1) & 3;
    // Basic wall-kick: try in place, then nudge left/right by 1 or 2 cells.
    static const int kKicks[5] = {0, -1, 1, -2, 2};
    for (int k = 0; k < 5; ++k)
    {
        if (!tetris_collides(st, st->piece, new_rot, st->piece_x + kKicks[k], st->piece_y))
        {
            st->piece_x += kKicks[k];
            st->rotation = new_rot;
            tetris_render(st);
            return;
        }
    }
}

void tetris_soft_drop(TetrisPageState* st)
{
    if (!st || st->game_over)
    {
        return;
    }
    if (tetris_step_down(st))
    {
        st->score += 1;  // small reward for soft drop
        tetris_update_score(st);
    }
    tetris_render(st);
}

void tetris_hard_drop(TetrisPageState* st)
{
    if (!st || st->game_over)
    {
        return;
    }
    int dropped = 0;
    while (!tetris_collides(st, st->piece, st->rotation, st->piece_x, st->piece_y + 1))
    {
        st->piece_y += 1;
        ++dropped;
    }
    st->score += static_cast<unsigned int>(dropped * 2);  // hard-drop reward
    tetris_update_score(st);
    tetris_lock_piece(st);
    tetris_render(st);
}

void tetris_reset(TetrisPageState* st)
{
    for (int i = 0; i < TetrisPageState::kCellCount; ++i)
    {
        st->board[i] = TetrisPageState::kEmpty;
    }
    st->score = 0;
    st->lines = 0;
    st->game_over = false;
    st->next_piece = static_cast<int>(tetris_rand(st) % 7u);
    tetris_spawn(st);
    tetris_update_score(st);
    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        lv_label_set_text(st->status_label, "");
    }
    if (st->timer)
    {
        lv_timer_set_period(st->timer, TetrisPageState::kBasePeriodMs);
        lv_timer_resume(st->timer);
    }
    tetris_render(st);
}

lv_obj_t* tetris_make_button(lv_obj_t* parent,
                             TetrisPageState* st,
                             const char* text,
                             lv_event_cb_t cb)
{
    // Large touch target (84x72) bound to PRESSED so Left/Right/Rotate/Drop act
    // the instant the finger lands and repaint at once (gravity stays on the
    // timer). The whole button is the hitbox, not just the glyph.
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 84, 72);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, st);
    return btn;
}

void tetris_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<TetrisPageState*>(user_data);
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
    lv_obj_set_style_pad_all(state->root, TetrisPageState::kRootPad, 0);
    lv_obj_set_flex_flow(state->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->root, 8, 0);
    // Vertically scrollable as a safety net: the 880px board plus the control pad
    // can exceed the 1168px height, so allow the page to be dragged on Y to keep
    // every control reachable (same pattern as the Help page).
    lv_obj_set_scroll_dir(state->root, LV_DIR_VER);
    lv_obj_add_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // Top row: Back button + score/lines label.
    lv_obj_t* top = lv_obj_create(state->root);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* back_btn = lv_button_create(top);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    state->score_label = lv_label_create(top);
    lv_obj_set_style_text_color(state->score_label, ui::theme::text(), 0);
    lv_obj_set_style_text_font(state->score_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(state->score_label, "Score 0  Lines 0");

    // Status (Game Over) line.
    state->status_label = lv_label_create(state->root);
    lv_obj_set_style_text_color(state->status_label, ui::theme::error(), 0);
    lv_obj_set_style_text_font(state->status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(state->status_label, "");

    // Play board: kCols x kRows cell rectangles, positioned absolutely (created
    // once, recolored each tick).
    const int board_w = TetrisPageState::kCols * TetrisPageState::kCellPx;
    const int board_h = TetrisPageState::kRows * TetrisPageState::kCellPx;
    lv_obj_t* board = lv_obj_create(state->root);
    lv_obj_set_size(board, board_w, board_h);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(board, 1, 0);
    lv_obj_set_style_border_color(board, ui::theme::border(), 0);
    lv_obj_set_style_radius(board, 0, 0);
    lv_obj_set_style_pad_all(board, 0, 0);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);

    for (int y = 0; y < TetrisPageState::kRows; ++y)
    {
        for (int x = 0; x < TetrisPageState::kCols; ++x)
        {
            lv_obj_t* cell = lv_obj_create(board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, TetrisPageState::kCellPx - 1, TetrisPageState::kCellPx - 1);
            lv_obj_set_pos(cell, x * TetrisPageState::kCellPx, y * TetrisPageState::kCellPx);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x222222), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(cell, 1, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            state->cells[tetris_cell_index(x, y)] = cell;
        }
    }

    // Controls row 1: Left, Rotate, Right. All act immediately on PRESSED and
    // repaint at once (see tetris_make_button); only gravity is on the timer.
    lv_obj_t* row1 = lv_obj_create(state->root);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, 12, 0);
    tetris_make_button(
        row1, state, LV_SYMBOL_LEFT,
        [](lv_event_t* e)
        { tetris_move(static_cast<TetrisPageState*>(lv_event_get_user_data(e)), -1); });
    tetris_make_button(
        row1, state, LV_SYMBOL_REFRESH,
        [](lv_event_t* e)
        { tetris_rotate(static_cast<TetrisPageState*>(lv_event_get_user_data(e))); });
    tetris_make_button(
        row1, state, LV_SYMBOL_RIGHT,
        [](lv_event_t* e)
        { tetris_move(static_cast<TetrisPageState*>(lv_event_get_user_data(e)), 1); });

    // Controls row 2: Soft drop (Down) and Hard drop. The Down button soft-drops
    // one row instantly on PRESSED and ALSO speeds gravity to kSoftDropPeriodMs
    // while held; RELEASED restores the base gravity so it behaves like a real
    // "hold to drop faster" key. Hard drop slams the piece down and locks it.
    lv_obj_t* row2 = lv_obj_create(state->root);
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, 12, 0);

    // Down (soft-drop): immediate drop on press + faster gravity while held.
    lv_obj_t* down_btn = lv_button_create(row2);
    lv_obj_set_size(down_btn, 84, 72);
    lv_obj_t* down_lbl = lv_label_create(down_btn);
    lv_obj_set_style_text_font(down_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(down_lbl, LV_SYMBOL_DOWN);
    lv_obj_center(down_lbl);
    lv_obj_add_event_cb(
        down_btn,
        [](lv_event_t* e)
        {
            auto* st = static_cast<TetrisPageState*>(lv_event_get_user_data(e));
            if (st && st->timer && !st->game_over)
            {
                lv_timer_set_period(st->timer, TetrisPageState::kSoftDropPeriodMs);
            }
            tetris_soft_drop(st);
        },
        LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(
        down_btn,
        [](lv_event_t* e)
        {
            auto* st = static_cast<TetrisPageState*>(lv_event_get_user_data(e));
            if (st && st->timer)
            {
                lv_timer_set_period(st->timer, TetrisPageState::kBasePeriodMs);
            }
        },
        LV_EVENT_RELEASED, state);
    // Also restore base gravity if the press is lost (finger slides off / scroll
    // steals the gesture) so gravity can never get stuck at the fast soft-drop rate.
    lv_obj_add_event_cb(
        down_btn,
        [](lv_event_t* e)
        {
            auto* st = static_cast<TetrisPageState*>(lv_event_get_user_data(e));
            if (st && st->timer)
            {
                lv_timer_set_period(st->timer, TetrisPageState::kBasePeriodMs);
            }
        },
        LV_EVENT_PRESS_LOST, state);

    tetris_make_button(
        row2, state, LV_SYMBOL_DOWNLOAD,
        [](lv_event_t* e)
        { tetris_hard_drop(static_cast<TetrisPageState*>(lv_event_get_user_data(e))); });

    // Restart button.
    lv_obj_t* restart_btn = lv_button_create(state->root);
    lv_obj_t* restart_lbl = lv_label_create(restart_btn);
    lv_label_set_text(restart_lbl, LV_SYMBOL_REFRESH " Restart");
    lv_obj_center(restart_lbl);
    lv_obj_add_event_cb(
        restart_btn,
        [](lv_event_t* e)
        { tetris_reset(static_cast<TetrisPageState*>(lv_event_get_user_data(e))); },
        LV_EVENT_CLICKED, state);

    // Seed the LCG from the tick counter, then start the game and the drop timer.
    // ~550ms/row gravity; the Down button does an immediate soft-drop and speeds
    // this up while held (restored on release).
    state->rng = lv_tick_get() ^ 0x2545F491u;
    state->timer = lv_timer_create(tetris_tick, TetrisPageState::kBasePeriodMs, state);
    tetris_reset(state);
}

void tetris_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<TetrisPageState*>(user_data);
    if (!state)
    {
        return;
    }
    if (state->timer)
    {
        lv_timer_del(state->timer);
        state->timer = nullptr;
    }
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->score_label = nullptr;
        state->status_label = nullptr;
        for (int i = 0; i < TetrisPageState::kCellCount; ++i)
        {
            state->cells[i] = nullptr;
        }
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->score_label = nullptr;
    state->status_label = nullptr;
    for (int i = 0; i < TetrisPageState::kCellCount; ++i)
    {
        state->cells[i] = nullptr;
    }
}

ui::CallbackAppScreen s_tetris_app("tetris", "Tetris", &Chat, tetris_enter, tetris_exit,
                                   &s_tetris_state);

// ---------------------------------------------------------------------------
// Help: a fully self-contained how-to / Help guide, built inline the same way as
// the C6 Companion app above (file-static state, enter()/exit() that build/tear-
// down a root sized LV_PCT(100), and a CallbackAppScreen added to s_apps[]). It
// holds no timer and no game state -- just a vertically scrollable flex-column
// page of clearly-titled, word-wrapped text sections describing the launcher and
// every app. The boot self-test enters then immediately exits this screen once;
// help_exit therefore just deletes the root with the same null/validity guards
// companion_exit uses so enter-then-exit is clean and leak-free.
// ---------------------------------------------------------------------------
struct HelpPageState
{
    lv_obj_t* root = nullptr;
};

HelpPageState s_help_state;

void help_add_title(lv_obj_t* parent, const char* text)
{
    // Section heading: the large montserrat-24 font (same size the menu uses for
    // the battery percentage; compiled into this build) in the theme accent color,
    // with extra top spacing so each section reads as a titled block above its
    // body text rather than as one continuous wall of words.
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, ui::theme::accent(), 0);
    lv_obj_set_style_pad_top(label, 18, 0);
}

void help_add_body(lv_obj_t* parent, const char* text)
{
    // Body paragraph: word-wrapped at ~100% width, in the standard text color, at
    // the same large montserrat-24 size as the headings so the whole page is easy
    // to read at arm's length on the device.
    add_label(parent, text, &lv_font_montserrat_24, ui::theme::text());
}

void help_add_page_title(lv_obj_t* parent, const char* text)
{
    // Page title: same large montserrat-24 accent styling as a section heading but
    // without the section's big top-pad, since it sits directly under the Back
    // button at the very top of the scroll view.
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, ui::theme::accent(), 0);
}

void help_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<HelpPageState*>(user_data);
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
    // Vertically scrollable: the content is taller than the screen, so allow the
    // page to scroll on the Y axis (touch drag) and keep it within the X bound.
    lv_obj_set_scroll_dir(state->root, LV_DIR_VER);
    lv_obj_add_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // Back button: routes to the launcher menu via the same exit the Chat app
    // uses (exactly like companion_enter's back button).
    lv_obj_t* back_btn = lv_button_create(state->root);
    lv_obj_set_width(back_btn, LV_PCT(45));
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    // Page title.
    help_add_page_title(state->root, "Trail Mate Help");

    // (1) Quick start.
    help_add_title(state->root, "Quick start");
    help_add_body(state->root,
                  "Power on and the menu shows the apps. Open Chat to message, Contacts to see "
                  "nearby nodes, and Settings to set your channel. Tap Back on any page to "
                  "return to the menu.");

    // (2) What this device is.
    help_add_title(state->root, "What this device is");
    help_add_body(state->root,
                  "A LilyGo T-Display P4 touchscreen running the Trail Mate launcher, a "
                  "multi-protocol off-grid mesh communicator and toolkit (Meshtastic, MeshCore, "
                  "LXMF, R-Node Bridge). It drives the LoRa radios directly; it is not the "
                  "original TrailMate phone app.");

    // (3) The apps, one tight line each.
    help_add_title(state->root, "The apps");
    help_add_body(state->root, "Chat: mesh messages.");
    help_add_body(state->root,
                  "Contacts: nearby nodes. Nearby auto-updates; Broadcast ID announces you "
                  "instantly.");
    help_add_body(state->root,
                  "Settings: device, radio, channel, GPS. Channel keys accept a passphrase. "
                  "Switch the mesh protocol under Settings > Chat > Protocol.");
    help_add_body(state->root, "Satellites: GNSS sky-plot.");
    help_add_body(state->root, "Tracker: GPS tracks to SD.");
    help_add_body(state->root, "Sub-GHz Scan: LoRa spectrum sweep.");
    help_add_body(state->root, "PC Link: USB / KISS modem.");
    help_add_body(state->root, "Extensions: Wi-Fi / language packs.");
    help_add_body(state->root, "C6 Companion: ESP32-C6 status.");
    help_add_body(state->root, "Games: Snake, Tetris.");

    // (4) Switching the mesh protocol (Settings > Chat > Protocol).
    help_add_title(state->root, "Switching protocols");
    help_add_body(state->root,
                  "This device can run several mesh protocols. To switch, open Settings, choose "
                  "Chat, then Protocol, and pick one: Meshtastic, MeshCore, LXMF, or R-Node "
                  "Bridge. Meshtastic and MeshCore are the two main mesh options; LXMF and "
                  "R-Node Bridge are also available.");

    // (5) Tips and tricks.
    help_add_title(state->root, "Tips and tricks");
    help_add_body(state->root,
                  "Use a memorable passphrase instead of a raw hex channel key.");
    help_add_body(state->root,
                  "Tap Broadcast ID on Contacts to appear on a nearby unit immediately.");
    help_add_body(state->root,
                  "The Back button returns you to the menu from anywhere.");
}

void help_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<HelpPageState*>(user_data);
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

ui::CallbackAppScreen s_help_app("help", "Help", &Setting, help_enter, help_exit, &s_help_state);

// ---------------------------------------------------------------------------
// System Test: a fully self-contained hardware self-test / diagnostics app,
// built inline the same way as the C6 Companion / Snake / Tetris / Help apps
// above (file-static state, enter()/exit() that build/tear-down a root sized
// LV_PCT(100), and a CallbackAppScreen added to s_apps[]). stable_id = "systest"
// so the boot self-test logs appselftest:systest:ok when it enters then
// immediately exits this screen once.
//
// The display + touch already work on this device, so this app REUSES the live
// LVGL display + touch indev: it never initialises any display, touch, or
// peripheral driver. The menu page is a scrollable flex column (like the Help
// page) with a Back button and three test tiles, each opening a full-screen
// sub-view (built as a child of the same app `parent`, so it covers the menu)
// that carries its own Back-to-test-menu control:
//   1. Color test  - a full-screen solid-color overlay on the top layer that
//                     cycles Red->Green->Blue->White->Black->Gradient on tap,
//                     then deletes itself and returns to the test menu.
//   2. Touch test  - an lv_canvas (buffer in PSRAM) that draws a red line under
//                     the finger via LV_EVENT_PRESSING + lv_indev_get_point.
//   3. Info readout- a panel of labels (battery / free heap / free PSRAM / a
//                     static panel line).
//
// systest_exit MUST leave nothing behind even when only enter()+exit() run (the
// boot self-test): it deletes the color overlay if open, frees the canvas PSRAM
// buffer if allocated (heap_caps_free) and clears the pointer, and deletes the
// root with the same null/validity guards companion_exit/snake_exit use. The
// canvas buffer is also freed in the touch sub-view's own close handler, so a
// leak cannot accumulate across repeated open/close of the touch test.
// ---------------------------------------------------------------------------
struct SystestPageState
{
    // Drawing surface size for the touch test. A full 540x1168 RGB565 canvas is
    // 540*1168*2 = ~1.26 MB; the panel has PSRAM so that fits, but we size the
    // surface to most of the screen (540x900) to leave room for the Back/Clear
    // controls above it. The buffer is still allocated from PSRAM.
    static constexpr int kCanvasW = 540;
    static constexpr int kCanvasH = 900;

    lv_obj_t* parent = nullptr;  // the app container; sub-views are children of it
    lv_obj_t* root = nullptr;    // the scrollable test menu page

    lv_obj_t* sub_view = nullptr;  // active full-screen sub-view (touch or info)

    // Color test: a full-screen overlay on the top layer + the cycle index.
    lv_obj_t* color_overlay = nullptr;
    int color_index = 0;

    // Touch test: the canvas + its PSRAM buffer + the last drawn point.
    lv_obj_t* canvas = nullptr;
    void* canvas_buf = nullptr;
    int32_t last_x = 0;
    int32_t last_y = 0;
    bool have_last = false;
};

SystestPageState s_systest_state;

// Free the touch-test canvas PSRAM buffer (if allocated) and clear the pointers.
// Safe to call repeatedly; called from the touch sub-view close handler AND from
// systest_exit so the boot self-test enter+exit can never leak the buffer.
void systest_free_canvas(SystestPageState* st)
{
    if (!st)
    {
        return;
    }
    st->canvas = nullptr;  // owned by (and deleted with) the sub-view tree
    st->have_last = false;
    if (st->canvas_buf)
    {
        heap_caps_free(st->canvas_buf);
        st->canvas_buf = nullptr;
    }
}

// ---- Color test -----------------------------------------------------------
// Six steps: Red, Green, Blue, White, Black, then a vertical Black->White
// gradient. The seventh tap deletes the overlay and returns to the test menu.
void systest_color_apply(SystestPageState* st)
{
    if (!st || !st->color_overlay || !lv_obj_is_valid(st->color_overlay))
    {
        return;
    }
    // A flat solid color via bg color (no raw framebuffer writes), or, on the
    // final step, a vertical gradient.
    lv_color_t c = lv_color_hex(0x000000);
    bool gradient = false;
    switch (st->color_index)
    {
    case 0: c = lv_color_hex(0xFF0000); break;  // Red
    case 1: c = lv_color_hex(0x00FF00); break;  // Green
    case 2: c = lv_color_hex(0x0000FF); break;  // Blue
    case 3: c = lv_color_hex(0xFFFFFF); break;  // White
    case 4: c = lv_color_hex(0x000000); break;  // Black
    default: gradient = true; break;            // vertical gradient
    }
    if (!gradient)
    {
        lv_obj_set_style_bg_grad_dir(st->color_overlay, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_color(st->color_overlay, c, 0);
        lv_obj_set_style_bg_opa(st->color_overlay, LV_OPA_COVER, 0);
    }
    else
    {
        // Vertical gradient from black (top) to white (bottom) as a smooth ramp
        // to spot banding / uneven backlight, still via lv_obj bg styling.
        lv_obj_set_style_bg_color(st->color_overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_grad_color(st->color_overlay, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_grad_dir(st->color_overlay, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(st->color_overlay, LV_OPA_COVER, 0);
    }
}

void systest_color_close(SystestPageState* st)
{
    if (st && st->color_overlay)
    {
        if (lv_obj_is_valid(st->color_overlay))
        {
            lv_obj_del(st->color_overlay);
        }
        st->color_overlay = nullptr;
    }
}

void systest_open_color(SystestPageState* st)
{
    if (!st || (st->color_overlay && lv_obj_is_valid(st->color_overlay)))
    {
        return;
    }
    st->color_index = 0;
    // Full-screen overlay on the top layer so it covers everything (menu + status)
    // and receives taps directly. lv_obj covering the whole display; bg color is
    // set per step, no raw framebuffer access.
    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    st->color_overlay = ov;
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        ov,
        [](lv_event_t* e)
        {
            auto* s = static_cast<SystestPageState*>(lv_event_get_user_data(e));
            if (!s)
            {
                return;
            }
            s->color_index += 1;
            if (s->color_index >= 6)
            {
                // Past the last (gradient) step: tear the overlay down and return
                // to the test menu.
                systest_color_close(s);
                return;
            }
            systest_color_apply(s);
        },
        LV_EVENT_CLICKED, st);
    systest_color_apply(st);
}

// ---- Touch test -----------------------------------------------------------
// Draw a red line segment from the last finger point to the new one on every
// LV_EVENT_PRESSING. Points come from the live touch indev; they are display
// coordinates, so translate into canvas-local pixels via the canvas's coords.
void systest_canvas_pressing(lv_event_t* e)
{
    auto* st = static_cast<SystestPageState*>(lv_event_get_user_data(e));
    if (!st || !st->canvas || !lv_obj_is_valid(st->canvas))
    {
        return;
    }
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev)
    {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    // Translate display coords -> canvas-local coords using the canvas position.
    lv_area_t coords;
    lv_obj_get_coords(st->canvas, &coords);
    const int32_t cx = p.x - coords.x1;
    const int32_t cy = p.y - coords.y1;
    if (cx < 0 || cy < 0 || cx >= SystestPageState::kCanvasW ||
        cy >= SystestPageState::kCanvasH)
    {
        // Finger is outside the drawing surface: drop the trail so the next
        // in-bounds press starts a fresh stroke instead of a long jump-line.
        st->have_last = false;
        return;
    }

    if (st->have_last)
    {
        lv_layer_t layer;
        lv_canvas_init_layer(st->canvas, &layer);
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_hex(0xFF0000);
        line.width = 4;
        line.round_start = 1;
        line.round_end = 1;
        line.p1.x = st->last_x;
        line.p1.y = st->last_y;
        line.p2.x = cx;
        line.p2.y = cy;
        lv_draw_line(&layer, &line);
        lv_canvas_finish_layer(st->canvas, &layer);
    }
    st->last_x = cx;
    st->last_y = cy;
    st->have_last = true;
}

void systest_close_sub_view(SystestPageState* st)
{
    if (!st)
    {
        return;
    }
    // Free the canvas buffer first (if this was the touch view) so closing it can
    // never leak, then delete the sub-view tree and re-show the test menu.
    systest_free_canvas(st);
    if (st->sub_view)
    {
        if (lv_obj_is_valid(st->sub_view))
        {
            lv_obj_del(st->sub_view);
        }
        st->sub_view = nullptr;
    }
    if (st->root && lv_obj_is_valid(st->root))
    {
        lv_obj_clear_flag(st->root, LV_OBJ_FLAG_HIDDEN);
    }
}

// Build the standard "Back to test" bar at the top of a sub-view. Returns the
// bar so the caller can add more controls (e.g. a Clear button) to the right.
lv_obj_t* systest_sub_view_bar(SystestPageState* st, lv_obj_t* sub, const char* title)
{
    lv_obj_t* bar = lv_obj_create(sub);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 12, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_button_create(bar);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Test");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn,
        [](lv_event_t* e)
        { systest_close_sub_view(static_cast<SystestPageState*>(lv_event_get_user_data(e))); },
        LV_EVENT_CLICKED, st);

    if (title)
    {
        lv_obj_t* lbl = lv_label_create(bar);
        lv_obj_set_style_text_color(lbl, ui::theme::text(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_label_set_text(lbl, title);
    }
    return bar;
}

// Create a full-screen sub-view as a child of the app parent (covering the menu).
// Hides the menu root while it is shown. Returns the sub-view root.
lv_obj_t* systest_make_sub_view(SystestPageState* st)
{
    if (!st || !st->parent)
    {
        return nullptr;
    }
    if (st->root && lv_obj_is_valid(st->root))
    {
        lv_obj_add_flag(st->root, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t* sub = lv_obj_create(st->parent);
    st->sub_view = sub;
    lv_obj_set_size(sub, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(sub, ui::theme::white(), 0);
    lv_obj_set_style_bg_opa(sub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sub, 0, 0);
    lv_obj_set_style_radius(sub, 0, 0);
    lv_obj_set_style_pad_all(sub, 10, 0);
    lv_obj_set_flex_flow(sub, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sub, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(sub, 8, 0);
    return sub;
}

void systest_open_touch(SystestPageState* st)
{
    if (!st)
    {
        return;
    }
    lv_obj_t* sub = systest_make_sub_view(st);
    if (!sub)
    {
        return;
    }

    lv_obj_t* bar = systest_sub_view_bar(st, sub, "Touch");
    // Clear button on the bar: refill the canvas bg and drop the trail.
    lv_obj_t* clear_btn = lv_button_create(bar);
    lv_obj_t* clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, LV_SYMBOL_TRASH " Clear");
    lv_obj_center(clear_lbl);
    lv_obj_add_event_cb(
        clear_btn,
        [](lv_event_t* e)
        {
            auto* s = static_cast<SystestPageState*>(lv_event_get_user_data(e));
            if (s && s->canvas && lv_obj_is_valid(s->canvas))
            {
                lv_canvas_fill_bg(s->canvas, lv_color_hex(0xDDDDDD), LV_OPA_COVER);
                s->have_last = false;
            }
        },
        LV_EVENT_CLICKED, st);

    // Allocate the canvas buffer from PSRAM. RGB565 -> 2 bytes/px. If the alloc
    // fails (low PSRAM) show a label instead of crashing.
    const size_t buf_size =
        static_cast<size_t>(SystestPageState::kCanvasW) * SystestPageState::kCanvasH *
        sizeof(lv_color_t);
    st->canvas_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!st->canvas_buf)
    {
        lv_obj_t* err = lv_label_create(sub);
        lv_obj_set_style_text_color(err, ui::theme::error(), 0);
        lv_obj_set_style_text_font(err, &lv_font_montserrat_14, 0);
        lv_label_set_text(err, "Touch test: PSRAM alloc failed");
        return;
    }

    st->canvas = lv_canvas_create(sub);
    lv_canvas_set_buffer(st->canvas, st->canvas_buf, SystestPageState::kCanvasW,
                         SystestPageState::kCanvasH, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(st->canvas, lv_color_hex(0xDDDDDD), LV_OPA_COVER);
    lv_obj_set_style_border_width(st->canvas, 1, 0);
    lv_obj_set_style_border_color(st->canvas, ui::theme::border(), 0);
    lv_obj_clear_flag(st->canvas, LV_OBJ_FLAG_SCROLLABLE);
    // The canvas itself must receive press events; bind PRESSING to draw.
    lv_obj_add_flag(st->canvas, LV_OBJ_FLAG_CLICKABLE);
    st->have_last = false;
    lv_obj_add_event_cb(st->canvas, systest_canvas_pressing, LV_EVENT_PRESSING, st);
}

// ---- Info readout ---------------------------------------------------------
void systest_open_info(SystestPageState* st)
{
    if (!st)
    {
        return;
    }
    lv_obj_t* sub = systest_make_sub_view(st);
    if (!sub)
    {
        return;
    }
    systest_sub_view_bar(st, sub, "Info");

    // A panel of labels. Battery uses the SAME source the menu uses
    // (platform::ui::device::battery_info(); see menu_runtime.cpp
    // refreshBatteryLabel). Heap/PSRAM are read live at open time.
    lv_obj_t* panel = lv_obj_create(sub);
    lv_obj_set_size(panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, ui::theme::surface(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, ui::theme::border(), 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 6, 0);

    const platform::ui::device::BatteryInfo battery = platform::ui::device::battery_info();
    char batt_buf[48];
    if (battery.level < 0)
    {
        std::snprintf(batt_buf, sizeof(batt_buf), "%s",
                      battery.charging ? "USB (charging)" : "unknown");
    }
    else
    {
        std::snprintf(batt_buf, sizeof(batt_buf), "%d%%%s", battery.level,
                      battery.charging ? " (charging)" : "");
    }
    add_status_line(panel, "Battery", batt_buf);

    add_u32_line(panel, "Free heap",
                 static_cast<unsigned long>(esp_get_free_heap_size()));
    add_u32_line(panel, "Free PSRAM",
                 static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    add_status_line(panel, "Display", "HI8561 540x1168 RGB565");
}

// ---- Test menu ------------------------------------------------------------
lv_obj_t* systest_make_tile(lv_obj_t* parent,
                            SystestPageState* st,
                            const char* text,
                            lv_event_cb_t cb)
{
    // A wide button tile (full width) that opens a sub-view on click. The whole
    // tile is the hitbox.
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_style_pad_top(btn, 16, 0);
    lv_obj_set_style_pad_bottom(btn, 16, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, st);
    return btn;
}

void systest_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<SystestPageState*>(user_data);
    if (!state || !parent || (state->root && lv_obj_is_valid(state->root)))
    {
        return;
    }

    state->parent = parent;
    state->sub_view = nullptr;
    state->color_overlay = nullptr;
    state->color_index = 0;
    state->canvas = nullptr;
    state->canvas_buf = nullptr;
    state->have_last = false;

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
    lv_obj_set_style_pad_row(state->root, 10, 0);
    // Vertically scrollable like the Help page so every tile stays reachable.
    lv_obj_set_scroll_dir(state->root, LV_DIR_VER);
    lv_obj_add_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // Back button: routes to the launcher menu via the same exit the Chat app
    // uses (exactly like companion_enter/help_enter).
    lv_obj_t* back_btn = lv_button_create(state->root);
    lv_obj_set_width(back_btn, LV_PCT(45));
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    // Page title.
    lv_obj_t* title = lv_label_create(state->root);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, ui::theme::accent(), 0);
    lv_label_set_text(title, "System Test");

    add_label(state->root,
              "Hardware self-test. Pick a test below.",
              &lv_font_montserrat_14,
              ui::theme::text_muted());

    // Three test tiles, each opening its sub-view.
    systest_make_tile(
        state->root, state, "Color Test (dead pixel / color)",
        [](lv_event_t* e)
        { systest_open_color(static_cast<SystestPageState*>(lv_event_get_user_data(e))); });
    systest_make_tile(
        state->root, state, "Touch Test (draw on touch)",
        [](lv_event_t* e)
        { systest_open_touch(static_cast<SystestPageState*>(lv_event_get_user_data(e))); });
    systest_make_tile(
        state->root, state, "Info Readout",
        [](lv_event_t* e)
        { systest_open_info(static_cast<SystestPageState*>(lv_event_get_user_data(e))); });
}

void systest_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<SystestPageState*>(user_data);
    if (!state)
    {
        return;
    }
    // Tear down the color overlay (lives on the top layer, not under root).
    systest_color_close(state);
    // Free the canvas PSRAM buffer if the touch test allocated it (also frees on
    // the touch view's own Back, so this covers enter->open-touch->exit and the
    // boot self-test's bare enter->exit).
    systest_free_canvas(state);
    // Delete any open sub-view (child of parent, a sibling of root).
    if (state->sub_view)
    {
        if (lv_obj_is_valid(state->sub_view))
        {
            lv_obj_del(state->sub_view);
        }
        state->sub_view = nullptr;
    }
    // Delete the root with companion_exit-style guards.
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->parent = nullptr;
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->parent = nullptr;
}

ui::CallbackAppScreen s_systest_app("systest", "System Test", &Setting, systest_enter,
                                    systest_exit, &s_systest_state);

// ---------------------------------------------------------------------------
// GPS Position: a fully self-contained NUMERIC GPS read-out, built inline the
// same way as the C6 Companion / Help / System Test apps above (file-static
// state, enter()/exit() that build/tear-down a root sized LV_PCT(100), and a
// CallbackAppScreen added to s_apps[]). stable_id = "gps_position" so the boot
// self-test logs appselftest:gps_position:ok when it enters then immediately
// exits this screen once.
//
// Unlike the Map (screens/gps) and Satellites (screens/gnss) apps, this is a
// plain scrollable column of text labels: one line each for Latitude, Longitude,
// Altitude, Speed, Heading/Course, Fix status, HDOP, Satellites (in use / in
// view), and Last-fix age. The values are read live from the SAME producers the
// sky-plot and map apps use: platform::ui::gps::get_data() (a gps::GpsState with
// lat/lng/alt_m+has_alt/speed_mps+has_speed/course_deg+has_course/satellites/
// valid/age) and platform::ui::gps::get_gnss_snapshot(out,max,&count,&status)
// (which fills a gps::GnssStatus with sats_in_use/sats_in_view/hdop/fix). Both
// are already compiled into this build via platform_ui_gps_runtime.cpp in the
// PLATFORM cmake block, so no new producer is needed.
//
// The labels are created once in enter() and then refreshed every ~1000 ms by an
// lv_timer (gps_position_tick). The producer degrades gracefully with no fix
// (valid==false, has_* flags false), in which case the lines show "--", so the
// boot self-test (which has no satellite fix) still enters cleanly. gps_position_
// exit deletes the timer and the root with the same null/validity guards
// companion_exit/snake_exit use, so the boot self-test's bare enter->exit leaves
// nothing dangling.
// ---------------------------------------------------------------------------
struct GpsPositionPageState
{
    lv_obj_t* root = nullptr;
    lv_timer_t* timer = nullptr;

    // One value label per metric; created once in enter(), updated each tick.
    lv_obj_t* lat_label = nullptr;
    lv_obj_t* lng_label = nullptr;
    lv_obj_t* alt_label = nullptr;
    lv_obj_t* speed_label = nullptr;
    lv_obj_t* course_label = nullptr;
    lv_obj_t* fix_label = nullptr;
    lv_obj_t* hdop_label = nullptr;
    lv_obj_t* sats_label = nullptr;
    lv_obj_t* age_label = nullptr;
};

GpsPositionPageState s_gps_position_state;

// A labeled metric row: a "<title>" caption in the muted color above a value
// label (returned) that the tick handler rewrites. Both wrap at full width so
// long values never clip on the 540px portrait panel.
lv_obj_t* gps_position_make_row(lv_obj_t* parent, const char* title)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* caption = lv_label_create(row);
    lv_label_set_text(caption, title ? title : "");
    lv_obj_set_width(caption, LV_PCT(100));
    lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(caption, ui::theme::text_muted(), 0);

    lv_obj_t* value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_width(value, LV_PCT(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(value, ui::theme::text(), 0);
    return value;
}

const char* gps_position_fix_name(gps::GnssFix fix)
{
    switch (fix)
    {
    case gps::GnssFix::FIX2D: return "2D fix";
    case gps::GnssFix::FIX3D: return "3D fix";
    case gps::GnssFix::NOFIX:
    default: return "No fix";
    }
}

void gps_position_set(lv_obj_t* label, const char* text)
{
    if (label && lv_obj_is_valid(label))
    {
        lv_label_set_text(label, text ? text : "--");
    }
}

void gps_position_refresh(GpsPositionPageState* st)
{
    if (!st)
    {
        return;
    }

    const gps::GpsState fix = platform::ui::gps::get_data();

    // GNSS status (fix kind / HDOP / sats-in-view). get_gnss_snapshot also fills a
    // satellite array we do not display here, but the API requires the buffer; the
    // in-view count and the in-use count both come from the GnssStatus it writes.
    gps::GnssSatInfo sats[gps::kMaxGnssSats]{};
    std::size_t sat_count = 0;
    gps::GnssStatus status{};
    const bool have_status =
        platform::ui::gps::get_gnss_snapshot(sats, gps::kMaxGnssSats, &sat_count, &status);

    char buf[96];

    // Latitude / Longitude: 6 decimal places (~0.11 m) when a fix is valid.
    if (fix.valid)
    {
        std::snprintf(buf, sizeof(buf), "%.6f deg", fix.lat);
        gps_position_set(st->lat_label, buf);
        std::snprintf(buf, sizeof(buf), "%.6f deg", fix.lng);
        gps_position_set(st->lng_label, buf);
    }
    else
    {
        gps_position_set(st->lat_label, "--");
        gps_position_set(st->lng_label, "--");
    }

    if (fix.has_alt)
    {
        std::snprintf(buf, sizeof(buf), "%.1f m", fix.alt_m);
        gps_position_set(st->alt_label, buf);
    }
    else
    {
        gps_position_set(st->alt_label, "--");
    }

    if (fix.has_speed)
    {
        std::snprintf(buf, sizeof(buf), "%.2f m/s", fix.speed_mps);
        gps_position_set(st->speed_label, buf);
    }
    else
    {
        gps_position_set(st->speed_label, "--");
    }

    if (fix.has_course)
    {
        std::snprintf(buf, sizeof(buf), "%.1f deg", fix.course_deg);
        gps_position_set(st->course_label, buf);
    }
    else
    {
        gps_position_set(st->course_label, "--");
    }

    // Fix status: prefer the richer GNSS fix kind when the snapshot is available,
    // otherwise fall back to the GpsState valid flag.
    if (have_status)
    {
        gps_position_set(st->fix_label, gps_position_fix_name(status.fix));
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(status.hdop));
        gps_position_set(st->hdop_label, buf);
        std::snprintf(buf, sizeof(buf), "%u in use / %u in view",
                      static_cast<unsigned>(status.sats_in_use),
                      static_cast<unsigned>(status.sats_in_view));
        gps_position_set(st->sats_label, buf);
    }
    else
    {
        gps_position_set(st->fix_label, fix.valid ? "Fix" : "No fix");
        gps_position_set(st->hdop_label, "--");
        std::snprintf(buf, sizeof(buf), "%u in use / -- in view",
                      static_cast<unsigned>(fix.satellites));
        gps_position_set(st->sats_label, buf);
    }

    // Last-fix age: milliseconds reported by the producer, shown as seconds.
    if (fix.valid)
    {
        std::snprintf(buf, sizeof(buf), "%.1f s ago",
                      static_cast<double>(fix.age) / 1000.0);
        gps_position_set(st->age_label, buf);
    }
    else
    {
        gps_position_set(st->age_label, "--");
    }
}

void gps_position_tick(lv_timer_t* timer)
{
    auto* st = static_cast<GpsPositionPageState*>(lv_timer_get_user_data(timer));
    gps_position_refresh(st);
}

void gps_position_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<GpsPositionPageState*>(user_data);
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
    lv_obj_set_style_pad_row(state->root, 10, 0);
    // Vertically scrollable: the nine metric rows are taller than the screen, so
    // allow the page to be dragged on Y (same pattern as the Help page).
    lv_obj_set_scroll_dir(state->root, LV_DIR_VER);
    lv_obj_add_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // Back button: routes to the launcher menu via the same exit the Chat app
    // uses (exactly like companion_enter/help_enter/systest_enter).
    lv_obj_t* back_btn = lv_button_create(state->root);
    lv_obj_set_width(back_btn, LV_PCT(45));
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    // Page title.
    lv_obj_t* title = lv_label_create(state->root);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, ui::theme::accent(), 0);
    lv_label_set_text(title, "Position");

    // One value label per metric (created once, rewritten each tick).
    state->lat_label = gps_position_make_row(state->root, "Latitude");
    state->lng_label = gps_position_make_row(state->root, "Longitude");
    state->alt_label = gps_position_make_row(state->root, "Altitude (m)");
    state->speed_label = gps_position_make_row(state->root, "Speed (m/s)");
    state->course_label = gps_position_make_row(state->root, "Heading / Course (deg)");
    state->fix_label = gps_position_make_row(state->root, "Fix status");
    state->hdop_label = gps_position_make_row(state->root, "HDOP");
    state->sats_label = gps_position_make_row(state->root, "Satellites (in use / in view)");
    state->age_label = gps_position_make_row(state->root, "Last-fix age");

    // Populate immediately, then refresh once a second.
    gps_position_refresh(state);
    state->timer = lv_timer_create(gps_position_tick, 1000, state);
}

void gps_position_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<GpsPositionPageState*>(user_data);
    if (!state)
    {
        return;
    }
    if (state->timer)
    {
        lv_timer_del(state->timer);
        state->timer = nullptr;
    }
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->lat_label = nullptr;
        state->lng_label = nullptr;
        state->alt_label = nullptr;
        state->speed_label = nullptr;
        state->course_label = nullptr;
        state->fix_label = nullptr;
        state->hdop_label = nullptr;
        state->sats_label = nullptr;
        state->age_label = nullptr;
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->lat_label = nullptr;
    state->lng_label = nullptr;
    state->alt_label = nullptr;
    state->speed_label = nullptr;
    state->course_label = nullptr;
    state->fix_label = nullptr;
    state->hdop_label = nullptr;
    state->sats_label = nullptr;
    state->age_label = nullptr;
}

ui::CallbackAppScreen s_gps_position_app("gps_position", "Position", &Setting, gps_position_enter,
                                         gps_position_exit, &s_gps_position_state);

// ---------------------------------------------------------------------------
// 2048: a fully self-contained touch-only 2048 sliding-tile game, built inline
// the same way as the C6 Companion / Snake / Tetris apps above (file-static
// state, enter()/exit() that build/tear-down a root sized LV_PCT(100), and a
// CallbackAppScreen added to s_apps[]). stable_id = "g2048" so the boot self-test
// logs appselftest:g2048:ok when it enters then immediately exits this screen
// once. This app holds NO repeating timer (moves are event-driven), so g2048_exit
// only needs to lv_obj_del the root with the same null/validity guards
// companion_exit uses; there is nothing else to tear down.
//
// Board model: a flat 4x4 int array (0 = empty, otherwise the tile value: 2, 4,
// 8 ...). A move slides every tile toward the chosen edge and merges equal
// neighbours once each (classic 2048 rules), implemented by extracting each of
// the four lines in travel order, compacting + merging it, and writing it back.
// If the board changed, a new tile (2 at 90%, 4 at 10%) is spawned on a random
// empty cell and merged values are added to the score. Game over is declared when
// the board is full AND no orthogonally-adjacent equal pair exists (no legal move
// in any direction). Input comes from BOTH swipe gestures on the board
// (LV_EVENT_GESTURE on the root -> lv_indev_get_gesture_dir, exactly like
// watch_face.cpp) AND four large on-screen arrow buttons, so it is fully usable by
// touch alone. The 4x4 grid plus header and controls fit within the 540x1168
// portrait panel, so the root is intentionally NOT scrollable (a scroll container
// would swallow the swipe gesture before it could be read).
// ---------------------------------------------------------------------------
struct G2048PageState
{
    static constexpr int kSize = 4;                 // 4x4 board
    static constexpr int kCellCount = kSize * kSize;  // 16 tiles
    // Tile geometry: the 540px portrait panel minus the root's 2*kRootPad side
    // margins gives 540 - 2*16 = 508px for the board. With kTileGap between/around
    // tiles: 4 tiles + 5 gaps. 5*10 = 50px of gap -> (508 - 50)/4 = 114px tiles, so
    // the board fills the screen width and the digits are large and legible.
    static constexpr int kRootPad = 16;
    static constexpr int kScreenW = 540;
    static constexpr int kTileGap = 10;
    static constexpr int kBoardInner = kScreenW - 2 * kRootPad;  // = 508
    static constexpr int kTilePx =
        (kBoardInner - (kSize + 1) * kTileGap) / kSize;  // = 114
    static constexpr int kBoardPx = kSize * kTilePx + (kSize + 1) * kTileGap;  // = 506

    lv_obj_t* root = nullptr;
    lv_obj_t* score_label = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* cells[kCellCount] = {nullptr};         // tile background objects
    lv_obj_t* tile_labels[kCellCount] = {nullptr};   // number label inside each tile

    int board[kCellCount] = {0};  // 0 = empty, else the tile value (2,4,8,...)

    unsigned int score = 0;
    bool game_over = false;
    uint32_t rng = 0;  // small LCG state
};

G2048PageState s_g2048_state;

uint32_t g2048_rand(G2048PageState* st)
{
    // Numerical Recipes LCG; deterministic per-seed, no global state (same scheme
    // the Snake/Tetris apps use).
    st->rng = st->rng * 1664525u + 1013904223u;
    return st->rng;
}

int g2048_index(int x, int y)
{
    return y * G2048PageState::kSize + x;
}

// Classic 2048 tile colors keyed by value. Larger values fold back onto the
// 2048 color so very deep games still render. Returns the background; the text
// color is chosen separately (dark for the pale 2/4 tiles, light otherwise).
lv_color_t g2048_tile_color(int value)
{
    switch (value)
    {
    case 0: return lv_color_hex(0xCDC1B4);     // empty cell
    case 2: return lv_color_hex(0xEEE4DA);
    case 4: return lv_color_hex(0xEDE0C8);
    case 8: return lv_color_hex(0xF2B179);
    case 16: return lv_color_hex(0xF59563);
    case 32: return lv_color_hex(0xF67C5F);
    case 64: return lv_color_hex(0xF65E3B);
    case 128: return lv_color_hex(0xEDCF72);
    case 256: return lv_color_hex(0xEDCC61);
    case 512: return lv_color_hex(0xEDC850);
    case 1024: return lv_color_hex(0xEDC53F);
    case 2048: return lv_color_hex(0xEDC22E);
    default: return lv_color_hex(0x3C3A32);    // >2048: dark "super" tile
    }
}

lv_color_t g2048_text_color(int value)
{
    // The pale 2 and 4 tiles take the dark 2048 text color; everything else
    // (including empty, which has no label text anyway) takes the off-white.
    if (value == 2 || value == 4)
    {
        return lv_color_hex(0x776E65);
    }
    return lv_color_hex(0xF9F6F2);
}

void g2048_update_score(G2048PageState* st)
{
    if (st->score_label && lv_obj_is_valid(st->score_label))
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Score: %u", st->score);
        lv_label_set_text(st->score_label, buf);
    }
}

void g2048_render(G2048PageState* st)
{
    for (int i = 0; i < G2048PageState::kCellCount; ++i)
    {
        const int v = st->board[i];
        if (st->cells[i] && lv_obj_is_valid(st->cells[i]))
        {
            lv_obj_set_style_bg_color(st->cells[i], g2048_tile_color(v), 0);
        }
        if (st->tile_labels[i] && lv_obj_is_valid(st->tile_labels[i]))
        {
            if (v == 0)
            {
                lv_label_set_text(st->tile_labels[i], "");
            }
            else
            {
                char buf[12];
                std::snprintf(buf, sizeof(buf), "%d", v);
                lv_label_set_text(st->tile_labels[i], buf);
            }
            lv_obj_set_style_text_color(st->tile_labels[i], g2048_text_color(v), 0);
        }
    }
}

// Spawn a new tile (2 at 90%, 4 at 10%) on a uniformly random empty cell.
// Returns false if the board is full (no empty cell). Bounded scan over the 16
// cells, so it always terminates.
bool g2048_spawn(G2048PageState* st)
{
    int empties[G2048PageState::kCellCount];
    int n = 0;
    for (int i = 0; i < G2048PageState::kCellCount; ++i)
    {
        if (st->board[i] == 0)
        {
            empties[n++] = i;
        }
    }
    if (n == 0)
    {
        return false;
    }
    const int pick = empties[g2048_rand(st) % static_cast<uint32_t>(n)];
    st->board[pick] = (g2048_rand(st) % 10u == 0u) ? 4 : 2;  // 1-in-10 -> 4
    return true;
}

// True if any legal move remains: an empty cell exists, or some orthogonally
// adjacent pair is equal (and could therefore merge).
bool g2048_moves_available(G2048PageState* st)
{
    for (int i = 0; i < G2048PageState::kCellCount; ++i)
    {
        if (st->board[i] == 0)
        {
            return true;
        }
    }
    for (int y = 0; y < G2048PageState::kSize; ++y)
    {
        for (int x = 0; x < G2048PageState::kSize; ++x)
        {
            const int v = st->board[g2048_index(x, y)];
            if (x + 1 < G2048PageState::kSize && st->board[g2048_index(x + 1, y)] == v)
            {
                return true;
            }
            if (y + 1 < G2048PageState::kSize && st->board[g2048_index(x, y + 1)] == v)
            {
                return true;
            }
        }
    }
    return false;
}

// Compact + merge one extracted line of kSize values toward index 0 (the leading
// edge in travel order). Non-zero values slide to the front; equal neighbours
// merge once (left-to-right, so each tile participates in at most one merge per
// move). Adds merged values to *score_gain and returns true if the line changed.
bool g2048_collapse_line(int* line, unsigned int* score_gain)
{
    int compact[G2048PageState::kSize];
    int n = 0;
    for (int i = 0; i < G2048PageState::kSize; ++i)
    {
        if (line[i] != 0)
        {
            compact[n++] = line[i];
        }
    }
    int merged[G2048PageState::kSize];
    int m = 0;
    for (int i = 0; i < n; ++i)
    {
        if (i + 1 < n && compact[i] == compact[i + 1])
        {
            const int v = compact[i] * 2;
            merged[m++] = v;
            *score_gain += static_cast<unsigned int>(v);
            ++i;  // consume the partner so it cannot merge again this move
        }
        else
        {
            merged[m++] = compact[i];
        }
    }
    bool changed = false;
    for (int i = 0; i < G2048PageState::kSize; ++i)
    {
        const int v = (i < m) ? merged[i] : 0;
        if (line[i] != v)
        {
            changed = true;
        }
        line[i] = v;
    }
    return changed;
}

// Apply a move in direction (dx,dy) in {(-1,0)=Left,(1,0)=Right,(0,-1)=Up,
// (0,1)=Down}. Extracts each line in travel order (so collapse always pushes
// toward index 0), collapses it, and writes it back. On any change: spawns a new
// tile, adds the merge score, repaints, and re-tests for game over.
void g2048_move(G2048PageState* st, int dx, int dy)
{
    if (!st || st->game_over)
    {
        return;
    }

    bool changed = false;
    unsigned int gain = 0;

    if (dx != 0)
    {
        // Horizontal: each row is a line. Travel order runs from the leading edge:
        // Left -> read columns 0..3; Right -> read columns 3..0.
        for (int y = 0; y < G2048PageState::kSize; ++y)
        {
            int line[G2048PageState::kSize];
            for (int k = 0; k < G2048PageState::kSize; ++k)
            {
                const int x = (dx < 0) ? k : (G2048PageState::kSize - 1 - k);
                line[k] = st->board[g2048_index(x, y)];
            }
            if (g2048_collapse_line(line, &gain))
            {
                changed = true;
            }
            for (int k = 0; k < G2048PageState::kSize; ++k)
            {
                const int x = (dx < 0) ? k : (G2048PageState::kSize - 1 - k);
                st->board[g2048_index(x, y)] = line[k];
            }
        }
    }
    else
    {
        // Vertical: each column is a line. Up -> read rows 0..3; Down -> 3..0.
        for (int x = 0; x < G2048PageState::kSize; ++x)
        {
            int line[G2048PageState::kSize];
            for (int k = 0; k < G2048PageState::kSize; ++k)
            {
                const int y = (dy < 0) ? k : (G2048PageState::kSize - 1 - k);
                line[k] = st->board[g2048_index(x, y)];
            }
            if (g2048_collapse_line(line, &gain))
            {
                changed = true;
            }
            for (int k = 0; k < G2048PageState::kSize; ++k)
            {
                const int y = (dy < 0) ? k : (G2048PageState::kSize - 1 - k);
                st->board[g2048_index(x, y)] = line[k];
            }
        }
    }

    if (!changed)
    {
        return;  // illegal move: nothing slid or merged, so no new tile spawns
    }

    st->score += gain;
    g2048_update_score(st);
    g2048_spawn(st);
    g2048_render(st);

    if (!g2048_moves_available(st))
    {
        st->game_over = true;
        if (st->status_label && lv_obj_is_valid(st->status_label))
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Game Over - Score %u", st->score);
            lv_label_set_text(st->status_label, buf);
        }
    }
}

void g2048_reset(G2048PageState* st)
{
    for (int i = 0; i < G2048PageState::kCellCount; ++i)
    {
        st->board[i] = 0;
    }
    st->score = 0;
    st->game_over = false;
    // Two starting tiles, exactly like the classic game.
    g2048_spawn(st);
    g2048_spawn(st);
    g2048_update_score(st);
    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        lv_label_set_text(st->status_label, "");
    }
    g2048_render(st);
}

// Map a swipe direction to a move. Attached to the root so a swipe anywhere on
// the board is read (mirrors watch_face.cpp's gesture handling).
void g2048_gesture_cb(lv_event_t* e)
{
    auto* st = static_cast<G2048PageState*>(lv_event_get_user_data(e));
    if (!st)
    {
        return;
    }
    lv_indev_t* indev = lv_indev_active();
    if (!indev)
    {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    switch (dir)
    {
    case LV_DIR_LEFT: g2048_move(st, -1, 0); break;
    case LV_DIR_RIGHT: g2048_move(st, 1, 0); break;
    case LV_DIR_TOP: g2048_move(st, 0, -1); break;
    case LV_DIR_BOTTOM: g2048_move(st, 0, 1); break;
    default: break;
    }
}

lv_obj_t* g2048_make_arrow(lv_obj_t* parent,
                           G2048PageState* st,
                           const char* text,
                           lv_event_cb_t cb)
{
    // Large touch target (96x80) bound to CLICKED so a tap fires one move (a move
    // is discrete, unlike Snake's held-direction d-pad). The whole button is the
    // hitbox, not just the glyph.
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 96, 80);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, st);
    return btn;
}

void g2048_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<G2048PageState*>(user_data);
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
    lv_obj_set_style_pad_all(state->root, G2048PageState::kRootPad, 0);
    lv_obj_set_flex_flow(state->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->root, 10, 0);
    // Intentionally NOT scrollable: the header + 506px board + control pad fit the
    // 1168px panel, and a scroll container would consume the swipe gesture before
    // g2048_gesture_cb could read it. The gesture is bound to the root below.
    lv_obj_clear_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(state->root, g2048_gesture_cb, LV_EVENT_GESTURE, state);

    // Top row: Back button + score label.
    lv_obj_t* top = lv_obj_create(state->root);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_button_create(top);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    state->score_label = lv_label_create(top);
    lv_obj_set_style_text_color(state->score_label, ui::theme::text(), 0);
    lv_obj_set_style_text_font(state->score_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(state->score_label, "Score: 0");

    // Status (Game Over) line.
    state->status_label = lv_label_create(state->root);
    lv_obj_set_style_text_color(state->status_label, ui::theme::error(), 0);
    lv_obj_set_style_text_font(state->status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(state->status_label, "");

    // Board: a fixed-size container holding the 4x4 tiles, positioned absolutely
    // (created once, recolored/relabeled each move). Classic 2048 board frame.
    lv_obj_t* board = lv_obj_create(state->root);
    lv_obj_set_size(board, G2048PageState::kBoardPx, G2048PageState::kBoardPx);
    lv_obj_set_style_bg_color(board, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(board, 0, 0);
    lv_obj_set_style_radius(board, 6, 0);
    lv_obj_set_style_pad_all(board, 0, 0);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);

    for (int y = 0; y < G2048PageState::kSize; ++y)
    {
        for (int x = 0; x < G2048PageState::kSize; ++x)
        {
            lv_obj_t* cell = lv_obj_create(board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, G2048PageState::kTilePx, G2048PageState::kTilePx);
            const int px = G2048PageState::kTileGap +
                           x * (G2048PageState::kTilePx + G2048PageState::kTileGap);
            const int py = G2048PageState::kTileGap +
                           y * (G2048PageState::kTilePx + G2048PageState::kTileGap);
            lv_obj_set_pos(cell, px, py);
            lv_obj_set_style_bg_color(cell, g2048_tile_color(0), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* lbl = lv_label_create(cell);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(lbl, g2048_text_color(0), 0);
            lv_label_set_text(lbl, "");
            lv_obj_center(lbl);

            const int ci = g2048_index(x, y);
            state->cells[ci] = cell;
            state->tile_labels[ci] = lbl;
        }
    }

    // Arrow pad: Up on its own row, then Left/Down/Right (same layout as Snake's
    // d-pad). Each arrow is a discrete one-move tap.
    lv_obj_t* pad_up_row = lv_obj_create(state->root);
    lv_obj_set_size(pad_up_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pad_up_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad_up_row, 0, 0);
    lv_obj_set_style_pad_all(pad_up_row, 0, 0);
    lv_obj_set_flex_flow(pad_up_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pad_up_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pad_up_row, LV_OBJ_FLAG_SCROLLABLE);
    g2048_make_arrow(
        pad_up_row, state, LV_SYMBOL_UP,
        [](lv_event_t* e)
        { g2048_move(static_cast<G2048PageState*>(lv_event_get_user_data(e)), 0, -1); });

    lv_obj_t* pad_mid_row = lv_obj_create(state->root);
    lv_obj_set_size(pad_mid_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pad_mid_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad_mid_row, 0, 0);
    lv_obj_set_style_pad_all(pad_mid_row, 0, 0);
    lv_obj_set_flex_flow(pad_mid_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pad_mid_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pad_mid_row, 12, 0);
    lv_obj_clear_flag(pad_mid_row, LV_OBJ_FLAG_SCROLLABLE);
    g2048_make_arrow(
        pad_mid_row, state, LV_SYMBOL_LEFT,
        [](lv_event_t* e)
        { g2048_move(static_cast<G2048PageState*>(lv_event_get_user_data(e)), -1, 0); });
    g2048_make_arrow(
        pad_mid_row, state, LV_SYMBOL_DOWN,
        [](lv_event_t* e)
        { g2048_move(static_cast<G2048PageState*>(lv_event_get_user_data(e)), 0, 1); });
    g2048_make_arrow(
        pad_mid_row, state, LV_SYMBOL_RIGHT,
        [](lv_event_t* e)
        { g2048_move(static_cast<G2048PageState*>(lv_event_get_user_data(e)), 1, 0); });

    // Restart button.
    lv_obj_t* restart_btn = lv_button_create(state->root);
    lv_obj_t* restart_lbl = lv_label_create(restart_btn);
    lv_label_set_text(restart_lbl, LV_SYMBOL_REFRESH " Restart");
    lv_obj_center(restart_lbl);
    lv_obj_add_event_cb(
        restart_btn,
        [](lv_event_t* e)
        { g2048_reset(static_cast<G2048PageState*>(lv_event_get_user_data(e))); },
        LV_EVENT_CLICKED, state);

    // Seed the LCG from the tick counter, then deal the opening board.
    state->rng = lv_tick_get() ^ 0x68C2A4B7u;
    g2048_reset(state);
}

void g2048_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<G2048PageState*>(user_data);
    if (!state)
    {
        return;
    }
    // No repeating timer to delete (moves are event-driven); just tear down the
    // root with companion_exit-style guards and clear the cached child pointers.
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->score_label = nullptr;
        state->status_label = nullptr;
        for (int i = 0; i < G2048PageState::kCellCount; ++i)
        {
            state->cells[i] = nullptr;
            state->tile_labels[i] = nullptr;
        }
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->score_label = nullptr;
    state->status_label = nullptr;
    for (int i = 0; i < G2048PageState::kCellCount; ++i)
    {
        state->cells[i] = nullptr;
        state->tile_labels[i] = nullptr;
    }
}

ui::CallbackAppScreen s_g2048_app("g2048", "2048", &Chat, g2048_enter, g2048_exit, &s_g2048_state);

// ---------------------------------------------------------------------------
// Flashlight / SOS: a fully self-contained touch-only flashlight, built inline
// the same way as the C6 Companion / Snake / Tetris / 2048 apps above (file-
// static state, enter()/exit() that build/tear-down a root sized LV_PCT(100),
// and a CallbackAppScreen added to s_apps[]). stable_id = "flashlight" so the
// boot self-test logs appselftest:flashlight:ok when it enters then immediately
// exits this screen once.
//
// The whole screen is a max-brightness white surface you toggle on (white) /
// off (black) by tapping the light area. A "SOS Strobe" toggle starts an
// lv_timer that flashes the surface white/black in the International Morse SOS
// pattern ( ... --- ... ): three short, three long, three short, then a word
// gap before it repeats. The pattern is a flat table of {lit, duration_ms}
// segments; one variable-period lv_timer advances one segment per fire and
// re-arms itself to that segment's duration (the same lv_timer_set_period
// technique the Tetris soft-drop and System-Test apps use), so the timing is
// exact (dot = kDotMs, dash = 3 dots, intra-symbol gap = 1 dot, inter-letter
// gap = 3 dots, word gap = 7 dots) without busy-waiting.
//
// While the app is active it drives the backlight to maximum through the
// portable platform::ui::device brightness API (supports_screen_brightness() /
// screen_brightness() / set_screen_brightness()), capturing the prior level on
// enter and restoring it on exit. set_screen_brightness() maps the level to a
// percent that the board runtime clamps to 0..100, so passing a saturating
// 0xFF reliably yields full brightness without needing the per-board max macro.
//
// flashlight_exit deletes the strobe timer and lv_obj_del the root with the
// same null/validity guards companion_exit/snake_exit use, and restores the
// captured backlight level, so the boot self-test's bare enter->exit leaves
// nothing dangling and the screen brightness unchanged.
// ---------------------------------------------------------------------------
struct FlashlightPageState
{
    // Morse timing. A dot is the base unit; a dash is three dots; the gap
    // between the dots/dashes inside one letter is one dot; the gap between
    // letters is three dots; the gap between words (here, before the SOS
    // pattern repeats) is seven dots. 200ms/dot gives a clearly readable strobe.
    static constexpr uint32_t kDotMs = 200;
    static constexpr uint32_t kDashMs = kDotMs * 3;        // 600
    static constexpr uint32_t kSymbolGapMs = kDotMs;       // 200 (within a letter)
    static constexpr uint32_t kLetterGapMs = kDotMs * 3;   // 600 (between letters)
    static constexpr uint32_t kWordGapMs = kDotMs * 7;     // 1400 (before repeat)

    lv_obj_t* root = nullptr;       // the app container (control bar lives here)
    lv_obj_t* light = nullptr;      // the tappable full-bleed light surface
    lv_obj_t* mode_label = nullptr; // text on the SOS toggle button

    lv_timer_t* timer = nullptr;    // SOS strobe timer (null unless SOS active)
    int seg = 0;                    // current index into the SOS segment table

    bool light_on = true;           // manual mode: is the surface lit (white)?
    bool sos = false;               // is SOS strobe mode active?

    bool brightness_saved = false;  // did we capture + raise the backlight?
    uint8_t prev_brightness = 0;    // backlight level captured on enter
};

FlashlightPageState s_flashlight_state;

// One SOS cycle as a flat list of {lit, duration} segments:
//   S = dot . dot . dot  -> three (on dot)(off symbol-gap) pairs
//   (letter gap)
//   O = dash _ dash _ dash
//   (letter gap)
//   S = dot . dot . dot
//   (word gap, then the table repeats)
// The trailing gap after each dot/dash within a letter is the symbol gap; the
// last symbol of each letter is instead followed by the letter gap (or, after
// the final S, the word gap). Lit segments paint white, unlit paint black.
struct FlashlightSegment
{
    bool lit;
    uint32_t duration_ms;
};

const FlashlightSegment kSosPattern[] = {
    // S: . . .
    {true, FlashlightPageState::kDotMs},  {false, FlashlightPageState::kSymbolGapMs},
    {true, FlashlightPageState::kDotMs},  {false, FlashlightPageState::kSymbolGapMs},
    {true, FlashlightPageState::kDotMs},  {false, FlashlightPageState::kLetterGapMs},
    // O: - - -
    {true, FlashlightPageState::kDashMs}, {false, FlashlightPageState::kSymbolGapMs},
    {true, FlashlightPageState::kDashMs}, {false, FlashlightPageState::kSymbolGapMs},
    {true, FlashlightPageState::kDashMs}, {false, FlashlightPageState::kLetterGapMs},
    // S: . . .
    {true, FlashlightPageState::kDotMs},  {false, FlashlightPageState::kSymbolGapMs},
    {true, FlashlightPageState::kDotMs},  {false, FlashlightPageState::kSymbolGapMs},
    {true, FlashlightPageState::kDotMs},  {false, FlashlightPageState::kWordGapMs},
};

constexpr int kSosPatternLen =
    static_cast<int>(sizeof(kSosPattern) / sizeof(kSosPattern[0]));

// Paint the light surface white (lit) or black (dark).
void flashlight_paint(FlashlightPageState* st, bool lit)
{
    if (!st || !st->light || !lv_obj_is_valid(st->light))
    {
        return;
    }
    lv_obj_set_style_bg_color(st->light, lit ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000),
                              0);
    lv_obj_set_style_bg_opa(st->light, LV_OPA_COVER, 0);
}

// Drive the backlight to maximum (capturing the prior level once so exit can
// restore it). set_screen_brightness maps the level to a percent the board
// clamps to 0..100, so 0xFF saturates to full brightness on any board max.
void flashlight_raise_brightness(FlashlightPageState* st)
{
    if (!st || st->brightness_saved)
    {
        return;
    }
    if (!platform::ui::device::supports_screen_brightness())
    {
        return;
    }
    st->prev_brightness = platform::ui::device::screen_brightness();
    st->brightness_saved = true;
    platform::ui::device::set_screen_brightness(0xFF);
}

// Restore the backlight level captured by flashlight_raise_brightness (no-op if
// it was never raised). Safe to call repeatedly.
void flashlight_restore_brightness(FlashlightPageState* st)
{
    if (!st || !st->brightness_saved)
    {
        return;
    }
    if (platform::ui::device::supports_screen_brightness())
    {
        platform::ui::device::set_screen_brightness(st->prev_brightness);
    }
    st->brightness_saved = false;
}

// SOS strobe tick: paint the current segment, advance to the next (wrapping at
// the end of the table), and re-arm the timer to the next segment's duration so
// each dot/dash/gap lasts exactly its Morse length.
void flashlight_sos_tick(lv_timer_t* timer)
{
    auto* st = static_cast<FlashlightPageState*>(lv_timer_get_user_data(timer));
    if (!st)
    {
        return;
    }
    if (st->seg < 0 || st->seg >= kSosPatternLen)
    {
        st->seg = 0;
    }
    const FlashlightSegment& s = kSosPattern[st->seg];
    flashlight_paint(st, s.lit);
    lv_timer_set_period(timer, s.duration_ms);
    st->seg = (st->seg + 1) % kSosPatternLen;
}

void flashlight_update_mode_label(FlashlightPageState* st)
{
    if (st && st->mode_label && lv_obj_is_valid(st->mode_label))
    {
        lv_label_set_text(st->mode_label, st->sos ? "SOS: ON" : "SOS: OFF");
    }
}

// Start the SOS strobe: create the variable-period timer (if not already
// running) and fire the first segment immediately so the pattern starts at once.
void flashlight_start_sos(FlashlightPageState* st)
{
    if (!st)
    {
        return;
    }
    st->sos = true;
    st->seg = 0;
    if (!st->timer)
    {
        // Initial period is a placeholder; the first tick re-arms it from the
        // segment table. Fire it now so the strobe begins on the first dot.
        st->timer = lv_timer_create(flashlight_sos_tick, FlashlightPageState::kDotMs, st);
        if (st->timer)
        {
            flashlight_sos_tick(st->timer);
        }
    }
    flashlight_update_mode_label(st);
}

// Stop the SOS strobe: delete the timer and return the surface to the manual
// on/off state.
void flashlight_stop_sos(FlashlightPageState* st)
{
    if (!st)
    {
        return;
    }
    st->sos = false;
    if (st->timer)
    {
        lv_timer_del(st->timer);
        st->timer = nullptr;
    }
    flashlight_paint(st, st->light_on);
    flashlight_update_mode_label(st);
}

void flashlight_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<FlashlightPageState*>(user_data);
    if (!state || !parent || (state->root && lv_obj_is_valid(state->root)))
    {
        return;
    }

    state->timer = nullptr;
    state->seg = 0;
    state->light_on = true;
    state->sos = false;
    state->brightness_saved = false;
    state->prev_brightness = 0;

    // Root: a full-bleed black container with no padding so the light surface
    // can cover the entire 540x1168 panel for the brightest possible flood.
    state->root = lv_obj_create(parent);
    lv_obj_set_size(state->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(state->root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(state->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->root, 0, 0);
    lv_obj_set_style_radius(state->root, 0, 0);
    lv_obj_set_style_pad_all(state->root, 0, 0);
    lv_obj_clear_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // The tappable light surface fills the whole root; tapping toggles it
    // on/off (white/black) when not in SOS mode. It sits at the back so the
    // control bar (added after) stays on top and clickable.
    state->light = lv_obj_create(state->root);
    lv_obj_remove_style_all(state->light);
    lv_obj_set_size(state->light, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(state->light, 0, 0);
    lv_obj_clear_flag(state->light, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(state->light, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        state->light,
        [](lv_event_t* e)
        {
            auto* st = static_cast<FlashlightPageState*>(lv_event_get_user_data(e));
            if (!st || st->sos)
            {
                return;  // taps are inert while the SOS strobe is running
            }
            st->light_on = !st->light_on;
            flashlight_paint(st, st->light_on);
        },
        LV_EVENT_CLICKED, state);

    // Control bar pinned to the top, above the light surface. Holds the Back
    // button and the SOS strobe toggle. Semi-transparent dark pill so the
    // controls stay legible whether the light behind them is white or black.
    lv_obj_t* bar = lv_obj_create(state->root);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 12, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // Back button: routes to the launcher menu via the same exit the Chat app
    // uses (exactly like companion_enter/help_enter/systest_enter).
    lv_obj_t* back_btn = lv_button_create(bar);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    // SOS strobe toggle: flips between the steady flashlight and the Morse SOS
    // strobe. The label reflects the current mode.
    lv_obj_t* sos_btn = lv_button_create(bar);
    lv_obj_set_style_pad_top(sos_btn, 14, 0);
    lv_obj_set_style_pad_bottom(sos_btn, 14, 0);
    state->mode_label = lv_label_create(sos_btn);
    lv_obj_set_style_text_font(state->mode_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(state->mode_label, "SOS: OFF");
    lv_obj_center(state->mode_label);
    lv_obj_add_event_cb(
        sos_btn,
        [](lv_event_t* e)
        {
            auto* st = static_cast<FlashlightPageState*>(lv_event_get_user_data(e));
            if (!st)
            {
                return;
            }
            if (st->sos)
            {
                flashlight_stop_sos(st);
            }
            else
            {
                flashlight_start_sos(st);
            }
        },
        LV_EVENT_CLICKED, state);

    // Drive the backlight to maximum for the brightest flood, then show the
    // light in its initial (on) state.
    flashlight_raise_brightness(state);
    flashlight_paint(state, state->light_on);
}

void flashlight_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<FlashlightPageState*>(user_data);
    if (!state)
    {
        return;
    }
    // Delete the strobe timer first so it can never fire against a freed root.
    if (state->timer)
    {
        lv_timer_del(state->timer);
        state->timer = nullptr;
    }
    // Restore the backlight level we captured on enter (no-op if never raised).
    flashlight_restore_brightness(state);
    state->sos = false;
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->light = nullptr;
        state->mode_label = nullptr;
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->light = nullptr;
    state->mode_label = nullptr;
}

ui::CallbackAppScreen s_flashlight_app("flashlight", "Flashlight", &Setting, flashlight_enter,
                                       flashlight_exit, &s_flashlight_state);

// ---------------------------------------------------------------------------
// Stopwatch + Timer: a fully self-contained touch-only stopwatch and count-down
// timer, built inline the same way as the C6 Companion / Snake / Tetris / 2048 /
// Flashlight apps above (file-static state, enter()/exit() that build/tear-down a
// root sized LV_PCT(100), and a CallbackAppScreen added to s_apps[]). stable_id =
// "stopwatch" so the boot self-test logs appselftest:stopwatch:ok when it enters
// then immediately exits this screen once.
//
// TIME SOURCE: the device wall-clock is deliberately NOT used. All elapsed time is
// measured from LVGL's monotonic millisecond tick (lv_tick_get() captures a start
// stamp; lv_tick_elaps(stamp) returns the ms since, handling the 32-bit wrap). A
// ~50 ms lv_timer (stopwatch_tick) only repaints the HH:MM:SS.cs label from the
// computed elapsed value -- it never accumulates time itself, so a missed/late tick
// never drifts the clock. Accuracy across Start/Stop is preserved by folding each
// running span into accum_ms on Stop: while running, elapsed = accum_ms +
// lv_tick_elaps(start_tick); while stopped, elapsed = accum_ms exactly.
//
// Two modes share that one elapsed value:
//   * Stopwatch (default): counts UP from 0; the big label shows the elapsed time
//     and Lap appends the current elapsed split to a scrollable lap list.
//   * Timer: counts DOWN from a target the user dials in with +/- (minutes and
//     seconds). remaining = target_ms - elapsed, clamped at 0; on reaching 0 the
//     run auto-stops and the status line reads "Time's up".
// Start/Stop toggles the run; Reset zeroes the elapsed value (and clears laps);
// Mode toggles Stopwatch<->Timer (only while stopped, to keep the math obvious).
// A Back button routes to the launcher menu via ::ui_request_exit_to_menu().
//
// stopwatch_exit deletes the repaint timer and lv_obj_del the root with the same
// null/validity guards companion_exit/snake_exit use, so the boot self-test's bare
// enter->exit (which never starts the run) leaves nothing dangling.
// ---------------------------------------------------------------------------
struct StopwatchPageState
{
    // ~50 ms repaint cadence: smooth centisecond digits without burdening the UI
    // thread. The label resolution is centiseconds (1/100 s) so 50 ms is ample.
    static constexpr uint32_t kTickMs = 50;
    // Lap list cap: a generous fixed ring so a long session cannot grow unbounded
    // memory; once full, the oldest laps scroll out of the list (newest kept).
    static constexpr int kMaxLaps = 64;
    // Timer-mode dial bounds: 0..99 minutes, 0..59 seconds.
    static constexpr int kMaxMinutes = 99;

    lv_obj_t* root = nullptr;
    lv_obj_t* time_label = nullptr;    // the big HH:MM:SS.cs read-out
    lv_obj_t* status_label = nullptr;  // mode / "Time's up" line
    lv_obj_t* start_label = nullptr;   // text on the Start/Stop button
    lv_obj_t* lap_list = nullptr;      // scrollable container of lap rows
    lv_obj_t* dial_row = nullptr;      // the +/- minute/second dial (Timer mode)
    lv_obj_t* dial_label = nullptr;    // shows the dialed-in target MM:SS
    lv_obj_t* lap_btn = nullptr;       // Lap button (Stopwatch mode only)
    lv_timer_t* timer = nullptr;

    bool running = false;        // is the clock currently advancing?
    bool countdown = false;      // false = stopwatch (up), true = timer (down)
    bool expired = false;        // timer mode reached zero
    uint32_t accum_ms = 0;       // elapsed ms banked from prior running spans
    uint32_t start_tick = 0;     // lv_tick_get() at the most recent Start
    uint32_t target_ms = 60000;  // timer-mode count-down target (default 1:00)
    int lap_count = 0;           // number of laps recorded this session
};

StopwatchPageState s_stopwatch_state;

// Total elapsed ms since the last Reset, summed across every Start/Stop span.
// While running, add the live span (lv_tick_elaps handles the 32-bit tick wrap);
// while stopped, accum_ms already holds the exact total.
uint32_t stopwatch_elapsed_ms(StopwatchPageState* st)
{
    if (!st)
    {
        return 0;
    }
    uint32_t ms = st->accum_ms;
    if (st->running)
    {
        ms += lv_tick_elaps(st->start_tick);
    }
    return ms;
}

// Format a millisecond count as HH:MM:SS.cs into buf (centiseconds = 1/100 s).
void stopwatch_format(uint32_t ms, char* buf, size_t buf_len)
{
    const uint32_t total_cs = ms / 10u;       // centiseconds
    const uint32_t cs = total_cs % 100u;
    const uint32_t total_s = total_cs / 100u;
    const uint32_t s = total_s % 60u;
    const uint32_t total_m = total_s / 60u;
    const uint32_t m = total_m % 60u;
    const uint32_t h = total_m / 60u;
    std::snprintf(buf, buf_len, "%02lu:%02lu:%02lu.%02lu",
                  static_cast<unsigned long>(h), static_cast<unsigned long>(m),
                  static_cast<unsigned long>(s), static_cast<unsigned long>(cs));
}

// Repaint the big read-out + status line from the current elapsed value. In timer
// mode this shows the remaining time (target - elapsed, floored at 0) and auto-
// stops the run the moment it hits zero.
void stopwatch_refresh(StopwatchPageState* st)
{
    if (!st)
    {
        return;
    }
    const uint32_t elapsed = stopwatch_elapsed_ms(st);

    uint32_t shown = elapsed;
    if (st->countdown)
    {
        if (elapsed >= st->target_ms)
        {
            shown = 0;
            // First crossing of zero while running: latch expired, stop the clock,
            // and bank the full target so a subsequent Start does not re-trigger.
            if (st->running && !st->expired)
            {
                st->expired = true;
                st->running = false;
                st->accum_ms = st->target_ms;
                if (st->start_label && lv_obj_is_valid(st->start_label))
                {
                    lv_label_set_text(st->start_label, LV_SYMBOL_PLAY " Start");
                }
            }
        }
        else
        {
            shown = st->target_ms - elapsed;
        }
    }

    if (st->time_label && lv_obj_is_valid(st->time_label))
    {
        char buf[24];
        stopwatch_format(shown, buf, sizeof(buf));
        lv_label_set_text(st->time_label, buf);
    }

    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        if (st->countdown && st->expired)
        {
            lv_label_set_text(st->status_label, "Timer - Time's up");
        }
        else if (st->countdown)
        {
            lv_label_set_text(st->status_label, st->running ? "Timer - running" : "Timer");
        }
        else
        {
            lv_label_set_text(st->status_label,
                              st->running ? "Stopwatch - running" : "Stopwatch");
        }
    }
}

void stopwatch_tick(lv_timer_t* timer)
{
    auto* st = static_cast<StopwatchPageState*>(lv_timer_get_user_data(timer));
    stopwatch_refresh(st);
}

// Reflect the dialed-in timer target into its label (MM:SS).
void stopwatch_update_dial(StopwatchPageState* st)
{
    if (!st || !st->dial_label || !lv_obj_is_valid(st->dial_label))
    {
        return;
    }
    const uint32_t total_s = st->target_ms / 1000u;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02lu:%02lu",
                  static_cast<unsigned long>(total_s / 60u),
                  static_cast<unsigned long>(total_s % 60u));
    lv_label_set_text(st->dial_label, buf);
}

// Show/hide the Timer dial row and the Lap button depending on the mode: the dial
// is only meaningful for the count-down timer; Lap splits only make sense for the
// count-up stopwatch.
void stopwatch_apply_mode_visibility(StopwatchPageState* st)
{
    if (!st)
    {
        return;
    }
    if (st->dial_row && lv_obj_is_valid(st->dial_row))
    {
        if (st->countdown)
        {
            lv_obj_clear_flag(st->dial_row, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(st->dial_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (st->lap_btn && lv_obj_is_valid(st->lap_btn))
    {
        if (st->countdown)
        {
            lv_obj_add_flag(st->lap_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(st->lap_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Start/Stop toggle. On Start, stamp the tick so the live span is measured from
// now; on Stop, fold the just-finished span into accum_ms so the total stays exact.
void stopwatch_toggle_run(StopwatchPageState* st)
{
    if (!st)
    {
        return;
    }
    if (st->running)
    {
        // Stop: bank the live span, then hold.
        st->accum_ms += lv_tick_elaps(st->start_tick);
        st->running = false;
    }
    else
    {
        // In timer mode, a Start after expiry first rewinds to a fresh target.
        if (st->countdown && (st->expired || st->accum_ms >= st->target_ms))
        {
            st->accum_ms = 0;
            st->expired = false;
        }
        st->start_tick = lv_tick_get();
        st->running = true;
    }
    if (st->start_label && lv_obj_is_valid(st->start_label))
    {
        lv_label_set_text(st->start_label,
                          st->running ? (LV_SYMBOL_PAUSE " Stop") : (LV_SYMBOL_PLAY " Start"));
    }
    stopwatch_refresh(st);
}

// Reset: zero the elapsed total, clear the running state and every recorded lap.
void stopwatch_reset(StopwatchPageState* st)
{
    if (!st)
    {
        return;
    }
    st->running = false;
    st->expired = false;
    st->accum_ms = 0;
    st->start_tick = lv_tick_get();
    st->lap_count = 0;
    if (st->lap_list && lv_obj_is_valid(st->lap_list))
    {
        lv_obj_clean(st->lap_list);  // delete all lap rows
    }
    if (st->start_label && lv_obj_is_valid(st->start_label))
    {
        lv_label_set_text(st->start_label, LV_SYMBOL_PLAY " Start");
    }
    stopwatch_refresh(st);
}

// Record a lap: prepend the current elapsed split to the lap list (newest on top).
// Bounded by kMaxLaps so the list cannot grow without limit over a long session.
void stopwatch_add_lap(StopwatchPageState* st)
{
    if (!st || st->countdown || !st->lap_list || !lv_obj_is_valid(st->lap_list))
    {
        return;
    }
    if (st->lap_count >= StopwatchPageState::kMaxLaps)
    {
        // Drop the oldest row (the last child) to keep the list bounded.
        const uint32_t child_count = lv_obj_get_child_count(st->lap_list);
        if (child_count > 0)
        {
            lv_obj_t* oldest = lv_obj_get_child(st->lap_list, child_count - 1);
            if (oldest && lv_obj_is_valid(oldest))
            {
                lv_obj_del(oldest);
            }
        }
    }
    else
    {
        st->lap_count += 1;
    }

    char tbuf[24];
    stopwatch_format(stopwatch_elapsed_ms(st), tbuf, sizeof(tbuf));
    char buf[40];
    std::snprintf(buf, sizeof(buf), "Lap %d   %s", st->lap_count, tbuf);

    lv_obj_t* row = lv_label_create(st->lap_list);
    lv_label_set_text(row, buf);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_text_font(row, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(row, ui::theme::text(), 0);
    // Newest lap on top.
    lv_obj_move_to_index(row, 0);
}

// Adjust the count-down target by a signed number of seconds, clamped to
// [0, kMaxMinutes:59]. Only meaningful in timer mode and while stopped.
void stopwatch_adjust_target(StopwatchPageState* st, int delta_s)
{
    if (!st)
    {
        return;
    }
    long total_s = static_cast<long>(st->target_ms / 1000u) + delta_s;
    const long max_s = static_cast<long>(StopwatchPageState::kMaxMinutes) * 60 + 59;
    if (total_s < 0)
    {
        total_s = 0;
    }
    if (total_s > max_s)
    {
        total_s = max_s;
    }
    st->target_ms = static_cast<uint32_t>(total_s) * 1000u;
    // Re-arm a stopped timer to the new target so the read-out tracks the dial.
    if (!st->running)
    {
        st->accum_ms = 0;
        st->expired = false;
    }
    stopwatch_update_dial(st);
    stopwatch_refresh(st);
}

// Toggle Stopwatch<->Timer. Only permitted while stopped so the elapsed math stays
// unambiguous; resets the elapsed value and laps for a clean switch.
void stopwatch_toggle_mode(StopwatchPageState* st)
{
    if (!st || st->running)
    {
        return;
    }
    st->countdown = !st->countdown;
    stopwatch_reset(st);
    stopwatch_apply_mode_visibility(st);
    stopwatch_update_dial(st);
    stopwatch_refresh(st);
}

// A large touch button with a centered text label, child of `parent`, bound to
// LV_EVENT_CLICKED -> cb with `st` as user_data. Returns the button.
lv_obj_t* stopwatch_make_button(lv_obj_t* parent,
                                StopwatchPageState* st,
                                const char* text,
                                lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_style_pad_top(btn, 14, 0);
    lv_obj_set_style_pad_bottom(btn, 14, 0);
    lv_obj_set_style_pad_left(btn, 18, 0);
    lv_obj_set_style_pad_right(btn, 18, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, st);
    return btn;
}

void stopwatch_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<StopwatchPageState*>(user_data);
    if (!state || !parent || (state->root && lv_obj_is_valid(state->root)))
    {
        return;
    }

    // Fresh session state (preserve the dialed target across re-entries, but reset
    // the run/laps so a stale running span can never carry over).
    state->running = false;
    state->expired = false;
    state->accum_ms = 0;
    state->start_tick = lv_tick_get();
    state->lap_count = 0;
    if (state->target_ms == 0)
    {
        state->target_ms = 60000;
    }
    state->time_label = nullptr;
    state->status_label = nullptr;
    state->start_label = nullptr;
    state->lap_list = nullptr;
    state->dial_row = nullptr;
    state->dial_label = nullptr;
    state->lap_btn = nullptr;
    state->timer = nullptr;

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
    lv_obj_set_flex_align(state->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->root, 10, 0);
    // Vertically scrollable as a safety net so the dial + controls + lap list can
    // never push a control off the 1168px panel (same pattern as the Help page).
    lv_obj_set_scroll_dir(state->root, LV_DIR_VER);
    lv_obj_add_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // Back button: routes to the launcher menu via the same exit the Chat app uses
    // (exactly like companion_enter/help_enter/systest_enter).
    lv_obj_t* back_btn = lv_button_create(state->root);
    lv_obj_set_width(back_btn, LV_PCT(45));
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    // Status line (mode / "Time's up").
    state->status_label = lv_label_create(state->root);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(state->status_label, ui::theme::text_muted(), 0);
    lv_label_set_text(state->status_label, "Stopwatch");

    // The big HH:MM:SS.cs read-out (montserrat-24 is the largest font compiled
    // into this build; the menu uses it for the battery percentage).
    state->time_label = lv_label_create(state->root);
    lv_obj_set_width(state->time_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->time_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(state->time_label, ui::theme::text(), 0);
    lv_obj_set_style_pad_top(state->time_label, 6, 0);
    lv_obj_set_style_pad_bottom(state->time_label, 6, 0);
    lv_label_set_text(state->time_label, "00:00:00.00");

    // Primary controls row: Start/Stop + Reset.
    lv_obj_t* ctrl_row = lv_obj_create(state->root);
    lv_obj_set_size(ctrl_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row, 14, 0);
    lv_obj_clear_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* start_btn = stopwatch_make_button(
        ctrl_row, state, LV_SYMBOL_PLAY " Start",
        [](lv_event_t* e)
        { stopwatch_toggle_run(static_cast<StopwatchPageState*>(lv_event_get_user_data(e))); });
    // Capture the Start/Stop button's label so the toggle can rewrite it.
    state->start_label = lv_obj_get_child(start_btn, 0);

    stopwatch_make_button(
        ctrl_row, state, LV_SYMBOL_REFRESH " Reset",
        [](lv_event_t* e)
        { stopwatch_reset(static_cast<StopwatchPageState*>(lv_event_get_user_data(e))); });

    // Secondary controls row: Lap (stopwatch) + Mode toggle.
    lv_obj_t* ctrl_row2 = lv_obj_create(state->root);
    lv_obj_set_size(ctrl_row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctrl_row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row2, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row2, 0, 0);
    lv_obj_set_flex_flow(ctrl_row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row2, 14, 0);
    lv_obj_clear_flag(ctrl_row2, LV_OBJ_FLAG_SCROLLABLE);

    state->lap_btn = stopwatch_make_button(
        ctrl_row2, state, LV_SYMBOL_PLUS " Lap",
        [](lv_event_t* e)
        { stopwatch_add_lap(static_cast<StopwatchPageState*>(lv_event_get_user_data(e))); });

    stopwatch_make_button(
        ctrl_row2, state, LV_SYMBOL_LOOP " Mode",
        [](lv_event_t* e)
        { stopwatch_toggle_mode(static_cast<StopwatchPageState*>(lv_event_get_user_data(e))); });

    // Timer dial row (Timer mode only): -1m  -10s  [MM:SS]  +10s  +1m.
    state->dial_row = lv_obj_create(state->root);
    lv_obj_set_size(state->dial_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(state->dial_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state->dial_row, 0, 0);
    lv_obj_set_style_pad_all(state->dial_row, 0, 0);
    lv_obj_set_flex_flow(state->dial_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->dial_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(state->dial_row, 8, 0);
    lv_obj_clear_flag(state->dial_row, LV_OBJ_FLAG_SCROLLABLE);

    stopwatch_make_button(
        state->dial_row, state, "-1m",
        [](lv_event_t* e)
        { stopwatch_adjust_target(static_cast<StopwatchPageState*>(lv_event_get_user_data(e)),
                                  -60); });
    stopwatch_make_button(
        state->dial_row, state, "-10s",
        [](lv_event_t* e)
        { stopwatch_adjust_target(static_cast<StopwatchPageState*>(lv_event_get_user_data(e)),
                                  -10); });
    state->dial_label = lv_label_create(state->dial_row);
    lv_obj_set_style_text_font(state->dial_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(state->dial_label, ui::theme::accent(), 0);
    lv_label_set_text(state->dial_label, "01:00");
    stopwatch_make_button(
        state->dial_row, state, "+10s",
        [](lv_event_t* e)
        { stopwatch_adjust_target(static_cast<StopwatchPageState*>(lv_event_get_user_data(e)),
                                  10); });
    stopwatch_make_button(
        state->dial_row, state, "+1m",
        [](lv_event_t* e)
        { stopwatch_adjust_target(static_cast<StopwatchPageState*>(lv_event_get_user_data(e)),
                                  60); });

    // Lap list: a scrollable column the laps are prepended to (newest on top).
    state->lap_list = lv_obj_create(state->root);
    lv_obj_set_width(state->lap_list, LV_PCT(100));
    lv_obj_set_height(state->lap_list, 300);
    lv_obj_set_style_bg_color(state->lap_list, ui::theme::surface(), 0);
    lv_obj_set_style_bg_opa(state->lap_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->lap_list, 1, 0);
    lv_obj_set_style_border_color(state->lap_list, ui::theme::border(), 0);
    lv_obj_set_style_radius(state->lap_list, 6, 0);
    lv_obj_set_style_pad_all(state->lap_list, 10, 0);
    lv_obj_set_flex_flow(state->lap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->lap_list, 4, 0);
    lv_obj_set_scroll_dir(state->lap_list, LV_DIR_VER);

    // Apply initial mode visibility (start in stopwatch mode: dial hidden, Lap shown)
    // and paint the read-out, then start the ~50 ms repaint timer.
    stopwatch_apply_mode_visibility(state);
    stopwatch_update_dial(state);
    stopwatch_refresh(state);
    state->timer = lv_timer_create(stopwatch_tick, StopwatchPageState::kTickMs, state);
}

void stopwatch_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<StopwatchPageState*>(user_data);
    if (!state)
    {
        return;
    }
    if (state->timer)
    {
        lv_timer_del(state->timer);
        state->timer = nullptr;
    }
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->time_label = nullptr;
        state->status_label = nullptr;
        state->start_label = nullptr;
        state->lap_list = nullptr;
        state->dial_row = nullptr;
        state->dial_label = nullptr;
        state->lap_btn = nullptr;
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->time_label = nullptr;
    state->status_label = nullptr;
    state->start_label = nullptr;
    state->lap_list = nullptr;
    state->dial_row = nullptr;
    state->dial_label = nullptr;
    state->lap_btn = nullptr;
}

ui::CallbackAppScreen s_stopwatch_app("stopwatch", "Stopwatch", &Setting, stopwatch_enter,
                                      stopwatch_exit, &s_stopwatch_state);

// ---------------------------------------------------------------------------
// Node Radar: a fully self-contained mesh-node "radar", built inline the same
// way as the C6 Companion / Snake / Tetris / Help / System Test apps above
// (file-static state, enter()/exit() that build/tear-down a root sized
// LV_PCT(100), and a CallbackAppScreen added to s_apps[]). stable_id =
// "node_radar" so the boot self-test logs appselftest:node_radar:ok when it
// enters then immediately exits this screen once.
//
// It lists every heard mesh node ranked strongest-first. It reads the SAME
// nearby-node source the Contacts page uses:
//   app::messagingFacade().getContactService().getNearby()
// which returns std::vector<chat::contacts::NodeInfo>. From each NodeInfo we use
// the display name (display_name, else long_name, else "!<id>"), the link
// metadata (rssi dBm / snr dB -- treated as present unless NaN, exactly like the
// Node Info page's build_rssi_line/build_snr_line), the last_seen Unix seconds,
// and the node position (position.valid + latitude_i/longitude_i at 1e-7 deg).
//
// Ranking (strongest first), honoring "do not invent data": nodes are bucketed
// by what real signal metadata they expose --
//   tier 0: has RSSI  -> ordered by rssi   (e.g. -50 dBm ranks above -90 dBm)
//   tier 1: has SNR only -> ordered by snr  (higher dB first)
//   tier 2: neither   -> ordered by last_seen (most-recently-heard first)
// so signal-bearing nodes sort above signal-less ones, and signal-less nodes
// fall back to last-heard recency. Each row shows the node name, the real metric
// it was ranked by (RSSI / SNR / "Seen ..."), a proportional signal bar, and --
// only when the node has a valid position AND we currently have a GPS fix
// (platform::ui::gps::get_data().valid) -- an approximate great-circle distance
// (haversine). No value is shown unless it is real.
//
// A ~2s lv_timer (node_radar_tick) re-queries getNearby() and rebuilds the row
// list ONLY when the heard set actually changes: a 32-bit signature folds each
// node's id, last_seen and quantized rssi/snr, so per-tick churn is avoided while
// new/updated/lost nodes still refresh. node_radar_exit deletes the timer and
// lv_obj_del the root with the same null/validity guards companion_exit/
// snake_exit use, so the boot self-test's bare enter->exit (no fix, possibly an
// empty roster) leaves nothing dangling.
// ---------------------------------------------------------------------------
struct NodeRadarPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* list = nullptr;     // scrollable column the node rows are added to
    lv_obj_t* status_label = nullptr;  // "N nodes" / "No nodes heard yet"
    lv_timer_t* timer = nullptr;

    bool have_signature = false;
    uint32_t signature = 0;  // fold of the current ranked set; rebuild on change
};

NodeRadarPageState s_node_radar_state;

// True iff this node exposes a usable RSSI / SNR reading. The Node Info page uses
// the SAME NaN test (build_rssi_line/build_snr_line), so an absent reading (NaN)
// is never treated as real data; a genuine 0 dBm / 0 dB reading still counts.
bool node_radar_has_rssi(const chat::contacts::NodeInfo& n)
{
    return !std::isnan(n.rssi);
}

bool node_radar_has_snr(const chat::contacts::NodeInfo& n)
{
    return !std::isnan(n.snr);
}

// Ranking tier: 0 = has RSSI, 1 = has SNR only, 2 = neither (last-heard only).
int node_radar_tier(const chat::contacts::NodeInfo& n)
{
    if (node_radar_has_rssi(n))
    {
        return 0;
    }
    if (node_radar_has_snr(n))
    {
        return 1;
    }
    return 2;
}

// Best-available display name for a node, mirroring how the Node Info / Contacts
// pages resolve it: the contact/display name first, then the long name, then the
// Meshtastic "!" + 8 hex id form so a row is never blank.
void node_radar_node_name(const chat::contacts::NodeInfo& n, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!n.display_name.empty())
    {
        std::snprintf(out, out_len, "%s", n.display_name.c_str());
        return;
    }
    if (n.long_name[0] != '\0')
    {
        std::snprintf(out, out_len, "%s", n.long_name);
        return;
    }
    if (n.short_name[0] != '\0')
    {
        std::snprintf(out, out_len, "%s", n.short_name);
        return;
    }
    std::snprintf(out, out_len, "!%08lx", static_cast<unsigned long>(n.node_id));
}

// Short "last heard" text from a Unix-seconds timestamp, using the live GPS-or-
// device clock as "now". Degrades to "Seen recently" if the clock is clearly
// unset (the boot self-test runs with no RTC), so a row is always meaningful.
void node_radar_seen_text(uint32_t last_seen, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (last_seen == 0)
    {
        std::snprintf(out, out_len, "Seen recently");
        return;
    }
    const uint32_t now = sys::epoch_seconds_now();
    if (now <= last_seen || now < 1577836800u /* 2020-01-01: clock unset */)
    {
        std::snprintf(out, out_len, "Seen recently");
        return;
    }
    const uint32_t age = now - last_seen;
    if (age < 60u)
    {
        std::snprintf(out, out_len, "Seen %us", static_cast<unsigned>(age));
    }
    else if (age < 3600u)
    {
        std::snprintf(out, out_len, "Seen %um", static_cast<unsigned>(age / 60u));
    }
    else if (age < 86400u)
    {
        std::snprintf(out, out_len, "Seen %uh", static_cast<unsigned>(age / 3600u));
    }
    else
    {
        std::snprintf(out, out_len, "Seen %ud", static_cast<unsigned>(age / 86400u));
    }
}

// Great-circle distance (metres) between two lat/lon points in degrees, via the
// haversine formula. Used only when both endpoints are real (node position valid
// AND a live GPS fix), so it never fabricates a distance.
double node_radar_haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kEarthR = 6371000.0;  // mean Earth radius, metres
    constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
    const double dlat = (lat2 - lat1) * kDeg2Rad;
    const double dlon = (lon2 - lon1) * kDeg2Rad;
    const double a =
        std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
        std::cos(lat1 * kDeg2Rad) * std::cos(lat2 * kDeg2Rad) * std::sin(dlon * 0.5) *
            std::sin(dlon * 0.5);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthR * c;
}

// Map a metric onto 0..100 for the signal bar. RSSI in [-120,-40] dBm and SNR in
// [-20,+10] dB are clamped to the bar's full range; for signal-less nodes the
// bar reflects last-heard recency (fresh = full) so the row still reads at a
// glance. The TEXT always states the real metric; the bar is only an indicator.
int node_radar_bar_percent(const chat::contacts::NodeInfo& n)
{
    if (node_radar_has_rssi(n))
    {
        double pct = (n.rssi + 120.0) / 80.0 * 100.0;  // -120..-40 dBm -> 0..100
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        return static_cast<int>(pct + 0.5);
    }
    if (node_radar_has_snr(n))
    {
        double pct = (n.snr + 20.0) / 30.0 * 100.0;  // -20..+10 dB -> 0..100
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        return static_cast<int>(pct + 0.5);
    }
    // Recency-based fill for signal-less nodes: <=2 min -> full, fading to 10%
    // over an hour, so a recently-heard node still shows a strong-ish bar.
    if (n.last_seen == 0)
    {
        return 35;
    }
    const uint32_t now = sys::epoch_seconds_now();
    if (now <= n.last_seen || now < 1577836800u)
    {
        return 60;
    }
    const uint32_t age = now - n.last_seen;
    if (age <= 120u)
    {
        return 100;
    }
    if (age >= 3600u)
    {
        return 10;
    }
    const double frac = 1.0 - static_cast<double>(age - 120u) / static_cast<double>(3600u - 120u);
    int pct = 10 + static_cast<int>(frac * 90.0 + 0.5);
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    return pct;
}

lv_color_t node_radar_bar_color(int percent)
{
    if (percent >= 66)
    {
        return ui::theme::status_green();
    }
    if (percent >= 33)
    {
        return ui::theme::accent();
    }
    return ui::theme::error();
}

// Build one row in the list for a single node: name on the left, the real ranked
// metric + optional distance on the right, and a proportional signal bar below.
void node_radar_add_row(NodeRadarPageState* st, const chat::contacts::NodeInfo& n)
{
    if (!st || !st->list || !lv_obj_is_valid(st->list))
    {
        return;
    }

    lv_obj_t* row = lv_obj_create(st->list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, ui::theme::surface(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, ui::theme::border(), 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Header line: name (left, grows) + ranked metric (right).
    lv_obj_t* head = lv_obj_create(row);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 8, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    char namebuf[40];
    node_radar_node_name(n, namebuf, sizeof(namebuf));
    lv_obj_t* name_lbl = lv_label_create(head);
    lv_obj_set_flex_grow(name_lbl, 1);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_lbl, ui::theme::text(), 0);
    lv_label_set_text(name_lbl, namebuf);

    char metricbuf[32];
    if (node_radar_has_rssi(n))
    {
        std::snprintf(metricbuf, sizeof(metricbuf), "RSSI %.0f dBm", static_cast<double>(n.rssi));
    }
    else if (node_radar_has_snr(n))
    {
        std::snprintf(metricbuf, sizeof(metricbuf), "SNR %+0.1f dB", static_cast<double>(n.snr));
    }
    else
    {
        node_radar_seen_text(n.last_seen, metricbuf, sizeof(metricbuf));
    }
    lv_obj_t* metric_lbl = lv_label_create(head);
    lv_obj_set_style_text_font(metric_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(metric_lbl, ui::theme::text_muted(), 0);
    lv_label_set_text(metric_lbl, metricbuf);

    // Signal bar: a track with a proportional colored fill.
    const int pct = node_radar_bar_percent(n);
    lv_obj_t* track = lv_obj_create(row);
    lv_obj_set_width(track, LV_PCT(100));
    lv_obj_set_height(track, 12);
    lv_obj_set_style_bg_color(track, lv_color_hex(0xE6D6B8), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_radius(track, 6, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* fill = lv_obj_create(track);
    lv_obj_remove_style_all(fill);
    lv_obj_set_height(fill, LV_PCT(100));
    lv_obj_set_width(fill, LV_PCT(pct < 1 ? 1 : pct));
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(fill, node_radar_bar_color(pct), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, 6, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);

    // Distance line: only when the node has a valid position AND we have a fix.
    if (n.position.valid)
    {
        const gps::GpsState fix = platform::ui::gps::get_data();
        if (fix.valid)
        {
            const double node_lat = static_cast<double>(n.position.latitude_i) * 1e-7;
            const double node_lon = static_cast<double>(n.position.longitude_i) * 1e-7;
            const double dist_m =
                node_radar_haversine_m(fix.lat, fix.lng, node_lat, node_lon);
            char distbuf[40];
            if (dist_m < 1000.0)
            {
                std::snprintf(distbuf, sizeof(distbuf), LV_SYMBOL_GPS " ~%d m",
                              static_cast<int>(dist_m + 0.5));
            }
            else
            {
                std::snprintf(distbuf, sizeof(distbuf), LV_SYMBOL_GPS " ~%.1f km",
                              dist_m / 1000.0);
            }
            lv_obj_t* dist_lbl = lv_label_create(row);
            lv_obj_set_width(dist_lbl, LV_PCT(100));
            lv_obj_set_style_text_font(dist_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(dist_lbl, ui::theme::text_muted(), 0);
            lv_label_set_text(dist_lbl, distbuf);
        }
    }
}

// Query getNearby(), rank strongest-first, and (only if the set changed) rebuild
// the row list. Returns the freshly computed signature so the caller can store
// it. Reads the SAME source as Contacts; never mutates the contact service.
uint32_t node_radar_rebuild(NodeRadarPageState* st, bool force)
{
    if (!st || !st->list || !lv_obj_is_valid(st->list))
    {
        return st ? st->signature : 0u;
    }

    std::vector<chat::contacts::NodeInfo> nodes;
    if (app::hasAppFacade())
    {
        nodes = app::messagingFacade().getContactService().getNearby();
    }

    // Rank strongest-first: lower tier wins; within a tier, higher metric wins.
    std::sort(nodes.begin(), nodes.end(),
              [](const chat::contacts::NodeInfo& a, const chat::contacts::NodeInfo& b)
              {
                  const int ta = node_radar_tier(a);
                  const int tb = node_radar_tier(b);
                  if (ta != tb)
                  {
                      return ta < tb;
                  }
                  if (ta == 0)
                  {
                      if (a.rssi != b.rssi)
                      {
                          return a.rssi > b.rssi;
                      }
                  }
                  else if (ta == 1)
                  {
                      if (a.snr != b.snr)
                      {
                          return a.snr > b.snr;
                      }
                  }
                  if (a.last_seen != b.last_seen)
                  {
                      return a.last_seen > b.last_seen;  // more recent first
                  }
                  return a.node_id < b.node_id;  // stable, deterministic tie-break
              });

    // Signature of the ranked set: fold id, last_seen and quantized rssi/snr so a
    // changed/added/removed node (or a meaningfully changed signal) triggers a
    // rebuild, but identical ticks do not churn the UI.
    uint32_t sig = 2166136261u;  // FNV-1a-ish seed
    auto fold = [&sig](uint32_t v)
    {
        sig = (sig ^ v) * 16777619u;
    };
    fold(static_cast<uint32_t>(nodes.size()));
    for (const auto& n : nodes)
    {
        fold(n.node_id);
        fold(n.last_seen);
        const int32_t rssi_q = std::isnan(n.rssi) ? INT32_MIN
                                                   : static_cast<int32_t>(n.rssi);
        const int32_t snr_q = std::isnan(n.snr) ? INT32_MIN
                                                : static_cast<int32_t>(n.snr * 10.0f);
        fold(static_cast<uint32_t>(rssi_q));
        fold(static_cast<uint32_t>(snr_q));
    }

    if (!force && st->have_signature && sig == st->signature)
    {
        return sig;  // nothing changed; keep the existing rows
    }

    // Rebuild the list children from scratch.
    lv_obj_clean(st->list);

    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        char sbuf[40];
        if (nodes.empty())
        {
            std::snprintf(sbuf, sizeof(sbuf), "No nodes heard yet");
        }
        else
        {
            std::snprintf(sbuf, sizeof(sbuf), "%u node%s heard",
                          static_cast<unsigned>(nodes.size()),
                          nodes.size() == 1 ? "" : "s");
        }
        lv_label_set_text(st->status_label, sbuf);
    }

    for (const auto& n : nodes)
    {
        node_radar_add_row(st, n);
    }
    return sig;
}

void node_radar_tick(lv_timer_t* timer)
{
    auto* st = static_cast<NodeRadarPageState*>(lv_timer_get_user_data(timer));
    if (!st)
    {
        return;
    }
    const uint32_t sig = node_radar_rebuild(st, false);
    st->signature = sig;
    st->have_signature = true;
}

void node_radar_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<NodeRadarPageState*>(user_data);
    if (!state || !parent || (state->root && lv_obj_is_valid(state->root)))
    {
        return;
    }

    state->have_signature = false;
    state->signature = 0;

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
    lv_obj_set_style_pad_row(state->root, 10, 0);
    lv_obj_clear_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);

    // Back button: routes to the launcher menu via the same exit the Chat app
    // uses (exactly like companion_enter/help_enter/systest_enter).
    lv_obj_t* back_btn = lv_button_create(state->root);
    lv_obj_set_width(back_btn, LV_PCT(45));
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    // Page title.
    lv_obj_t* title = lv_label_create(state->root);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, ui::theme::accent(), 0);
    lv_label_set_text(title, "Node Radar");

    // Status line: node count (or "No nodes heard yet").
    state->status_label = lv_label_create(state->root);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(state->status_label, ui::theme::text_muted(), 0);
    lv_label_set_text(state->status_label, "Scanning...");

    // Scrollable list the node rows are added to (grows to fill remaining height).
    state->list = lv_obj_create(state->root);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_flex_grow(state->list, 1);
    lv_obj_set_style_bg_opa(state->list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state->list, 0, 0);
    lv_obj_set_style_pad_all(state->list, 0, 0);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->list, 8, 0);
    lv_obj_set_scroll_dir(state->list, LV_DIR_VER);
    lv_obj_add_flag(state->list, LV_OBJ_FLAG_SCROLLABLE);

    // Populate immediately, then refresh every ~2s (rebuild only on set change).
    const uint32_t sig = node_radar_rebuild(state, true);
    state->signature = sig;
    state->have_signature = true;
    state->timer = lv_timer_create(node_radar_tick, 2000, state);
}

void node_radar_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<NodeRadarPageState*>(user_data);
    if (!state)
    {
        return;
    }
    if (state->timer)
    {
        lv_timer_del(state->timer);
        state->timer = nullptr;
    }
    if (!state->root || !lv_obj_is_valid(state->root))
    {
        state->root = nullptr;
        state->list = nullptr;
        state->status_label = nullptr;
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
    state->list = nullptr;
    state->status_label = nullptr;
}

ui::CallbackAppScreen s_node_radar_app("node_radar", "Node Radar", &Chat, node_radar_enter,
                                       node_radar_exit, &s_node_radar_state);

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

// Map (the full offline-tile map: your-position marker + mesh node markers +
// tracker/route overlays). stable_id = 'map'; screen dir is screens/gps. Mirrors
// the chat/contacts/settings/sky_plot binding: the gps page shell's enter/exit
// take a ui::page::Host* (gps::ui::shell::Host is an alias of ::ui::page::Host) as
// user_data and route the back request through ui_request_exit_to_menu() via the
// menu host. The shell wraps the runtime with the header-only page_shell_fallback
// template; its placeholder_page::show/hide (non-inline) TU is ALREADY linked via
// TRAILMATE_ESP_IDF_GNSS_UI_SOURCES, so it is not repeated in the GPS set.
// is_available() == platform::ui::device::gps_supported() == true on the P4, so
// the live map runtime is entered: it builds the offline-tile map (map_viewport +
// the arduino_common map_tiles engine, both already compiled for the Contacts
// node-detail mini-map) and reads the fix/team/route via the platform::ui
// producers (gps/tracker/route_storage/team_ui_store, all in the PLATFORM block).
// It degrades gracefully with no SD/fix (empty tiles, default view), and exit()
// tears down synchronously via gps::ui::runtime::exit (timers deleted, overlays/
// tiles cleaned, root deleted), so the boot self-test enters+exits cleanly.
ui::page::Host s_gps_menu_host = make_menu_host();

ui::CallbackAppScreen s_gps_app("map",
                                "Map",
                                &Setting,
                                gps::ui::shell::enter,
                                gps::ui::shell::exit,
                                &s_gps_menu_host);

// Extensions (Wi-Fi / companion extensions status panel: language-pack catalog,
// install/update/uninstall + per-package detail). stable_id = 'extensions'.
// Mirrors the chat/contacts/settings binding: the extensions page shell's
// enter/exit take a ui::page::Host* as user_data and route the back request
// through ui_request_exit_to_menu() via the menu host. The screen reads catalog
// state through platform::ui::wifi::status() + ui::runtime::packs::fetch_catalog()
// (the ESP-IDF pack_repository backend), both compiled producers; it does no
// network I/O on enter (the catalog fetch returns early unless Wi-Fi is connected
// and the self-test board is offline), and exit() just deletes the root with no
// queued async work, so it tears down cleanly in the boot self-test.
ui::page::Host s_extensions_menu_host = make_menu_host();

ui::CallbackAppScreen s_extensions_app("extensions",
                                       "Extensions",
                                       &Setting,
                                       extensions::ui::shell::enter,
                                       extensions::ui::shell::exit,
                                       &s_extensions_menu_host);

// Tracker (record/list/delete GPS tracks + KML route files). stable_id =
// 'tracker'. Mirrors the chat/contacts/settings binding: the tracker page shell's
// enter/exit take a ui::page::Host* (tracker::ui::shell::Host is an alias of
// ::ui::page::Host) as user_data and route the back request through
// ui_request_exit_to_menu() via the menu host. The shell wraps the runtime in the
// header-only page_shell_fallback template: when neither platform::ui::tracker nor
// platform::ui::route_storage is supported it shows the shared placeholder_page;
// on this board both read the SD via bsp_runtime, so the live runtime is entered.
// It degrades gracefully when no SD card is present (refresh_record_list /
// refresh_route_list short-circuit to "No SD Card" when device::sd_ready() is
// false), so the boot self-test enters+exits cleanly whatever the SD state. Backing
// producers platform::ui::tracker (platform_ui_tracker_runtime.cpp) and
// platform::ui::route_storage (platform_ui_route_storage.cpp) are compiled in the
// PLATFORM block; teardown is synchronous (cleanup_page deletes modals/group/root
// via lv_obj_del, no queued lv_async_call), so no async-cancel is required.
ui::page::Host s_tracker_menu_host = make_menu_host();

ui::CallbackAppScreen s_tracker_app("tracker",
                                    "Tracker",
                                    &Setting,
                                    tracker::ui::shell::enter,
                                    tracker::ui::shell::exit,
                                    &s_tracker_menu_host);

// Energy Sweep (LoRa RSSI spectrum sweep over the configured region band).
// stable_id = 'energy_sweep'. Mirrors the chat/contacts/settings binding: the
// energy-sweep page shell's enter/exit take a ui::page::Host* (energy_sweep::ui::
// shell::Host is an alias of ::ui::page::Host) as user_data and route the back
// request through ui_request_exit_to_menu() via the menu host. The shell wraps the
// runtime with the header-only page_shell_fallback template; placeholder_page::
// show/hide (non-inline) is ALREADY linked via TRAILMATE_ESP_IDF_GNSS_UI_SOURCES,
// so it is intentionally NOT repeated in the energy-sweep set (a second copy would
// be a duplicate-symbol link error). is_available() == lora::is_supported() ==
// kBoardProfile.has_lora (true on tdisplayp4_tft), so the live runtime is entered.
// enter() runs purely simulated (init_sweep_state); it does NOT acquire the shared
// SX126x radio -- the radio is acquired lazily only when SCAN is pressed
// (acquire_radio_runtime), and the boot self-test only enters+exits, so no radio
// contention with the chat radio occurs. exit() tears down synchronously
// (teardown_radio_context releases only if a scan acquired the radio; lv_timer_del
// of the refresh timer; lv_obj_del of the root) with no queued lv_async_call, so it
// is self-test safe. Backing producer platform::ui::lora
// (platform_ui_lora_runtime.cpp over Sx126xRadio) is added to the PLATFORM block;
// platform::ui::screen sleep control (disable_sleep/enable_sleep) is already
// provided inline by screen_sleep.cpp, and device::delay_ms by the device runtime.
ui::page::Host s_energy_sweep_menu_host = make_menu_host();

ui::CallbackAppScreen s_energy_sweep_app("energy_sweep",
                                         "Sub-GHz Scan",
                                         &Setting,
                                         energy_sweep::ui::shell::enter,
                                         energy_sweep::ui::shell::exit,
                                         &s_energy_sweep_menu_host);

// PC Link (USB-CDC host bridge: shows link state + RX/TX frame counters; in
// RNode-protocol mode it presents as a KISS modem for Reticulum). stable_id =
// 'pc_link'. Mirrors the chat/contacts/settings binding: the pc_link page shell's
// enter/exit take a ui::page::Host* (pc_link::ui::shell::Host is an alias of
// ::ui::page::Host) as user_data and route the back request through
// ui_request_exit_to_menu() via the menu host. The shell wraps the runtime with
// the header-only page_shell_fallback template; placeholder_page::show/hide
// (non-inline) is ALREADY linked via TRAILMATE_ESP_IDF_GNSS_UI_SOURCES, so it is
// not repeated in the PC Link set. is_available() ==
// platform::ui::hostlink::is_supported() == true on the P4 (USB-Serial-JTAG), so
// the live runtime is entered. enter() starts the hostlink task (which just waits
// for a host during the boot self-test, none attached) and exit() stops it +
// deletes the root synchronously (lv_timer_del of the refresh timer, no queued
// lv_async_call), so it is self-test safe. The screen labels itself from
// app::appFacade().getMeshProtocol() (provided by IdfChatFacade).
ui::page::Host s_pc_link_menu_host = make_menu_host();

ui::CallbackAppScreen s_pc_link_app("pc_link",
                                    "PC Link",
                                    &Setting,
                                    pc_link::ui::shell::enter,
                                    pc_link::ui::shell::exit,
                                    &s_pc_link_menu_host);

AppScreen* s_apps[] = {&s_chat_app,
                       &s_contacts_app,
                       &s_settings_app,
                       &s_skyplot_app,
                       &s_gps_app,
                       &s_extensions_app,
                       &s_tracker_app,
                       &s_energy_sweep_app,
                       &s_pc_link_app,
                       &s_companion_app,
                       &s_snake_app,
                       &s_tetris_app,
                       &s_help_app,
                       &s_systest_app,
                       &s_gps_position_app,
                       &s_g2048_app,
                       &s_flashlight_app,
                       &s_stopwatch_app,
                       &s_node_radar_app};
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
