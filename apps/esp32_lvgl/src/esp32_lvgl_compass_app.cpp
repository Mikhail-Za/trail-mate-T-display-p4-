// Compass: a GPS-course "compass" for a device with NO magnetometer.
//
// There is no magnetometer driver on this board, so there is no absolute heading
// while stationary. The only heading source is GPS course-over-ground, which is
// physically meaningful ONLY while moving (the receiver derives it from successive
// position fixes). This app is honest about that: it shows a north-up dial with a
// needle pointing at the current GPS course while you are moving, and clearly says
// "move to get a heading" when you are stopped -- it never shows a stale/garbage
// bearing.
//
// Self-contained inline app, same pattern as Field Guide / Translate: file-static
// state, enter()/exit() build and tear down everything, back routes through
// ui_request_exit_to_menu(). A periodic lv_timer refreshes the needle and readouts.

#include "lvgl.h"
#include "platform/ui/gps_runtime.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/menu/dashboard/dashboard_style.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{

// Below this ground speed the GPS course is noise, not a heading. ~0.5 m/s is about
// 1.8 km/h -- a slow walk. Under it we treat the device as stationary and hide the
// needle rather than spin it on jitter.
constexpr float kMinSpeedMps = 0.5f;
// Hysteresis lower band: once a heading is showing we keep it until speed drops below
// this, so a walking pace hovering around kMinSpeedMps doesn't flicker the needle
// shown/hidden every 300ms tick. Only the speed threshold is hysteretic -- fix/course/
// speed validity stay hard gates.
constexpr float kStopSpeedMps = 0.3f;
constexpr float kPi = 3.14159265358979323846f;
constexpr uint32_t kRefreshMs = 300;

struct CompassAppState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* dial = nullptr;
    lv_obj_t* needle = nullptr;
    lv_obj_t* center_dot = nullptr;
    lv_obj_t* n_label = nullptr;
    lv_obj_t* e_label = nullptr;
    lv_obj_t* s_label = nullptr;
    lv_obj_t* w_label = nullptr;
    lv_obj_t* heading_label = nullptr;
    lv_obj_t* speed_label = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_timer_t* timer = nullptr;

    // LVGL's lv_line only stores the ADDRESS of the point array (see lv_line.h: "Only
    // the address is saved, so the array needs to be alive while the line exists").
    // Keeping it in the file-static state makes it outlive every tick -- a stack-local
    // array recomputed per tick would be a use-after-scope on the next redraw.
    lv_point_precise_t needle_points[2] = {};

    // Geometry cached at enter() so the tick doesn't re-query the panel each time.
    float center = 0.0f;     // dial center in dial-local coords (== diameter/2)
    float needle_len = 0.0f; // needle length from center toward the course tip

    // Hysteresis memory for the speed threshold (see kStopSpeedMps): true while the
    // heading is currently shown. Reset to false on enter().
    bool was_moving = false;
};

CompassAppState s_state;

// Recompute the needle and text readouts from the latest GPS fix. Split out of the
// timer callback so enter() can paint one frame immediately (no blank first 300 ms).
void update_compass(CompassAppState* st)
{
    if (!st || !st->root || !lv_obj_is_valid(st->root))
    {
        return;
    }

    const gps::GpsState fix = platform::ui::gps::get_data();
    const bool has_fix = fix.valid;
    // GPS course is only a heading while actually moving AND while the fix is valid with
    // live course+speed. On fix loss the upstream parser early-returns and leaves
    // has_course/course_deg/speed_mps FROZEN at their last values; without these hard
    // gates a mid-motion fix loss would keep the needle frozen on a stale bearing. So
    // fix/course/speed validity are hard gates (a lost fix or missing course blanks
    // immediately); only the speed threshold carries hysteresis so a walking pace near
    // kMinSpeedMps doesn't flicker.
    const bool gps_heading_live = fix.valid && fix.has_course && fix.has_speed;
    bool moving;
    if (!gps_heading_live)
    {
        moving = false; // lost fix / no course / no speed -> blank immediately
    }
    else if (st->was_moving)
    {
        moving = fix.speed_mps > kStopSpeedMps; // keep heading until below the low band
    }
    else
    {
        moving = fix.speed_mps > kMinSpeedMps; // only start once above the high band
    }
    st->was_moving = moving;

    // Speed readout carries the same hard gates as the needle: shown only when the fix is
    // valid AND reports speed (even ~0 while stopped, since the heading depends on speed).
    // On fix loss the upstream parser freezes has_speed/speed_mps at their last values, so
    // without the fix.valid gate the readout would keep showing a stale speed while the
    // needle is hidden and the status says "No GPS fix" -- it now blanks in lockstep.
    if (fix.valid && fix.has_speed)
    {
        const double kmh = fix.speed_mps * 3.6;
        const double mph = fix.speed_mps * 2.2369362920544;
        char sbuf[48];
        std::snprintf(sbuf, sizeof(sbuf), "%.1f km/h   %.1f mph", kmh, mph);
        lv_label_set_text(st->speed_label, sbuf);
    }
    else
    {
        lv_label_set_text(st->speed_label, "-- km/h   -- mph");
    }

    if (moving)
    {
        const float course = ui::menu::dashboard::normalize_degrees(static_cast<float>(fix.course_deg));
        int deg = static_cast<int>(std::lroundf(course));
        if (deg >= 360)
        {
            deg -= 360;
        }
        if (deg < 0)
        {
            deg += 360;
        }
        const char* rose = ui::menu::dashboard::compass_rose(course);
        char hbuf[32];
        std::snprintf(hbuf, sizeof(hbuf), "%03ddeg  %s", deg, rose);
        lv_label_set_text(st->heading_label, hbuf);

        // North-up dial: angle 0 = straight up (North), increasing clockwise. Screen y
        // grows downward, so dy = -cos and dx = sin put 0deg at the top and 90deg to the
        // right. The point array is recomputed in place and re-handed to the line each
        // tick (LVGL keeps only the pointer, so mutating st->needle_points is enough, but
        // lv_line_set_points also re-marks the widget dirty for redraw).
        const float rad = course * (kPi / 180.0f);
        const float dx = std::sin(rad);
        const float dy = -std::cos(rad);
        st->needle_points[0].x = static_cast<lv_value_precise_t>(st->center);
        st->needle_points[0].y = static_cast<lv_value_precise_t>(st->center);
        st->needle_points[1].x = static_cast<lv_value_precise_t>(st->center + dx * st->needle_len);
        st->needle_points[1].y = static_cast<lv_value_precise_t>(st->center + dy * st->needle_len);
        lv_line_set_points(st->needle, st->needle_points, 2);
        lv_obj_clear_flag(st->needle, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(st->status_label, "Heading from GPS course (valid while moving)");
        lv_obj_set_style_text_color(st->status_label, ui::menu::dashboard::color_text_dim(), 0);
    }
    else
    {
        // Stationary / no course / no fix: no honest heading exists. Hide the needle and
        // blank the numeric readout instead of freezing on a stale bearing.
        lv_label_set_text(st->heading_label, "---deg  --");
        lv_obj_add_flag(st->needle, LV_OBJ_FLAG_HIDDEN);
        if (!has_fix)
        {
            lv_label_set_text(st->status_label, "No GPS fix. Waiting for satellites...");
            lv_obj_set_style_text_color(st->status_label, ui::menu::dashboard::color_warn(), 0);
        }
        else
        {
            lv_label_set_text(st->status_label, "Move to get a heading (GPS compass needs motion)");
            lv_obj_set_style_text_color(st->status_label, ui::menu::dashboard::color_amber(), 0);
        }
    }
}

void compass_tick(lv_timer_t* t)
{
    auto* st = static_cast<CompassAppState*>(lv_timer_get_user_data(t));
    if (!st)
    {
        return;
    }
    update_compass(st);
}

lv_obj_t* make_cardinal_label(lv_obj_t* dial, const char* text, lv_align_t align, lv_color_t color)
{
    lv_obj_t* lbl = lv_label_create(dial);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_label_set_text(lbl, text);
    // Small inward inset so the glyphs sit inside the ring rather than on the border.
    lv_coord_t inset = 8;
    switch (align)
    {
        case LV_ALIGN_TOP_MID:
            lv_obj_align(lbl, align, 0, inset);
            break;
        case LV_ALIGN_BOTTOM_MID:
            lv_obj_align(lbl, align, 0, -inset);
            break;
        case LV_ALIGN_LEFT_MID:
            lv_obj_align(lbl, align, inset, 0);
            break;
        case LV_ALIGN_RIGHT_MID:
            lv_obj_align(lbl, align, -inset, 0);
            break;
        default:
            lv_obj_align(lbl, align, 0, 0);
            break;
    }
    return lbl;
}

void compass_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<CompassAppState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }
    // Defensive: never leak a timer if enter() is somehow re-run without a prior exit().
    if (st->timer)
    {
        lv_timer_del(st->timer);
        st->timer = nullptr;
    }

    st->root = lv_obj_create(parent);
    lv_obj_set_size(st->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(st->root, 0, 0);
    lv_obj_set_style_border_width(st->root, 0, 0);
    lv_obj_set_style_radius(st->root, 0, 0);
    lv_obj_clear_flag(st->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(st->root, LV_FLEX_FLOW_COLUMN);

    // --- Top bar: Back + title -------------------------------------------------
    lv_obj_t* bar = lv_obj_create(st->root);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 12, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_button_create(bar);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* title = lv_label_create(bar);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_text(title, "Compass");

    // --- Content: dial + readouts, centered -----------------------------------
    lv_obj_t* content = lv_obj_create(st->root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 12, 0);
    lv_obj_set_style_pad_row(content, 14, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Size the dial from the actual panel so it works on BOTH physical resolutions
    // (540x1168 and 568x1232) -- never hardcode. Diameter = min(width, half height) * 0.8,
    // which lands well under the width on either panel and leaves room for the readouts.
    lv_coord_t hor = lv_display_get_horizontal_resolution(NULL);
    lv_coord_t ver = lv_display_get_vertical_resolution(NULL);
    if (hor <= 0)
    {
        hor = 540; // boot self-test / no display attached
    }
    if (ver <= 0)
    {
        ver = 1168;
    }
    float limit = static_cast<float>(hor);
    const float half_h = static_cast<float>(ver) * 0.5f;
    if (half_h < limit)
    {
        limit = half_h;
    }
    lv_coord_t diameter = static_cast<lv_coord_t>(limit * 0.8f);
    st->center = static_cast<float>(diameter) * 0.5f;
    st->needle_len = static_cast<float>(diameter) * 0.42f;

    st->dial = lv_obj_create(content);
    lv_obj_set_size(st->dial, diameter, diameter);
    lv_obj_set_style_radius(st->dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(st->dial, 0, 0);
    lv_obj_set_style_border_width(st->dial, 3, 0);
    lv_obj_set_style_border_color(st->dial, ui::menu::dashboard::color_line(), 0);
    lv_obj_set_style_bg_color(st->dial, ui::menu::dashboard::color_panel_bg(), 0);
    lv_obj_clear_flag(st->dial, LV_OBJ_FLAG_SCROLLABLE);

    // Cardinal labels, North-up. North is amber to stress that the dial is fixed with
    // North at the top (the needle moves, not the ring).
    st->n_label = make_cardinal_label(st->dial, "N", LV_ALIGN_TOP_MID, ui::menu::dashboard::color_amber());
    st->e_label = make_cardinal_label(st->dial, "E", LV_ALIGN_RIGHT_MID, ui::menu::dashboard::color_text());
    st->s_label = make_cardinal_label(st->dial, "S", LV_ALIGN_BOTTOM_MID, ui::menu::dashboard::color_text());
    st->w_label = make_cardinal_label(st->dial, "W", LV_ALIGN_LEFT_MID, ui::menu::dashboard::color_text());

    // Needle: an lv_line whose points live in st->needle_points (persistent). It covers
    // the whole dial and is positioned at (0,0) so its point coordinates are in dial-local
    // space with the center at (diameter/2, diameter/2).
    st->needle = lv_line_create(st->dial);
    lv_obj_set_size(st->needle, diameter, diameter);
    lv_obj_set_pos(st->needle, 0, 0);
    lv_obj_set_style_line_width(st->needle, 6, 0);
    lv_obj_set_style_line_rounded(st->needle, true, 0);
    lv_obj_set_style_line_color(st->needle, ui::menu::dashboard::color_amber(), 0);
    lv_obj_clear_flag(st->needle, LV_OBJ_FLAG_SCROLLABLE);
    // Start hidden; the first update() reveals it only if we are actually moving.
    lv_obj_add_flag(st->needle, LV_OBJ_FLAG_HIDDEN);
    st->needle_points[0].x = static_cast<lv_value_precise_t>(st->center);
    st->needle_points[0].y = static_cast<lv_value_precise_t>(st->center);
    st->needle_points[1].x = static_cast<lv_value_precise_t>(st->center);
    st->needle_points[1].y = static_cast<lv_value_precise_t>(st->center - st->needle_len);
    lv_line_set_points(st->needle, st->needle_points, 2);

    // Center hub, drawn last so it caps the needle base.
    st->center_dot = lv_obj_create(st->dial);
    lv_obj_set_size(st->center_dot, 16, 16);
    lv_obj_set_style_radius(st->center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(st->center_dot, 0, 0);
    lv_obj_set_style_bg_color(st->center_dot, ui::menu::dashboard::color_amber(), 0);
    lv_obj_clear_flag(st->center_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(st->center_dot);

    // --- Readouts below the dial ----------------------------------------------
    st->heading_label = lv_label_create(content);
    lv_obj_set_style_text_font(st->heading_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(st->heading_label, ui::menu::dashboard::color_text(), 0);
    lv_label_set_text(st->heading_label, "---deg  --");

    st->speed_label = lv_label_create(content);
    lv_obj_set_style_text_font(st->speed_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(st->speed_label, ui::menu::dashboard::color_text_dim(), 0);
    lv_label_set_text(st->speed_label, "-- km/h   -- mph");

    st->status_label = lv_label_create(content);
    lv_obj_set_width(st->status_label, LV_PCT(90));
    lv_label_set_long_mode(st->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(st->status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(st->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(st->status_label, ui::menu::dashboard::color_text_dim(), 0);
    lv_label_set_text(st->status_label, "Move to get a heading (GPS compass needs motion)");

    // Persistent honesty note: this device has no magnetometer, so there is no heading
    // at all while still. Stated up front, not just implied by the dimmed needle.
    lv_obj_t* note = lv_label_create(content);
    lv_obj_set_width(note, LV_PCT(90));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(note, ui::menu::dashboard::color_text_dim(), 0);
    lv_label_set_text(note, "North-up dial. No magnetometer on this device; heading comes from GPS motion.");

    // Fresh entry: no heading shown yet, so hysteresis must require the high band first.
    st->was_moving = false;

    // Paint one frame now, then refresh on a timer for responsiveness.
    update_compass(st);
    st->timer = lv_timer_create(compass_tick, kRefreshMs, st);
}

void compass_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<CompassAppState*>(user_data);
    if (!st)
    {
        return;
    }
    // Delete the timer BEFORE the tree so a pending tick can never touch freed widgets.
    if (st->timer)
    {
        lv_timer_del(st->timer);
        st->timer = nullptr;
    }
    if (st->root && lv_obj_is_valid(st->root))
    {
        lv_obj_del(st->root);
    }
    st->root = nullptr;
    st->dial = nullptr;
    st->needle = nullptr;
    st->center_dot = nullptr;
    st->n_label = nullptr;
    st->e_label = nullptr;
    st->s_label = nullptr;
    st->w_label = nullptr;
    st->heading_label = nullptr;
    st->speed_label = nullptr;
    st->status_label = nullptr;
}

extern "C"
{
    extern const lv_image_dsc_t gps_topbar;
}

} // namespace

ui::CallbackAppScreen g_compass_app("compass",
                                    "Compass",
                                    &gps_topbar,
                                    compass_enter,
                                    compass_exit,
                                    &s_state);
