#include "ui/app_registry.h"

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
#include "ui/screens/pc_link/pc_link_page_shell.h"
#include "ui/screens/settings/settings_page_shell.h"
#include "ui/screens/tracker/tracker_page_shell.h"
#include "ui/ui_theme.h"

#include <cstdint>
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
    static constexpr int kCellPx = 30;
    static constexpr int kCellCount = kCols * kRows;

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
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 84, 64);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, st);
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
    lv_obj_set_style_pad_all(state->root, 10, 0);
    lv_obj_set_flex_flow(state->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->root, 8, 0);

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
    state->rng = lv_tick_get() ^ 0x9E3779B9u;
    state->timer = lv_timer_create(snake_tick, 180, state);
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
    static constexpr int kCellPx = 22;  // 10*22 = 220px board width (portrait-safe)
    static constexpr int kCellCount = kCols * kRows;
    static constexpr int kEmpty = -1;

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
        lv_timer_resume(st->timer);
    }
    tetris_render(st);
}

lv_obj_t* tetris_make_button(lv_obj_t* parent,
                             TetrisPageState* st,
                             const char* text,
                             lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 66, 56);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, st);
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
    lv_obj_set_style_pad_all(state->root, 10, 0);
    lv_obj_set_flex_flow(state->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->root, 8, 0);

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

    // Controls row 1: Left, Rotate, Right.
    lv_obj_t* row1 = lv_obj_create(state->root);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, 10, 0);
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

    // Controls row 2: Soft drop (Down), Hard drop.
    lv_obj_t* row2 = lv_obj_create(state->root);
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, 10, 0);
    tetris_make_button(
        row2, state, LV_SYMBOL_DOWN,
        [](lv_event_t* e)
        { tetris_soft_drop(static_cast<TetrisPageState*>(lv_event_get_user_data(e))); });
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
    state->rng = lv_tick_get() ^ 0x2545F491u;
    state->timer = lv_timer_create(tetris_tick, 600, state);
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
                       &s_extensions_app,
                       &s_tracker_app,
                       &s_energy_sweep_app,
                       &s_pc_link_app,
                       &s_companion_app,
                       &s_snake_app,
                       &s_tetris_app};
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
