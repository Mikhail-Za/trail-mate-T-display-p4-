// Sun & Moon: an offline astronomical planner. From the device clock (UTC epoch),
// the resolved local timezone offset, and the last-known GPS position it computes
// today's sunrise, solar noon, sunset, day length, and the current moon phase
// (name + illumination %). Pure closed-form math -- no network, no ephemeris file.
//
// Self-contained inline app, same pattern as Field Guide / Flashlight / Snake:
// file-static state, enter()/exit() build and tear down everything, a slow refresh
// timer keeps the numbers current as the clock advances / position changes, and
// Back routes through ui_request_exit_to_menu().

#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"

#include "platform/ui/gps_runtime.h"
#include "platform/ui/time_runtime.h"
#include "sys/clock.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{

extern "C"
{
    extern const lv_image_dsc_t Setting;
}

constexpr double kDeg2Rad = M_PI / 180.0;
constexpr double kRad2Deg = 180.0 / M_PI;
constexpr double kSynodicMonth = 29.530588853;     // mean synodic month, days
constexpr double kRefNewMoonJd = 2451550.1;        // JD of new moon 2000-01-06 18:14 UTC
constexpr double kUnixEpochJd = 2440587.5;         // JD of 1970-01-01 00:00 UTC
constexpr uint32_t kClockValidEpoch = 1577836800u;  // 2020-01-01 UTC; below = RTC unsynced
constexpr uint32_t kStalePositionMaxSec = 24u * 3600u; // cached fix older than this = refuse

struct SunMoonState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* status_label = nullptr; // "clock not set" / "need gps" messages
    lv_obj_t* date_label = nullptr;
    lv_obj_t* sunrise_label = nullptr;
    lv_obj_t* noon_label = nullptr;
    lv_obj_t* sunset_label = nullptr;
    lv_obj_t* daylen_label = nullptr;
    lv_obj_t* moon_label = nullptr;
    lv_obj_t* illum_label = nullptr;
    lv_obj_t* loc_label = nullptr;
    lv_timer_t* timer = nullptr;
    // Last-known position latch: solar times only need approximate coordinates, so a
    // stale fix is fine. We remember the last usable position so the page keeps
    // working after the GPS drops its live lock.
    double last_lat = 0.0;
    double last_lng = 0.0;
    bool have_last = false;
    // Epoch (UTC seconds) at which last_lat/last_lng were captured from a genuinely
    // live fix. Used to disclose the cached position's age and to expire a fix too
    // old to trust. 0 = never stamped from a valid fix.
    uint32_t last_fix_epoch = 0;
};

SunMoonState s_state;

// ---------------------------------------------------------------------------
// Astronomy (all closed-form, double precision)
// ---------------------------------------------------------------------------

// Gregorian calendar date -> Julian Day at 0h UT (result ends in .5).
double julian_day_0h(int year, int month, int day)
{
    if (month <= 2)
    {
        year -= 1;
        month += 12;
    }
    const int a = year / 100;
    const int b = 2 - a + a / 4;
    return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day + b -
           1524.5;
}

struct SolarResult
{
    bool ok = false;
    int state = 0; // 0 = normal, 1 = sun up all day, 2 = sun down all day
    double sunrise_local_min = 0.0;
    double noon_local_min = 0.0;
    double sunset_local_min = 0.0;
    double daylen_min = 0.0;
};

// NOAA-style single-pass sunrise/sunset for a local calendar date. lat/lng are in
// degrees (east-positive longitude), tz_min is the local UTC offset in minutes.
// Returns times as minutes-from-local-midnight (may be <0 or >=1440; caller wraps).
SolarResult compute_solar(int year, int month, int day, double lat, double lng, int tz_min)
{
    SolarResult r;

    const double jd0 = julian_day_0h(year, month, day);
    // Evaluate the slowly-varying solar terms near this date's LOCAL solar noon
    // (expressed in UT) so declination / equation-of-time are for the right instant.
    const double jd_eval = jd0 + 0.5 - lng / 360.0;
    const double t = (jd_eval - 2451545.0) / 36525.0; // Julian centuries since J2000

    // Geometric mean longitude / anomaly of the sun (degrees) and orbit eccentricity.
    double l0 = 280.46646 + t * (36000.76983 + 0.0003032 * t);
    l0 = std::fmod(l0, 360.0);
    if (l0 < 0.0)
    {
        l0 += 360.0;
    }
    const double m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    const double m_rad = m * kDeg2Rad;
    const double e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);

    // Equation of the center -> true then apparent ecliptic longitude.
    const double c = std::sin(m_rad) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                     std::sin(2.0 * m_rad) * (0.019993 - 0.000101 * t) +
                     std::sin(3.0 * m_rad) * 0.000289;
    const double true_long = l0 + c;
    const double omega = 125.04 - 1934.136 * t;
    const double lambda = true_long - 0.00569 - 0.00478 * std::sin(omega * kDeg2Rad);

    // Obliquity of the ecliptic (with nutation correction) -> solar declination.
    const double eps0 =
        23.0 + (26.0 + ((21.448 - t * (46.815 + t * (0.00059 - t * 0.001813)))) / 60.0) / 60.0;
    const double eps = eps0 + 0.00256 * std::cos(omega * kDeg2Rad);
    const double eps_rad = eps * kDeg2Rad;
    const double decl_rad = std::asin(std::sin(eps_rad) * std::sin(lambda * kDeg2Rad));

    // Equation of time (minutes) via the NOAA compact series.
    const double y = std::tan(eps_rad / 2.0) * std::tan(eps_rad / 2.0);
    const double l0_rad = l0 * kDeg2Rad;
    const double eot = 4.0 * kRad2Deg *
                       (y * std::sin(2.0 * l0_rad) - 2.0 * e * std::sin(m_rad) +
                        4.0 * e * y * std::sin(m_rad) * std::cos(2.0 * l0_rad) -
                        0.5 * y * y * std::sin(4.0 * l0_rad) -
                        1.25 * e * e * std::sin(2.0 * m_rad));

    // Solar noon in UTC minutes (east-positive longitude), then shift to local.
    const double noon_utc = 720.0 - 4.0 * lng - eot;
    r.noon_local_min = noon_utc + tz_min;

    // Hour angle for the standard sunrise altitude of -0.833 deg (zenith 90.833),
    // accounting for atmospheric refraction + the solar radius.
    const double lat_rad = lat * kDeg2Rad;
    const double cos_z = std::cos(90.833 * kDeg2Rad);
    const double denom = std::cos(lat_rad) * std::cos(decl_rad);
    if (std::isnan(decl_rad) || !(std::fabs(denom) > 1e-9))
    {
        // Degenerate (at a geographic pole, or a NaN slipped through): report no event.
        r.ok = true;
        r.state = 2;
        return r;
    }
    const double cos_h = (cos_z - std::sin(lat_rad) * std::sin(decl_rad)) / denom;
    if (cos_h > 1.0)
    {
        r.ok = true;
        r.state = 2; // sun never reaches the horizon -> down all day
        return r;
    }
    if (cos_h < -1.0)
    {
        r.ok = true;
        r.state = 1; // sun never sets -> up all day
        return r;
    }
    const double h_deg = std::acos(cos_h) * kRad2Deg; // sunrise hour angle, degrees
    const double half_day_min = 4.0 * h_deg;          // 4 minutes of time per degree
    r.sunrise_local_min = r.noon_local_min - half_day_min;
    r.sunset_local_min = r.noon_local_min + half_day_min;
    r.daylen_min = 2.0 * half_day_min;
    r.ok = true;
    r.state = 0;
    return r;
}

struct MoonResult
{
    double age_days = 0.0;
    double illum_pct = 0.0;
    const char* name = "";
};

// Closed-form mean-phase moon age from a known new moon, mapped to a named phase and
// an approximate illuminated fraction. Good to ~a day of phase; no ephemeris needed.
MoonResult compute_moon(uint32_t utc)
{
    MoonResult r;
    const double jd = static_cast<double>(utc) / 86400.0 + kUnixEpochJd;
    double age = std::fmod(jd - kRefNewMoonJd, kSynodicMonth);
    if (age < 0.0)
    {
        age += kSynodicMonth;
    }
    r.age_days = age;

    const double frac = age / kSynodicMonth; // 0..1 through the cycle
    r.illum_pct = (1.0 - std::cos(2.0 * M_PI * frac)) * 0.5 * 100.0;

    static const char* const kNames[8] = {"New Moon",       "Waxing Crescent", "First Quarter",
                                           "Waxing Gibbous", "Full Moon",       "Waning Gibbous",
                                           "Last Quarter",   "Waning Crescent"};
    // Center each named phase on its 1/8 segment; index 0 (New) straddles the wrap.
    int idx = static_cast<int>(std::floor((frac + 1.0 / 16.0) * 8.0)) % 8;
    if (idx < 0)
    {
        idx += 8;
    }
    r.name = kNames[idx];
    return r;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

// Wrap a signed minutes-from-midnight value into a valid [0, 1440) minute of day.
int wrap_minutes(double minutes)
{
    long v = std::lround(minutes);
    v %= 1440;
    if (v < 0)
    {
        v += 1440;
    }
    return static_cast<int>(v);
}

// Coarse human-readable age ("45m", "3h", "2d") for the last-known position tag.
void format_age_short(uint32_t seconds, char* out, size_t n)
{
    if (seconds < 3600u)
    {
        std::snprintf(out, n, "%um", static_cast<unsigned>(seconds / 60u));
    }
    else if (seconds < 86400u)
    {
        std::snprintf(out, n, "%uh", static_cast<unsigned>(seconds / 3600u));
    }
    else
    {
        std::snprintf(out, n, "%ud", static_cast<unsigned>(seconds / 86400u));
    }
}

void set_line(lv_obj_t* label, const char* text)
{
    if (label && lv_obj_is_valid(label))
    {
        lv_label_set_text(label, text);
    }
}

void clear_data_lines(SunMoonState* st)
{
    set_line(st->date_label, "");
    set_line(st->sunrise_label, "");
    set_line(st->noon_label, "");
    set_line(st->sunset_label, "");
    set_line(st->daylen_label, "");
    set_line(st->moon_label, "");
    set_line(st->illum_label, "");
    set_line(st->loc_label, "");
}

void recompute(SunMoonState* st)
{
    if (!st)
    {
        return;
    }

    const uint32_t utc = sys::epoch_seconds_now();
    // Guard against an unsynced RTC (epoch still at/near the boot default).
    if (utc < kClockValidEpoch)
    {
        set_line(st->status_label, "Clock not set yet (waiting for GPS/RTC time)");
        clear_data_lines(st);
        return;
    }

    // Position: use the live fix when it carries coordinates, else the last latched
    // position. Solar times only need approximate coordinates, so a stale fix is ok.
    const gps::GpsState fix = platform::ui::gps::get_data();
    const bool fix_has_pos = fix.valid || (fix.lat != 0.0 || fix.lng != 0.0);
    if (fix_has_pos)
    {
        st->last_lat = fix.lat;
        st->last_lng = fix.lng;
        st->have_last = true;
        // Stamp the capture time only for a genuinely-current fix. Re-reading an
        // already-stale cached position (coords present but fix.valid == false)
        // must NOT refresh this, so the age below reflects true position freshness.
        if (fix.valid)
        {
            st->last_fix_epoch = utc;
        }
    }
    if (!st->have_last)
    {
        set_line(st->status_label, "Need a GPS position first (open Map/GPS to get a fix)");
        clear_data_lines(st);
        return;
    }
    const double lat = st->last_lat;
    const double lng = st->last_lng;
    const bool stale = !fix.valid;

    // Stale-position disclosure + expiry. With no live fix we're drawing from the
    // last latched position, so work out how old that position actually is: this
    // lets us both surface the age to the user and refuse a fix too old to trust.
    // (A user manually choosing a timezone inconsistent with their real location is
    // configuration the app faithfully honors -- out of scope; this targets only the
    // silently-stale GPS position.)
    uint32_t pos_age_sec = 0;
    bool pos_age_known = false;
    if (stale && st->last_fix_epoch >= kClockValidEpoch && utc >= st->last_fix_epoch)
    {
        pos_age_sec = utc - st->last_fix_epoch;
        pos_age_known = true;
    }
    if (stale && pos_age_known && pos_age_sec > kStalePositionMaxSec)
    {
        // A day-old position is too far off to trust for sunrise/sunset; refuse it
        // rather than showing plausible-looking but wrong solar times.
        set_line(st->status_label, "Last position too old -- open Map/GPS for a fresh fix");
        clear_data_lines(st);
        return;
    }
    set_line(st->status_label, "");

    const int tz_min = platform::ui::time::timezone_offset_min();

    // Local broken-down time: apply the (already DST-resolved) offset to UTC, then
    // read the fields with gmtime_r (the offset is baked in, so NOT localtime_r).
    const time_t local_t = platform::ui::time::apply_timezone_offset(static_cast<time_t>(utc));
    struct tm tmv;
    gmtime_r(&local_t, &tmv);
    const int year = tmv.tm_year + 1900;
    const int month = tmv.tm_mon + 1;
    const int day = tmv.tm_mday;

    char buf[128];
    static const char* const kWeekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* wd = (tmv.tm_wday >= 0 && tmv.tm_wday < 7) ? kWeekdays[tmv.tm_wday] : "";
    std::snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d", wd, year, month, day);
    set_line(st->date_label, buf);

    // Format the tz offset once as "UTC-05:00" for the sunrise/sunset lines.
    char tzbuf[16];
    {
        const int at = tz_min < 0 ? -tz_min : tz_min;
        std::snprintf(tzbuf, sizeof(tzbuf), "UTC%c%02d:%02d", tz_min < 0 ? '-' : '+', at / 60,
                      at % 60);
    }

    const SolarResult sol = compute_solar(year, month, day, lat, lng, tz_min);
    if (!sol.ok)
    {
        set_line(st->sunrise_label, "Sunrise:  --:--");
        set_line(st->noon_label, "Solar noon:  --:--");
        set_line(st->sunset_label, "Sunset:  --:--");
        set_line(st->daylen_label, "Day length:  --");
    }
    else if (sol.state == 1)
    {
        set_line(st->sunrise_label, "Sun up all day");
        set_line(st->noon_label, "");
        set_line(st->sunset_label, "");
        set_line(st->daylen_label, "Day length:  24h 00m");
    }
    else if (sol.state == 2)
    {
        set_line(st->sunrise_label, "Sun down all day");
        set_line(st->noon_label, "");
        set_line(st->sunset_label, "");
        set_line(st->daylen_label, "Day length:  0h 00m");
    }
    else
    {
        const int sr = wrap_minutes(sol.sunrise_local_min);
        const int sn = wrap_minutes(sol.noon_local_min);
        const int ss = wrap_minutes(sol.sunset_local_min);
        std::snprintf(buf, sizeof(buf), "Sunrise:  %02d:%02d  (%s)", sr / 60, sr % 60, tzbuf);
        set_line(st->sunrise_label, buf);
        std::snprintf(buf, sizeof(buf), "Solar noon:  %02d:%02d", sn / 60, sn % 60);
        set_line(st->noon_label, buf);
        std::snprintf(buf, sizeof(buf), "Sunset:  %02d:%02d  (%s)", ss / 60, ss % 60, tzbuf);
        set_line(st->sunset_label, buf);
        const int dl = static_cast<int>(std::lround(sol.daylen_min));
        std::snprintf(buf, sizeof(buf), "Day length:  %dh %02dm", dl / 60, dl % 60);
        set_line(st->daylen_label, buf);
    }

    const MoonResult moon = compute_moon(utc);
    std::snprintf(buf, sizeof(buf), "Moon:  %s", moon.name);
    set_line(st->moon_label, buf);
    std::snprintf(buf, sizeof(buf), "Illumination:  %.0f%%  (age %.1f d)", moon.illum_pct,
                  moon.age_days);
    set_line(st->illum_label, buf);

    char locsuffix[48];
    locsuffix[0] = '\0';
    if (stale)
    {
        if (pos_age_known)
        {
            char agebuf[16];
            format_age_short(pos_age_sec, agebuf, sizeof(agebuf));
            std::snprintf(locsuffix, sizeof(locsuffix), "  (last known %s ago)", agebuf);
        }
        else
        {
            // No valid-fix timestamp yet (position latched from a non-valid fix).
            std::snprintf(locsuffix, sizeof(locsuffix), "  (last known)");
        }
    }
    std::snprintf(buf, sizeof(buf), "Location:  %.4f, %.4f%s", lat, lng, locsuffix);
    set_line(st->loc_label, buf);
}

void sun_moon_tick(lv_timer_t* timer)
{
    recompute(static_cast<SunMoonState*>(lv_timer_get_user_data(timer)));
}

lv_obj_t* make_data_label(lv_obj_t* parent, const lv_font_t* font)
{
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_label_set_text(lbl, "");
    return lbl;
}

void sun_moon_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<SunMoonState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }

    st->root = lv_obj_create(parent);
    lv_obj_set_size(st->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(st->root, 0, 0);
    lv_obj_set_style_border_width(st->root, 0, 0);
    lv_obj_set_style_radius(st->root, 0, 0);
    lv_obj_clear_flag(st->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(st->root, LV_FLEX_FLOW_COLUMN);

    // Header bar: Back button + title.
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
    lv_label_set_text(title, "Sun & Moon");

    // Body: a vertical, scrollable column of labels.
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

    st->status_label = make_data_label(body, &lv_font_montserrat_20);
    st->date_label = make_data_label(body, &lv_font_montserrat_24);
    st->sunrise_label = make_data_label(body, &lv_font_montserrat_20);
    st->noon_label = make_data_label(body, &lv_font_montserrat_20);
    st->sunset_label = make_data_label(body, &lv_font_montserrat_20);
    st->daylen_label = make_data_label(body, &lv_font_montserrat_20);
    st->moon_label = make_data_label(body, &lv_font_montserrat_24);
    st->illum_label = make_data_label(body, &lv_font_montserrat_20);
    st->loc_label = make_data_label(body, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(st->loc_label, lv_color_hex(0x9A9A9A), 0);

    // Render immediately, then refresh slowly so it tracks the clock (near midnight)
    // and position changes without churning the UI.
    recompute(st);
    st->timer = lv_timer_create(sun_moon_tick, 30000, st);
}

void sun_moon_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<SunMoonState*>(user_data);
    if (!st)
    {
        return;
    }
    // Delete the refresh timer first so it can never fire against a freed root.
    if (st->timer)
    {
        lv_timer_del(st->timer);
        st->timer = nullptr;
    }
    if (!st->root || !lv_obj_is_valid(st->root))
    {
        st->root = nullptr;
        st->status_label = nullptr;
        st->date_label = nullptr;
        st->sunrise_label = nullptr;
        st->noon_label = nullptr;
        st->sunset_label = nullptr;
        st->daylen_label = nullptr;
        st->moon_label = nullptr;
        st->illum_label = nullptr;
        st->loc_label = nullptr;
        return;
    }
    lv_obj_del(st->root);
    st->root = nullptr;
    st->status_label = nullptr;
    st->date_label = nullptr;
    st->sunrise_label = nullptr;
    st->noon_label = nullptr;
    st->sunset_label = nullptr;
    st->daylen_label = nullptr;
    st->moon_label = nullptr;
    st->illum_label = nullptr;
    st->loc_label = nullptr;
}

} // namespace

ui::CallbackAppScreen g_sun_moon_app("sun_moon",
                                     "Sun & Moon",
                                     &Setting,
                                     sun_moon_enter,
                                     sun_moon_exit,
                                     &s_state);
