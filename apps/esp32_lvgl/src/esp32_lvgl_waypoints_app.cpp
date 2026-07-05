// Waypoints + Go-To: mark the current GPS position as a named waypoint, then walk
// back to it with a live bearing + distance.
//
// Persistence lives on the SD card as a plain TSV loaded via the LVGL 'A:' POSIX
// driver (same store as map tiles / translate / field guide):
//   A:/waypoints.tsv    name \t lat \t lng      (lat/lng decimal degrees, %.6f)
// so saved points survive a reboot with zero connectivity. Auto-names ("WP 1",
// "WP 2", ...) derive their next number from the existing rows, so no extra counter
// needs to be persisted.
//
// Flow: a list screen (Save button + one live row per waypoint) -> tap a row for the
// Go-To screen (big bearing + distance that update on a 1s timer) -> Back to the list.
//
// Self-contained inline app, same pattern as Translate/Field Guide/Node Radar:
// file-static state, enter()/exit() build and tear down everything (timer first, then
// the widget tree), back routes through ui_request_exit_to_menu().

#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/support/lvgl_fs_utils.h"
#include "ui/menu/dashboard/dashboard_style.h"
#include "platform/ui/gps_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

constexpr char kWaypointsPath[] = "A:/waypoints.tsv";
constexpr size_t kMaxWaypoints = 32;

struct Waypoint
{
    std::string name;
    double lat = 0.0;
    double lng = 0.0;
};

enum class View
{
    List,
    GoTo,
};

struct WaypointsAppState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* body = nullptr;
    lv_timer_t* timer = nullptr;

    std::vector<Waypoint> waypoints;
    // List-view: the per-row "distance + bearing" labels, parallel to `waypoints`, so
    // the tick can rewrite them in place instead of rebuilding (which would reset the
    // scroll position every second). Cleared whenever the body is cleared.
    std::vector<lv_obj_t*> row_info_labels;

    // List-view Save control.
    lv_obj_t* save_btn = nullptr;
    lv_obj_t* status_label = nullptr;

    // Go-To-view live labels (rewritten each tick).
    lv_obj_t* goto_bearing = nullptr;
    lv_obj_t* goto_distance = nullptr;
    lv_obj_t* goto_target = nullptr;
    lv_obj_t* goto_current = nullptr;

    View view = View::List;
    int goto_idx = -1;

    // Latched best-known position: updated only from a currently-valid fix, so a stale
    // last-known coordinate the receiver still reports while invalid is never trusted.
    bool have_pos = false;
    double cur_lat = 0.0;
    double cur_lng = 0.0;

    bool sd_missing = false; // SD card absent at load time (vs. file simply not present)
};

WaypointsAppState s_state;

// ---- persistence ----------------------------------------------------------

// Slurp the TSV via the shared ui::fs helper (loops to true EOF; a local
// break-on-short-read would truncate since the LVGL POSIX read is a single read()).
bool read_text_file(const char* path, std::string& out)
{
    return ::ui::fs::read_text_file(path, out);
}

void parse_waypoints(const std::string& text, std::vector<Waypoint>& out)
{
    out.clear();
    size_t pos = 0;
    while (pos < text.size() && out.size() < kMaxWaypoints)
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
        {
            eol = text.size();
        }
        size_t len = eol - pos;
        if (len > 0 && text[pos + len - 1] == '\r')
        {
            --len;
        }
        if (len > 0)
        {
            const std::string line = text.substr(pos, len);
            const size_t t1 = line.find('\t');
            const size_t t2 =
                (t1 == std::string::npos) ? std::string::npos : line.find('\t', t1 + 1);
            // Require all three fields; blank/malformed/short lines (including the
            // trailing '\n' padding a shrink writes) are silently skipped.
            if (t1 != std::string::npos && t2 != std::string::npos)
            {
                Waypoint w;
                w.name = line.substr(0, t1);
                const std::string lat_s = line.substr(t1 + 1, t2 - t1 - 1);
                const std::string lng_s = line.substr(t2 + 1);
                char* lat_end = nullptr;
                char* lng_end = nullptr;
                const double lat = std::strtod(lat_s.c_str(), &lat_end);
                const double lng = std::strtod(lng_s.c_str(), &lng_end);
                if (!w.name.empty() && lat_end != lat_s.c_str() && lng_end != lng_s.c_str() &&
                    std::isfinite(lat) && std::isfinite(lng) && lat >= -90.0 && lat <= 90.0 &&
                    lng >= -180.0 && lng <= 180.0)
                {
                    w.lat = lat;
                    w.lng = lng;
                    out.push_back(std::move(w));
                }
            }
        }
        pos = eol + 1;
    }
}

void load_waypoints(WaypointsAppState* st)
{
    st->waypoints.clear();
    st->sd_missing = false;
    std::string text;
    if (!read_text_file(kWaypointsPath, text))
    {
        // read failed: either the file does not exist yet (empty list) or there is no
        // SD card. Probe the drive root to tell them apart so the UI can warn on the
        // latter instead of pretending the list is merely empty.
        if (!::ui::fs::dir_exists("A:/"))
        {
            st->sd_missing = true;
        }
        return;
    }
    parse_waypoints(text, st->waypoints);
}

// Rewrite the whole TSV. The LVGL POSIX driver opens LV_FS_MODE_WR with
// O_WRONLY|O_CREAT and NO O_TRUNC, so a shorter payload (after a delete) would leave
// stale bytes past the new end. We pad the payload with '\n' up to the previous
// on-disk length; those blank lines are inert to the parser. The file therefore never
// shrinks on disk but stays bounded by kMaxWaypoints rows.
bool write_waypoints(WaypointsAppState* st)
{
    std::string content;
    content.reserve(st->waypoints.size() * 48);
    for (const auto& w : st->waypoints)
    {
        // Auto-generated names are ASCII with no tab/newline, so the TSV shape holds.
        char line[160];
        std::snprintf(line, sizeof(line), "%s\t%.6f\t%.6f\n", w.name.c_str(), w.lat, w.lng);
        content += line;
    }

    uint32_t old_size = 0;
    lv_fs_file_t rf;
    if (lv_fs_open(&rf, kWaypointsPath, LV_FS_MODE_RD) == LV_FS_RES_OK)
    {
        if (lv_fs_seek(&rf, 0, LV_FS_SEEK_END) == LV_FS_RES_OK)
        {
            lv_fs_tell(&rf, &old_size);
        }
        lv_fs_close(&rf);
    }
    if (content.size() < old_size)
    {
        content.append(old_size - content.size(), '\n');
    }

    lv_fs_file_t wf;
    if (lv_fs_open(&wf, kWaypointsPath, LV_FS_MODE_WR) != LV_FS_RES_OK)
    {
        return false;
    }
    lv_fs_res_t res = LV_FS_RES_OK;
    uint32_t bw = 0;
    if (!content.empty())
    {
        res = lv_fs_write(&wf, content.data(), static_cast<uint32_t>(content.size()), &bw);
    }
    lv_fs_close(&wf);
    return res == LV_FS_RES_OK && bw == content.size();
}

// Next auto-name: one past the highest existing "WP <n>", so a fresh point never
// collides with a survivor after deletes reorder the list.
std::string next_waypoint_name(const WaypointsAppState* st)
{
    int max_n = 0;
    for (const auto& w : st->waypoints)
    {
        if (w.name.size() > 3 && w.name.compare(0, 3, "WP ") == 0)
        {
            char* end = nullptr;
            const long n = std::strtol(w.name.c_str() + 3, &end, 10);
            if (end != w.name.c_str() + 3 && n > max_n)
            {
                max_n = static_cast<int>(n);
            }
        }
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "WP %d", max_n + 1);
    return std::string(buf);
}

// ---- position -------------------------------------------------------------

void refresh_position(WaypointsAppState* st)
{
    const gps::GpsState fix = platform::ui::gps::get_data();
    if (fix.valid)
    {
        st->have_pos = true;
        st->cur_lat = fix.lat;
        st->cur_lng = fix.lng;
    }
}

// "1.2 km   NE 043deg" from the current position, or the saved coordinates when no
// position is available yet.
void format_row_info(const WaypointsAppState* st, const Waypoint& w, char* out, size_t out_len)
{
    if (st->have_pos)
    {
        const double dist =
            ui::menu::dashboard::haversine_m(st->cur_lat, st->cur_lng, w.lat, w.lng);
        const float bear =
            ui::menu::dashboard::bearing_between(st->cur_lat, st->cur_lng, w.lat, w.lng);
        char distbuf[24];
        ui::menu::dashboard::format_distance(dist, distbuf, sizeof(distbuf));
        std::snprintf(out, out_len, "%s   %s %03ddeg", distbuf,
                      ui::menu::dashboard::compass_rose(bear),
                      static_cast<int>(std::lroundf(bear)) % 360);
    }
    else
    {
        std::snprintf(out, out_len, "%.5f, %.5f", w.lat, w.lng);
    }
}

// ---- UI -------------------------------------------------------------------

void show_list_screen(WaypointsAppState* st);
void show_goto_screen(WaypointsAppState* st);
void update_live(WaypointsAppState* st);

void clear_body(WaypointsAppState* st)
{
    if (st->body && lv_obj_is_valid(st->body))
    {
        lv_obj_clean(st->body);
        lv_obj_scroll_to_y(st->body, 0, LV_ANIM_OFF);
    }
    // Every body child was just deleted; drop references so the tick and teardown
    // never touch freed objects.
    st->row_info_labels.clear();
    st->save_btn = nullptr;
    st->status_label = nullptr;
    st->goto_bearing = nullptr;
    st->goto_distance = nullptr;
    st->goto_target = nullptr;
    st->goto_current = nullptr;
}

void set_title(WaypointsAppState* st, const char* text)
{
    if (st->title_label && lv_obj_is_valid(st->title_label))
    {
        lv_label_set_text(st->title_label, text);
    }
}

// Brief, auto-dismissing message floated over the body (save/delete feedback). FLOATING
// keeps it out of the column flex; it self-deletes and is torn down with the tree on
// exit (LVGL cancels the pending delete anim when the object is deleted first).
void show_toast(WaypointsAppState* st, const char* text)
{
    if (!st->root || !lv_obj_is_valid(st->root))
    {
        return;
    }
    lv_obj_t* toast = lv_label_create(st->root);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(toast, LV_PCT(90));
    lv_label_set_long_mode(toast, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast, 8, 0);
    lv_obj_set_style_pad_all(toast, 14, 0);
    lv_obj_set_style_text_color(toast, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(toast, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(toast, text);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_delete_delayed(toast, 2000);
}

void update_live(WaypointsAppState* st)
{
    if (st->view == View::List)
    {
        if (st->save_btn && lv_obj_is_valid(st->save_btn))
        {
            if (st->have_pos && !st->sd_missing)
            {
                lv_obj_clear_state(st->save_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(st->save_btn, LV_STATE_DISABLED);
            }
        }
        if (st->status_label && lv_obj_is_valid(st->status_label))
        {
            if (st->sd_missing)
            {
                lv_label_set_text(st->status_label,
                                  LV_SYMBOL_WARNING " SD card not found, waypoints unavailable");
            }
            else if (!st->have_pos)
            {
                lv_label_set_text(st->status_label, "No position yet (waiting for GPS fix)");
            }
            else
            {
                lv_label_set_text(st->status_label, "Position ready");
            }
        }
        const size_t n = std::min(st->row_info_labels.size(), st->waypoints.size());
        for (size_t i = 0; i < n; ++i)
        {
            lv_obj_t* lbl = st->row_info_labels[i];
            if (!lbl || !lv_obj_is_valid(lbl))
            {
                continue;
            }
            char info[64];
            format_row_info(st, st->waypoints[i], info, sizeof(info));
            lv_label_set_text(lbl, info);
        }
        return;
    }

    // Go-To view.
    if (st->goto_idx < 0 || st->goto_idx >= static_cast<int>(st->waypoints.size()))
    {
        return;
    }
    const Waypoint& w = st->waypoints[st->goto_idx];
    char buf[64];
    if (st->goto_bearing && lv_obj_is_valid(st->goto_bearing))
    {
        if (st->have_pos)
        {
            const float bear =
                ui::menu::dashboard::bearing_between(st->cur_lat, st->cur_lng, w.lat, w.lng);
            std::snprintf(buf, sizeof(buf), "%03ddeg  %s",
                          static_cast<int>(std::lroundf(bear)) % 360,
                          ui::menu::dashboard::compass_rose(bear));
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(st->goto_bearing, buf);
    }
    if (st->goto_distance && lv_obj_is_valid(st->goto_distance))
    {
        if (st->have_pos)
        {
            const double dist =
                ui::menu::dashboard::haversine_m(st->cur_lat, st->cur_lng, w.lat, w.lng);
            ui::menu::dashboard::format_distance(dist, buf, sizeof(buf));
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "No position yet");
        }
        lv_label_set_text(st->goto_distance, buf);
    }
    if (st->goto_target && lv_obj_is_valid(st->goto_target))
    {
        std::snprintf(buf, sizeof(buf), "Target:  %.5f, %.5f", w.lat, w.lng);
        lv_label_set_text(st->goto_target, buf);
    }
    if (st->goto_current && lv_obj_is_valid(st->goto_current))
    {
        if (st->have_pos)
        {
            std::snprintf(buf, sizeof(buf), "You:  %.5f, %.5f", st->cur_lat, st->cur_lng);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "You:  --");
        }
        lv_label_set_text(st->goto_current, buf);
    }
}

void on_save_clicked(lv_event_t*)
{
    WaypointsAppState* st = &s_state;
    if (st->sd_missing)
    {
        show_toast(st, "No SD card; cannot save");
        return;
    }
    // Prefer a currently-valid fix; fall back to the latched last-known position.
    const gps::GpsState fix = platform::ui::gps::get_data();
    double lat = 0.0;
    double lng = 0.0;
    if (fix.valid)
    {
        lat = fix.lat;
        lng = fix.lng;
        st->have_pos = true;
        st->cur_lat = lat;
        st->cur_lng = lng;
    }
    else if (st->have_pos)
    {
        lat = st->cur_lat;
        lng = st->cur_lng;
    }
    else
    {
        show_toast(st, "No position yet");
        return;
    }
    if (st->waypoints.size() >= kMaxWaypoints)
    {
        show_toast(st, "Waypoint limit reached (32)");
        return;
    }

    Waypoint w;
    w.name = next_waypoint_name(st);
    w.lat = lat;
    w.lng = lng;
    st->waypoints.push_back(std::move(w));
    if (!write_waypoints(st))
    {
        st->waypoints.pop_back(); // keep memory consistent with disk
        show_toast(st, "Save failed (SD write error)");
        return;
    }
    show_toast(st, "Saved");
    show_list_screen(st); // rebuild with the new row
}

void on_delete_clicked(lv_event_t* e)
{
    WaypointsAppState* st = &s_state;
    const auto idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (idx >= st->waypoints.size())
    {
        return;
    }
    st->waypoints.erase(st->waypoints.begin() + static_cast<std::ptrdiff_t>(idx));
    if (!write_waypoints(st))
    {
        show_toast(st, "Delete failed (SD write error)");
        // Disk and memory diverged; reload the on-disk truth before rebuilding.
        load_waypoints(st);
    }
    show_list_screen(st);
}

void on_row_clicked(lv_event_t* e)
{
    WaypointsAppState* st = &s_state;
    const auto idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (idx >= st->waypoints.size())
    {
        return;
    }
    st->goto_idx = static_cast<int>(idx);
    st->view = View::GoTo;
    show_goto_screen(st);
}

void show_list_screen(WaypointsAppState* st)
{
    st->view = View::List;
    clear_body(st);
    set_title(st, "Waypoints");

    // Save current location.
    lv_obj_t* save_btn = lv_button_create(st->body);
    lv_obj_set_width(save_btn, LV_PCT(100));
    lv_obj_set_height(save_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(save_btn, 14, 0);
    lv_obj_add_event_cb(save_btn, on_save_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* save_lbl = lv_label_create(save_btn);
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_20, 0);
    lv_label_set_text(save_lbl, LV_SYMBOL_PLUS " Save current location");
    lv_obj_center(save_lbl);
    st->save_btn = save_btn;

    // Status line under the button (GPS / SD state).
    lv_obj_t* status = lv_label_create(st->body);
    lv_obj_set_width(status, LV_PCT(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0x9A9A9A), 0);
    lv_label_set_text(status, "");
    st->status_label = status;

    if (st->waypoints.empty())
    {
        lv_obj_t* msg = lv_label_create(st->body);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_label_set_text(msg, st->sd_missing
                                   ? "Waypoints are stored on the SD card, which was not found."
                                   : "No waypoints yet.\n\n"
                                     "Stand where you want to remember, then tap Save.");
    }

    for (size_t i = 0; i < st->waypoints.size(); ++i)
    {
        // Row: [ tap-target column (name + live info) | delete ]. Two separate buttons
        // so a tap on one never triggers the other.
        lv_obj_t* row = lv_obj_create(st->body);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        lv_obj_t* main_btn = lv_button_create(row);
        lv_obj_set_flex_grow(main_btn, 1);
        lv_obj_set_height(main_btn, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(main_btn, 12, 0);
        lv_obj_set_flex_flow(main_btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(main_btn, on_row_clicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        lv_obj_t* name_lbl = lv_label_create(main_btn);
        lv_obj_set_width(name_lbl, LV_PCT(100));
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_20, 0);
        lv_label_set_text(name_lbl, st->waypoints[i].name.c_str());

        lv_obj_t* info_lbl = lv_label_create(main_btn);
        lv_obj_set_width(info_lbl, LV_PCT(100));
        lv_label_set_long_mode(info_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x9A9A9A), 0);
        char info[64];
        format_row_info(st, st->waypoints[i], info, sizeof(info));
        lv_label_set_text(info_lbl, info);
        st->row_info_labels.push_back(info_lbl);

        lv_obj_t* del_btn = lv_button_create(row);
        lv_obj_set_style_pad_all(del_btn, 12, 0);
        lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x8A2A2A), 0);
        lv_obj_add_event_cb(del_btn, on_delete_clicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_t* del_lbl = lv_label_create(del_btn);
        lv_obj_set_style_text_font(del_lbl, &lv_font_montserrat_20, 0);
        lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
        lv_obj_center(del_lbl);
    }

    update_live(st); // sets Save enable + status + row text immediately
}

void show_goto_screen(WaypointsAppState* st)
{
    clear_body(st);
    // Defensive: never index out of range if the selection is stale.
    if (st->goto_idx < 0 || st->goto_idx >= static_cast<int>(st->waypoints.size()))
    {
        st->view = View::List;
        show_list_screen(st);
        return;
    }
    const Waypoint& w = st->waypoints[st->goto_idx];
    set_title(st, w.name.c_str());

    lv_obj_t* name = lv_label_create(st->body);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(name, 12, 0);
    lv_label_set_text(name, w.name.c_str());

    lv_obj_t* bearing = lv_label_create(st->body);
    lv_obj_set_width(bearing, LV_PCT(100));
    lv_obj_set_style_text_font(bearing, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(bearing, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(bearing, lv_color_hex(0x2F6FD6), 0);
    lv_obj_set_style_pad_top(bearing, 16, 0);
    lv_label_set_text(bearing, "--");
    st->goto_bearing = bearing;

    lv_obj_t* distance = lv_label_create(st->body);
    lv_obj_set_width(distance, LV_PCT(100));
    lv_obj_set_style_text_font(distance, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(distance, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(distance, "--");
    st->goto_distance = distance;

    lv_obj_t* target = lv_label_create(st->body);
    lv_obj_set_width(target, LV_PCT(100));
    lv_label_set_long_mode(target, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(target, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(target, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(target, 20, 0);
    lv_label_set_text(target, "");
    st->goto_target = target;

    lv_obj_t* current = lv_label_create(st->body);
    lv_obj_set_width(current, LV_PCT(100));
    lv_label_set_long_mode(current, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(current, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(current, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(current, lv_color_hex(0x9A9A9A), 0);
    lv_label_set_text(current, "");
    st->goto_current = current;

    update_live(st); // populate immediately instead of waiting for the first tick
}

void go_back(WaypointsAppState* st)
{
    if (st->view == View::GoTo)
    {
        st->view = View::List;
        st->goto_idx = -1;
        show_list_screen(st);
        return;
    }
    ::ui_request_exit_to_menu();
}

void waypoints_tick(lv_timer_t* timer)
{
    auto* st = static_cast<WaypointsAppState*>(lv_timer_get_user_data(timer));
    if (!st || !st->root || !lv_obj_is_valid(st->root))
    {
        return;
    }
    refresh_position(st);
    update_live(st);
}

void waypoints_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<WaypointsAppState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }
    st->view = View::List;
    st->goto_idx = -1;
    st->have_pos = false;
    st->cur_lat = 0.0;
    st->cur_lng = 0.0;

    st->root = lv_obj_create(parent);
    lv_obj_set_size(st->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(st->root, 0, 0);
    lv_obj_set_style_border_width(st->root, 0, 0);
    lv_obj_set_style_radius(st->root, 0, 0);
    lv_obj_clear_flag(st->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(st->root, LV_FLEX_FLOW_COLUMN);

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
        back_btn, [](lv_event_t*) { go_back(&s_state); }, LV_EVENT_CLICKED, nullptr);

    st->title_label = lv_label_create(bar);
    lv_obj_set_style_text_font(st->title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_flex_grow(st->title_label, 1);
    lv_label_set_long_mode(st->title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(st->title_label, "Waypoints");

    st->body = lv_obj_create(st->root);
    lv_obj_set_width(st->body, LV_PCT(100));
    lv_obj_set_flex_grow(st->body, 1);
    lv_obj_set_style_border_width(st->body, 0, 0);
    lv_obj_set_style_radius(st->body, 0, 0);
    lv_obj_set_style_pad_all(st->body, 12, 0);
    lv_obj_set_style_pad_row(st->body, 10, 0);
    lv_obj_set_flex_flow(st->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(st->body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(st->body, LV_SCROLLBAR_MODE_AUTO);

    refresh_position(st); // seed latch so the first render can show distances
    load_waypoints(st);
    show_list_screen(st);

    st->timer = lv_timer_create(waypoints_tick, 1000, st);
}

void waypoints_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<WaypointsAppState*>(user_data);
    if (!st)
    {
        return;
    }
    // Timer first: no tick may run against a half-torn-down tree.
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
    st->title_label = nullptr;
    st->body = nullptr;
    st->row_info_labels.clear();
    st->save_btn = nullptr;
    st->status_label = nullptr;
    st->goto_bearing = nullptr;
    st->goto_distance = nullptr;
    st->goto_target = nullptr;
    st->goto_current = nullptr;
    st->waypoints.clear();
    st->view = View::List;
    st->goto_idx = -1;
    st->have_pos = false;
    st->cur_lat = 0.0;
    st->cur_lng = 0.0;
    st->sd_missing = false;
}

extern "C"
{
    extern const lv_image_dsc_t room_24px;
}

} // namespace

ui::CallbackAppScreen g_waypoints_app("waypoints",
                                      "Waypoints",
                                      &room_24px,
                                      waypoints_enter,
                                      waypoints_exit,
                                      &s_state);
