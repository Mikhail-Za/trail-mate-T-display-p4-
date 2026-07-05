// Trip Computer: accumulates trip statistics from the live GPS stream while the
// app is open. Current / max / moving-average speed, total distance travelled,
// moving time, current altitude and cumulative ascent. A Reset button clears the
// trip; a reboot clears it too.
//
// HONEST SCOPE (v1): stats accumulate ONLY while this screen is open. The trip
// survives leaving and re-entering the app within a session (the accumulators are
// file-static), but nothing is sampled while another app is on screen. The on-screen
// note states this plainly.
//
// Self-contained inline app, same pattern as C6 Companion / Snake / Field Guide:
// file-static state, enter()/exit() build and tear down the UI, a ~1 s lv_timer
// samples the GPS, and back routes through ui_request_exit_to_menu().
//
// GPS-only jitter handling (no barometer, single-frequency receiver):
//   * Distance uses an ANCHOR (distance filter): displacement is committed only when
//     it exceeds a jitter floor from the last committed point. This rejects at-rest
//     GPS wander (which would otherwise inflate distance by hundreds of metres) while
//     still capturing a slow, steady walk, whose displacement from the anchor grows
//     monotonically until it crosses the floor (a plain per-sample floor at 1 Hz would
//     discard every sub-floor step and record nothing for a walker).
//   * Ascent uses a HYSTERESIS reference: a rise is counted only when it exceeds a
//     floor above the running low point; descents freely lower the reference. This
//     suppresses noisy at-rest vertical wander while still logging sustained climbs.

#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"

#include "platform/ui/gps_runtime.h"
#include "ui/menu/dashboard/dashboard_style.h"

#include <cstdint>
#include <cstdio>

namespace
{

namespace dashboard = ::ui::menu::dashboard;

// Tracker top-bar glyph, reused as the launcher icon (defined in
// modules/ui_shared/src/ui/assets/tracker_topbar.c, compiled into both the IDF
// and Linux UI-shared source sets).
extern "C"
{
    extern const lv_image_dsc_t tracker_topbar;
}

// --- Jitter floors (see the file header for the rationale) -----------------------
// Distance: ~1.3x a typical single-frequency horizontal CEP. Combined with the
// anchor filter it rejects at-rest wander yet still logs a slow steady walk.
constexpr double kDistanceFloorM = 4.0;
// Ascent: raised above a naive ~1 m because GPS-only vertical error is ~1.5-2x the
// horizontal and oscillates several metres at rest; a 1 m floor would admit that as
// phantom ascent. With the hysteresis reference, 4 m suppresses at-rest wander while
// a sustained climb still accrues from the running low point once it clears 4 m.
constexpr double kElevationFloorM = 4.0;
// Speed at/above which the device is considered moving (~1.8 km/h).
constexpr double kMoveSpeedMps = 0.5;
// A sample-to-sample gap larger than this (app was closed / GPS was lost) is treated
// as a discontinuity: the references are re-baselined and nothing is folded into the
// trip for that interval (so re-entering the app never injects a phantom segment or a
// huge slug of moving time). Normal cadence is ~1000 ms.
constexpr uint32_t kMaxGapMs = 5000;

struct TripState
{
    // ---- Persistent trip accumulators (survive re-entry; cleared by Reset/reboot) ----
    double total_distance_m = 0.0;
    uint32_t moving_time_ms = 0;
    double max_speed_mps = 0.0;
    double ascent_m = 0.0;

    // Current instantaneous readouts (last known; shown even after a fix is lost).
    double cur_speed_mps = 0.0;
    double cur_alt_m = 0.0;
    bool has_cur_alt = false;
    bool any_fix = false; // have we had any valid fix this trip?

    // Per-sample reference (updated on every valid fix): dt, derived speed, moving.
    bool has_prev = false;
    double prev_lat = 0.0;
    double prev_lng = 0.0;
    uint32_t prev_tick = 0;

    // Distance anchor (last committed point): only advances when displacement > floor.
    bool has_anc = false;
    double anc_lat = 0.0;
    double anc_lng = 0.0;
    uint32_t anc_tick = 0;

    // Elevation reference (running low point) for the hysteresis ascent counter.
    bool has_alt_ref = false;
    double alt_ref = 0.0;

    // ---- Per-enter UI (rebuilt on enter, nulled on exit; NOT accumulators) ----
    lv_obj_t* root = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* val_distance = nullptr;
    lv_obj_t* val_cur_speed = nullptr;
    lv_obj_t* val_max_speed = nullptr;
    lv_obj_t* val_avg_speed = nullptr;
    lv_obj_t* val_moving_time = nullptr;
    lv_obj_t* val_altitude = nullptr;
    lv_obj_t* val_ascent = nullptr;
    lv_timer_t* timer = nullptr;
    bool live_fix = false; // was the most recent sample a valid fix?
};

TripState s_state;

void trip_update_labels(TripState* st);
void trip_reset(TripState* st);

lv_obj_t* add_stat_label(lv_obj_t* parent)
{
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    return lbl;
}

// Speeds are shown in BOTH km/h and mph, from a single m/s value.
void set_speed_label(lv_obj_t* label, const char* caption, double mps)
{
    if (!label || !lv_obj_is_valid(label))
    {
        return;
    }
    char buf[72];
    std::snprintf(buf, sizeof(buf), "%s: %.1f km/h  (%.1f mph)", caption, mps * 3.6,
                  mps * 2.23694);
    lv_label_set_text(label, buf);
}

// Fold one valid fix into the trip. now = lv_tick_get() at sample time.
void trip_accumulate(TripState* st, const gps::GpsState& fix, uint32_t now)
{
    st->any_fix = true;

    // First fix of the trip, or resuming after a gap (app closed / GPS lost):
    // (re)baseline every reference and accumulate nothing for this interval.
    const uint32_t dt_ms = st->has_prev ? (now - st->prev_tick) : 0u; // unsigned wrap-safe
    if (!st->has_prev || dt_ms > kMaxGapMs)
    {
        st->prev_lat = fix.lat;
        st->prev_lng = fix.lng;
        st->prev_tick = now;
        st->has_prev = true;
        st->anc_lat = fix.lat;
        st->anc_lng = fix.lng;
        st->anc_tick = now;
        st->has_anc = true;
        if (fix.has_alt)
        {
            st->alt_ref = fix.alt_m;
            st->has_alt_ref = true;
            st->cur_alt_m = fix.alt_m;
            st->has_cur_alt = true;
        }
        if (fix.has_speed)
        {
            st->cur_speed_mps = fix.speed_mps;
        }
        return;
    }

    const double dt_s = dt_ms / 1000.0;
    const double seg_prev =
        dashboard::haversine_m(st->prev_lat, st->prev_lng, fix.lat, fix.lng);

    // ---- Current + max speed ----
    // Prefer the receiver's own speed (Doppler-derived, ~0 at rest). Only fall back
    // to position-derived speed when the fix carries none; and only trust that noisy
    // fallback for MAX when the sample moved past the jitter floor.
    if (fix.has_speed)
    {
        st->cur_speed_mps = fix.speed_mps;
        if (fix.speed_mps > st->max_speed_mps)
        {
            st->max_speed_mps = fix.speed_mps;
        }
    }
    else if (dt_s > 0.05)
    {
        const double v = seg_prev / dt_s;
        st->cur_speed_mps = v; // best-effort; position-derived, noisy
        if (seg_prev > kDistanceFloorM && v > st->max_speed_mps)
        {
            st->max_speed_mps = v;
        }
    }

    // ---- Moving this interval? ----
    bool moving;
    if (fix.has_speed)
    {
        moving = fix.speed_mps > kMoveSpeedMps;
    }
    else
    {
        moving = (dt_s > 0.0) && ((seg_prev / dt_s) > kMoveSpeedMps);
    }

    // ---- Total distance via the anchor (distance filter) ----
    if (st->has_anc)
    {
        const double d = dashboard::haversine_m(st->anc_lat, st->anc_lng, fix.lat, fix.lng);
        if (d > kDistanceFloorM)
        {
            st->total_distance_m += d;
            st->anc_lat = fix.lat;
            st->anc_lng = fix.lng;
            st->anc_tick = now;
            moving = true; // genuine displacement crossed the floor
        }
    }

    // ---- Moving time (per-sample dt while moving) ----
    if (moving)
    {
        st->moving_time_ms += dt_ms;
    }

    // ---- Cumulative ascent (hysteresis around a running low reference) ----
    if (fix.has_alt)
    {
        st->cur_alt_m = fix.alt_m;
        st->has_cur_alt = true;
        if (!st->has_alt_ref)
        {
            st->alt_ref = fix.alt_m;
            st->has_alt_ref = true;
        }
        else if (fix.alt_m > st->alt_ref + kElevationFloorM)
        {
            st->ascent_m += (fix.alt_m - st->alt_ref);
            st->alt_ref = fix.alt_m;
        }
        else if (fix.alt_m < st->alt_ref)
        {
            st->alt_ref = fix.alt_m; // new low; track it, do not count as ascent
        }
    }

    // ---- Advance the per-sample reference ----
    st->prev_lat = fix.lat;
    st->prev_lng = fix.lng;
    st->prev_tick = now;
}

void trip_update_labels(TripState* st)
{
    if (!st->root || !lv_obj_is_valid(st->root))
    {
        return;
    }

    char buf[72];

    if (st->val_distance && lv_obj_is_valid(st->val_distance))
    {
        char dbuf[24];
        dashboard::format_distance(st->total_distance_m, dbuf, sizeof(dbuf));
        std::snprintf(buf, sizeof(buf), "Distance: %s", dbuf);
        lv_label_set_text(st->val_distance, buf);
    }

    set_speed_label(st->val_cur_speed, "Speed", st->cur_speed_mps);
    set_speed_label(st->val_max_speed, "Max speed", st->max_speed_mps);

    const double avg_mps =
        (st->moving_time_ms > 0u) ? (st->total_distance_m / (st->moving_time_ms / 1000.0)) : 0.0;
    set_speed_label(st->val_avg_speed, "Avg (moving)", avg_mps);

    if (st->val_moving_time && lv_obj_is_valid(st->val_moving_time))
    {
        const uint32_t total_s = st->moving_time_ms / 1000u;
        const unsigned h = static_cast<unsigned>(total_s / 3600u);
        const unsigned m = static_cast<unsigned>((total_s % 3600u) / 60u);
        const unsigned s = static_cast<unsigned>(total_s % 60u);
        std::snprintf(buf, sizeof(buf), "Moving time: %02u:%02u:%02u", h, m, s);
        lv_label_set_text(st->val_moving_time, buf);
    }

    if (st->val_altitude && lv_obj_is_valid(st->val_altitude))
    {
        if (st->has_cur_alt)
        {
            std::snprintf(buf, sizeof(buf), "Altitude: %.0f m", st->cur_alt_m);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "Altitude: --");
        }
        lv_label_set_text(st->val_altitude, buf);
    }

    if (st->val_ascent && lv_obj_is_valid(st->val_ascent))
    {
        std::snprintf(buf, sizeof(buf), "Ascent: %.0f m", st->ascent_m);
        lv_label_set_text(st->val_ascent, buf);
    }

    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        const char* status;
        if (st->live_fix)
        {
            status = "GPS live - trip accumulating";
        }
        else if (st->any_fix)
        {
            status = "GPS lost - showing last trip data";
        }
        else
        {
            status = "Waiting for GPS fix";
        }
        lv_label_set_text(st->status_label, status);
    }
}

// Reset zeroes every accumulator and drops all references so the next valid fix
// starts a fresh trip. UI object pointers are left intact.
void trip_reset(TripState* st)
{
    st->total_distance_m = 0.0;
    st->moving_time_ms = 0u;
    st->max_speed_mps = 0.0;
    st->ascent_m = 0.0;
    st->cur_speed_mps = 0.0;
    st->cur_alt_m = 0.0;
    st->has_cur_alt = false;
    st->any_fix = false;
    st->has_prev = false;
    st->has_anc = false;
    st->has_alt_ref = false;
    trip_update_labels(st);
}

void trip_tick(lv_timer_t* timer)
{
    auto* st = static_cast<TripState*>(lv_timer_get_user_data(timer));
    if (!st)
    {
        return;
    }
    const gps::GpsState fix = platform::ui::gps::get_data();
    const uint32_t now = lv_tick_get();
    st->live_fix = fix.valid;
    if (fix.valid)
    {
        trip_accumulate(st, fix, now);
    }
    trip_update_labels(st);
}

void trip_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<TripState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }
    // Accumulators are file-static and intentionally NOT reset here: the trip
    // survives leaving and re-entering the app within a session.
    st->live_fix = false;

    st->root = lv_obj_create(parent);
    lv_obj_set_size(st->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(st->root, 0, 0);
    lv_obj_set_style_border_width(st->root, 0, 0);
    lv_obj_set_style_radius(st->root, 0, 0);
    lv_obj_clear_flag(st->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(st->root, LV_FLEX_FLOW_COLUMN);

    // Top bar: Back, title (grows to push Reset to the right edge), Reset.
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
    lv_label_set_text(title, "Trip");
    lv_obj_set_flex_grow(title, 1);

    lv_obj_t* reset_btn = lv_button_create(bar);
    lv_obj_t* reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, LV_SYMBOL_REFRESH " Reset");
    lv_obj_center(reset_lbl);
    lv_obj_add_event_cb(
        reset_btn, [](lv_event_t*) { trip_reset(&s_state); }, LV_EVENT_CLICKED, nullptr);

    // Scrollable readout body.
    lv_obj_t* body = lv_obj_create(st->root);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, 14, 0);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    st->status_label = lv_label_create(body);
    lv_obj_set_width(st->status_label, LV_PCT(100));
    lv_label_set_long_mode(st->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(st->status_label, &lv_font_montserrat_20, 0);

    lv_obj_t* note = lv_label_create(body);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(0x9A9A9A), 0);
    lv_label_set_text(note,
                      "Trip accumulates only while this screen is open (v1). "
                      "Reset clears it.");

    st->val_distance = add_stat_label(body);
    st->val_cur_speed = add_stat_label(body);
    st->val_max_speed = add_stat_label(body);
    st->val_avg_speed = add_stat_label(body);
    st->val_moving_time = add_stat_label(body);
    st->val_altitude = add_stat_label(body);
    st->val_ascent = add_stat_label(body);

    // Show the persisted values immediately, then refresh every ~1 s.
    trip_update_labels(st);
    st->timer = lv_timer_create(trip_tick, 1000, st);
}

void trip_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<TripState*>(user_data);
    if (!st)
    {
        return;
    }
    // Delete the timer first so it can never fire against a freed root.
    if (st->timer)
    {
        lv_timer_del(st->timer);
        st->timer = nullptr;
    }
    // Only the UI object pointers are cleared here. The trip accumulators persist
    // (file-static) so the trip survives re-entry; Reset / reboot clear them.
    if (st->root && lv_obj_is_valid(st->root))
    {
        lv_obj_del(st->root);
    }
    st->root = nullptr;
    st->status_label = nullptr;
    st->val_distance = nullptr;
    st->val_cur_speed = nullptr;
    st->val_max_speed = nullptr;
    st->val_avg_speed = nullptr;
    st->val_moving_time = nullptr;
    st->val_altitude = nullptr;
    st->val_ascent = nullptr;
}

} // namespace

ui::CallbackAppScreen g_trip_app("trip", "Trip", &tracker_topbar, trip_enter, trip_exit, &s_state);
