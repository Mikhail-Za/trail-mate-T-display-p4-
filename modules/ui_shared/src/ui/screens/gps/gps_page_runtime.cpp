#include "ui/screens/gps/gps_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
using Host = gps::ui::shell::Host;
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/gps/gps_constants.h"
#include "ui/screens/gps/gps_modal.h"
#include "ui/screens/gps/gps_page_components.h"
#include "ui/screens/gps/gps_page_input.h"
#include "ui/screens/gps/gps_page_layout.h"
#include "ui/screens/gps/gps_page_lifetime.h"
#include "ui/screens/gps/gps_page_map.h"
#include "ui/screens/gps/gps_page_styles.h"
#include "ui/screens/gps/gps_route_overlay.h"
#include "ui/screens/gps/gps_state.h"
#include "ui/screens/gps/gps_tracker_overlay.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_tiles.h"
#include "ui_gps_runtime/gps_page_runtime_pump.h"

#include <cmath>
#include <cstdio>

using GPSData = ::gps::GpsState;

static bool show_pan_axis_controls()
{
    return !::ui::page_profile::current().large_touch_hitbox;
}

#define GPS_DEBUG 0
#ifndef GPS_LOG
#if GPS_DEBUG
#define GPS_LOG(...) std::printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif
#endif

#define GPS_FLOW_LOG(...)         \
    do                            \
    {                             \
        std::printf(__VA_ARGS__); \
        std::fflush(stdout);      \
    } while (0)

namespace
{

const Host* s_host = nullptr;

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

} // namespace

static void gps_top_bar_back(void* /*user_data*/)
{
    GPS_LOG("[GPS][BACK] gps_top_bar_back: exiting=%d alive=%d root=%p\n",
            g_gps_state.exiting, g_gps_state.alive, gps_root);
    if (g_gps_state.exiting)
    {
        GPS_LOG("[GPS][BACK] gps_top_bar_back: already exiting, ignore\n");
        return;
    }
    g_gps_state.exiting = true;
    GPS_LOG("[GPS][BACK] gps_top_bar_back: scheduling async exit\n");
    request_exit();
}

GPSPageState g_gps_state;
static lv_obj_t* gps_root = nullptr;

namespace
{

struct SavedGpsMapView
{
    bool valid = false;
    int zoom_level = gps_ui::kDefaultZoom;
    double lat = gps_ui::kDefaultLat;
    double lng = gps_ui::kDefaultLng;
    bool has_fix = false;
    int pan_x = 0;
    int pan_y = 0;
    bool follow_position = true;
};

SavedGpsMapView s_saved_gps_map_view;

void assign_layout_widgets(const gps::ui::layout::Widgets& w)
{
    gps_root = w.root;
    g_gps_state.root = w.root;
    g_gps_state.header = w.header;
    g_gps_state.page = w.content;
    g_gps_state.map = w.map;
    g_gps_state.resolution_label = w.resolution_label;
    g_gps_state.altitude_label = w.altitude_label;
    g_gps_state.panel = w.panel;
    g_gps_state.member_panel = w.member_panel;
    g_gps_state.zoom = w.zoom_btn;
    g_gps_state.pos = w.pos_btn;
    g_gps_state.pan_h = w.pan_h_btn;
    g_gps_state.pan_v = w.pan_v_btn;
    g_gps_state.tracker_btn = w.tracker_btn;
    g_gps_state.layer_btn = w.layer_btn;
    g_gps_state.route_btn = w.route_btn;
    g_gps_state.top_bar = w.top_bar;
}

void bind_controls_and_group(lv_group_t* app_g)
{
    set_control_id(g_gps_state.top_bar.back_btn, ControlId::BackBtn);
    set_control_id(g_gps_state.map, ControlId::Map);
    set_control_id(g_gps_state.zoom, ControlId::ZoomBtn);
    set_control_id(g_gps_state.pos, ControlId::PosBtn);
    if (show_pan_axis_controls())
    {
        set_control_id(g_gps_state.pan_h, ControlId::PanHBtn);
        set_control_id(g_gps_state.pan_v, ControlId::PanVBtn);
    }
    set_control_id(g_gps_state.tracker_btn, ControlId::TrackerBtn);
    set_control_id(g_gps_state.layer_btn, ControlId::LayerBtn);
    set_control_id(g_gps_state.route_btn, ControlId::RouteBtn);

    auto bind_btn_events = [](lv_obj_t* obj, bool include_rotary)
    {
        if (!obj) return;
        lv_obj_add_event_cb(obj, on_ui_event, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(obj, on_ui_event, LV_EVENT_KEY, NULL);
        if (include_rotary)
        {
            lv_obj_add_event_cb(obj, on_ui_event, LV_EVENT_ROTARY, NULL);
        }
    };

    bind_btn_events(g_gps_state.zoom, true);
    bind_btn_events(g_gps_state.pos, true);
    if (show_pan_axis_controls())
    {
        bind_btn_events(g_gps_state.pan_h, true);
        bind_btn_events(g_gps_state.pan_v, true);
    }
    bind_btn_events(g_gps_state.tracker_btn, false);
    bind_btn_events(g_gps_state.layer_btn, false);
    bind_btn_events(g_gps_state.route_btn, false);

    if (g_gps_state.top_bar.back_btn)
    {
        // Ensure encoder KEY events can trigger back even if LVGL doesn't emit CLICKED.
        lv_obj_add_event_cb(g_gps_state.top_bar.back_btn, on_ui_event, LV_EVENT_KEY, NULL);
    }

    if (app_g)
    {
        if (g_gps_state.top_bar.back_btn)
        {
            lv_group_add_obj(app_g, g_gps_state.top_bar.back_btn);
        }
        lv_group_add_obj(app_g, g_gps_state.zoom);
        lv_group_add_obj(app_g, g_gps_state.pos);
        if (show_pan_axis_controls())
        {
            if (g_gps_state.pan_h) lv_group_add_obj(app_g, g_gps_state.pan_h);
            if (g_gps_state.pan_v) lv_group_add_obj(app_g, g_gps_state.pan_v);
        }
        lv_group_add_obj(app_g, g_gps_state.tracker_btn);
        lv_group_add_obj(app_g, g_gps_state.layer_btn);
        lv_group_add_obj(app_g, g_gps_state.route_btn);
    }

    lv_obj_add_event_cb(g_gps_state.map, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.map, on_ui_event, LV_EVENT_ROTARY, NULL);
}

void init_gps_state_defaults()
{
    g_gps_state.exiting = false;
    GPSData gps_data = platform::ui::gps::get_data();
    constexpr double kCoordEps = 1e-6;
    const bool has_cached_coord = std::isfinite(gps_data.lat) &&
                                  std::isfinite(gps_data.lng) &&
                                  (std::fabs(gps_data.lat) > kCoordEps || std::fabs(gps_data.lng) > kCoordEps);

    if (gps_data.valid)
    {
        g_gps_state.lat = gps_data.lat;
        g_gps_state.lng = gps_data.lng;
        g_gps_state.has_fix = true;
    }
    else
    {
        g_gps_state.zoom_level = gps_ui::kDefaultZoom;
        if (has_cached_coord)
        {
            // Keep last known location so offline maps remain useful before reacquiring fix.
            g_gps_state.lat = gps_data.lat;
            g_gps_state.lng = gps_data.lng;
        }
        else
        {
            g_gps_state.lat = gps_ui::kDefaultLat;
            g_gps_state.lng = gps_ui::kDefaultLng;
        }
        g_gps_state.has_fix = false;
    }

    g_gps_state.has_map_data = false;
    g_gps_state.has_visible_map_data = false;

    g_gps_state.pan_x = 0;
    g_gps_state.pan_y = 0;
    g_gps_state.follow_position = true;

    g_gps_state.edit_mode = GpsEditMode::None;

    g_gps_state.pending_refresh = false;
    g_gps_state.last_resolution_lat = 0.0;
    g_gps_state.last_resolution_zoom = -1;

    g_gps_state.anchor.valid = false;
    g_gps_state.initial_load_ms = 0;
    g_gps_state.initial_tiles_loaded = false;

    g_gps_state.tiles.clear();
    g_gps_state.tiles.reserve(TILE_RECORD_LIMIT);
}

} // namespace

bool isGPSLoadingTiles()
{
    return false;
}

static void title_update_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!gps::ui::lifetime::is_alive() || g_gps_state.exiting)
    {
        return;
    }

    reset_title_status_cache();
    update_title_and_status();
}

namespace
{

class EspGpsRuntimeRefreshModel final : public ::ui::screens::gps::IGpsStatusRefreshModel
{
  public:
    void refresh() override
    {
        // If there's no GPS fix and nothing explicitly requested a refresh,
        // avoid driving full UI/map updates on every runtime cadence tick.
        GPSData gps_data = platform::ui::gps::get_data();
        const bool has_fix_now = gps_data.valid || g_gps_state.has_fix;

        if (g_gps_state.pending_refresh)
        {
            GPS_FLOW_LOG("[GPS][MAP][flow] pending_refresh consume zoom=%d fix=%d pan=%d,%d\n",
                         g_gps_state.zoom_level,
                         g_gps_state.has_fix,
                         g_gps_state.pan_x,
                         g_gps_state.pan_y);
            g_gps_state.pending_refresh = false;
            update_map_tiles(false);
            log_map_tile_state("pending_refresh");
        }

        refresh_member_panel(false);
        refresh_team_markers_from_team_source();
        refresh_team_signal_markers_from_chatlog();

        if (g_gps_state.zoom_modal.is_open())
        {
            tick_gps_update(false);
            return;
        }

        if (modal_is_open(g_gps_state.tracker_modal))
        {
            tick_gps_update(false);
            return;
        }
        if (modal_is_open(g_gps_state.layer_modal))
        {
            tick_gps_update(false);
            return;
        }
        if (modal_is_open(g_gps_state.route_modal))
        {
            tick_gps_update(false);
            return;
        }

        if (!has_fix_now && !g_gps_state.pending_refresh)
        {
            return;
        }

        tick_gps_update(true);
    }
};

class EspGpsUiRefreshSink final : public ::ui::screens::gps::IGpsUiRefreshSink
{
  public:
    void onGpsRuntimeUpdated() override {}
};

EspGpsRuntimeRefreshModel& gps_runtime_refresh_model()
{
    static EspGpsRuntimeRefreshModel model;
    return model;
}

EspGpsUiRefreshSink& gps_runtime_refresh_sink()
{
    static EspGpsUiRefreshSink sink;
    return sink;
}

::ui::screens::gps::GpsPageRuntimePump& gps_runtime_pump()
{
    static ::ui::screens::gps::GpsPageRuntimePump pump(
        gps_runtime_refresh_model(),
        &gps_runtime_refresh_sink(),
        500);
    return pump;
}

} // namespace

static void gps_update_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!gps::ui::lifetime::is_alive() || g_gps_state.exiting)
    {
        return;
    }

    gps_runtime_pump().update(sys::millis_now());
}

static void gps_loader_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!gps::ui::lifetime::is_alive() || g_gps_state.exiting)
    {
        return;
    }
    tick_loader();
}

static void gps_initial_tiles_async(void* /*user_data*/)
{
    if (!gps::ui::lifetime::is_alive() || g_gps_state.exiting || !g_gps_state.map)
    {
        return;
    }
    // Ensure final sizes before first tile calculation to avoid visible jitter.
    lv_obj_update_layout(gps_root);
    GPS_FLOW_LOG("[GPS][MAP][flow] initial_async begin zoom=%d fix=%d src=%u contour=%d map=%dx%d lat=%.6f lng=%.6f\n",
                 g_gps_state.zoom_level,
                 g_gps_state.has_fix,
                 sanitize_map_source(app::configFacade().getConfig().map_source),
                 app::configFacade().getConfig().map_contour_enabled,
                 static_cast<int>(lv_obj_get_width(g_gps_state.map)),
                 static_cast<int>(lv_obj_get_height(g_gps_state.map)),
                 g_gps_state.lat,
                 g_gps_state.lng);
    update_map_tiles(false);
    log_map_tile_state("initial_async");
}

namespace gps::ui::runtime
{

bool is_available()
{
    return platform::ui::device::gps_supported();
}

void remember_gps_view_state()
{
    s_saved_gps_map_view.valid = true;
    s_saved_gps_map_view.zoom_level = g_gps_state.zoom_level;
    s_saved_gps_map_view.lat = g_gps_state.lat;
    s_saved_gps_map_view.lng = g_gps_state.lng;
    s_saved_gps_map_view.has_fix = g_gps_state.has_fix;
    s_saved_gps_map_view.pan_x = g_gps_state.pan_x;
    s_saved_gps_map_view.pan_y = g_gps_state.pan_y;
    s_saved_gps_map_view.follow_position = g_gps_state.follow_position;
}

bool restore_gps_view_state()
{
    if (!s_saved_gps_map_view.valid)
    {
        return false;
    }

    g_gps_state.zoom_level = s_saved_gps_map_view.zoom_level;
    g_gps_state.lat = s_saved_gps_map_view.lat;
    g_gps_state.lng = s_saved_gps_map_view.lng;
    g_gps_state.has_fix = s_saved_gps_map_view.has_fix;
    g_gps_state.pan_x = s_saved_gps_map_view.pan_x;
    g_gps_state.pan_y = s_saved_gps_map_view.pan_y;
    g_gps_state.follow_position = s_saved_gps_map_view.follow_position;
    return true;
}

void enter(const shell::Host* host, lv_obj_t* parent, shell::Projection projection)
{
    (void)projection;
    s_host = host;
    GPS_LOG("[GPS] Entering GPS page, SD ready: %d, GPS ready: %d\n",
            platform::ui::device::sd_ready(),
            platform::ui::device::gps_ready());
    GPS_FLOW_LOG("[GPS][MAP][flow] enter sd=%d gps=%d\n",
                 platform::ui::device::sd_ready(),
                 platform::ui::device::gps_ready());

    // Ensure any previous instance is cleaned up before we reset state.
    if (gps_root != nullptr)
    {
        exit(nullptr);
    }

    reset_control_tags();
    g_gps_state = GPSPageState{};

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    gps::ui::layout::Spec spec{};
    gps::ui::layout::Widgets w{};
    gps::ui::layout::create(parent, spec, w);
    gps::ui::styles::apply_all(w, spec);

    assign_layout_widgets(w);

    gps::ui::lifetime::mark_alive(gps_root, app_g);
    gps::ui::lifetime::bind_root_delete_hook();

    // Initialize TopBar on the state-owned instance (user_data must outlive layout locals).
    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::page_profile::current().top_bar_height;
    ::ui::widgets::top_bar_init(g_gps_state.top_bar, g_gps_state.header, cfg);
    ::ui::widgets::top_bar_set_title(g_gps_state.top_bar, ::ui::i18n::tr("Map"));
    ::ui::widgets::top_bar_set_back_callback(g_gps_state.top_bar, gps_top_bar_back, nullptr);

    // Ensure layout sizes are finalized before any tile calculations.
    lv_obj_update_layout(gps_root);

    ::init_tile_context(g_gps_state.tile_ctx,
                        nullptr,
                        &g_gps_state.anchor,
                        &g_gps_state.tiles,
                        &g_gps_state.tile_render_queue,
                        &g_gps_state.has_map_data,
                        &g_gps_state.has_visible_map_data);
    g_gps_state.tile_ctx.map_container = g_gps_state.map;

    if (!g_gps_state.tracker_draw_cb_bound)
    {
        lv_obj_add_event_cb(g_gps_state.map, gps_tracker_draw_event, LV_EVENT_DRAW_POST, NULL);
        g_gps_state.tracker_draw_cb_bound = true;
    }
    if (!g_gps_state.route_draw_cb_bound)
    {
        lv_obj_add_event_cb(g_gps_state.map, gps_route_draw_event, LV_EVENT_DRAW_POST, NULL);
        g_gps_state.route_draw_cb_bound = true;
    }

    lv_label_set_text(g_gps_state.resolution_label, "");
    if (g_gps_state.altitude_label)
    {
        ::ui::i18n::set_label_text(g_gps_state.altitude_label, "Alt: -- m");
    }

    bind_controls_and_group(app_g);
#ifdef USING_INPUT_DEV_TOUCHPAD
    bind_map_touch_input();
#endif

    lv_obj_move_foreground(g_gps_state.panel);
    if (g_gps_state.resolution_label != NULL)
    {
        lv_obj_move_foreground(g_gps_state.resolution_label);
    }
    if (g_gps_state.altitude_label != NULL)
    {
        lv_obj_move_foreground(g_gps_state.altitude_label);
    }

    init_gps_state_defaults();
    const bool restored_view = restore_gps_view_state();
    if (restored_view)
    {
        GPS_FLOW_LOG("[GPS][MAP][flow] restored view zoom=%d fix=%d follow=%d pan=%d,%d lat=%.6f lng=%.6f\n",
                     g_gps_state.zoom_level,
                     g_gps_state.has_fix,
                     g_gps_state.follow_position,
                     g_gps_state.pan_x,
                     g_gps_state.pan_y,
                     g_gps_state.lat,
                     g_gps_state.lng);
    }

    hide_pan_h_indicator();
    hide_pan_v_indicator();

    {
        const auto& cfg = app::configFacade().getConfig();
        bool show_route = cfg.route_enabled && (cfg.route_path[0] != '\0');
        if (g_gps_state.route_btn)
        {
            if (show_route)
            {
                lv_obj_clear_flag(g_gps_state.route_btn, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(g_gps_state.route_btn, LV_OBJ_FLAG_HIDDEN);
                if (app_g)
                {
                    lv_group_remove_obj(g_gps_state.route_btn);
                }
            }
        }
        if (show_route)
        {
            gps_route_sync_from_config(false);
        }
    }

    refresh_member_panel(true);

    if (app_g != NULL)
    {
        lv_group_set_editing(app_g, false);
    }

    reset_title_status_cache();
    update_zoom_btn();

    g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
    g_gps_state.last_resolution_lat = g_gps_state.lat;
    update_resolution_display();
    update_altitude_display(platform::ui::gps::get_data());

    if (g_gps_state.map == NULL || g_gps_state.tile_ctx.map_container != g_gps_state.map)
    {
        GPS_LOG("[GPS] WARNING: map=%p, tile_ctx.map_container=%p, skipping initial tile calculation\n",
                g_gps_state.map, g_gps_state.tile_ctx.map_container);
    }
    else
    {
        // Defer the first tile calculation to the next LVGL tick to stabilize layout.
        lv_async_call(gps_initial_tiles_async, nullptr);
    }

    if (app_g)
    {
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    // Split timers: fast tile loading + slower GPS refresh.
    gps_runtime_pump().setActive(true);
    g_gps_state.loader_timer = gps::ui::lifetime::add_timer(gps_loader_timer_cb, 200, NULL);
    g_gps_state.timer = gps::ui::lifetime::add_timer(gps_update_timer_cb, 500, NULL);
    g_gps_state.title_timer = gps::ui::lifetime::add_timer(title_update_timer_cb, 30000, NULL);

    update_title_and_status();

    GPS_LOG("[GPS] GPS page initialized: main_timer=%p, title_timer=%p\n", g_gps_state.timer, g_gps_state.title_timer);
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    GPS_LOG("[GPS] Exiting GPS page\n");
    GPS_LOG("[GPS][EXIT] begin: alive=%d exiting=%d root=%p\n",
            g_gps_state.alive, g_gps_state.exiting, gps_root);

    // Prevent re-entrant exit.
    remember_gps_view_state();
    g_gps_state.exiting = true;
    gps_runtime_pump().setActive(false);

    // Mirror Contacts cleanup order: stop inputs/timers, close modals, delete root, reset state.
#ifdef USING_INPUT_DEV_TOUCHPAD
    unbind_map_touch_input();
#endif

    gps::ui::lifetime::clear_timers();
    g_gps_state.timer = nullptr;
    g_gps_state.loader_timer = nullptr;
    g_gps_state.title_timer = nullptr;
    g_gps_state.toast_timer = nullptr;
#ifdef USING_INPUT_DEV_TOUCHPAD
    g_gps_state.touch_timer = nullptr;
#endif
    GPS_LOG("[GPS][EXIT] timers cleared\n");

    if (app_g)
    {
        auto remove_if = [](lv_obj_t* obj)
        {
            if (obj)
            {
                lv_group_remove_obj(obj);
            }
        };
        remove_if(g_gps_state.top_bar.back_btn);
        remove_if(g_gps_state.zoom);
        remove_if(g_gps_state.pos);
        remove_if(g_gps_state.pan_h);
        remove_if(g_gps_state.pan_v);
        remove_if(g_gps_state.tracker_btn);
        remove_if(g_gps_state.layer_btn);
        remove_if(g_gps_state.route_btn);
        remove_if(g_gps_state.pan_h_indicator);
        remove_if(g_gps_state.pan_v_indicator);
        for (auto* btn : g_gps_state.member_btns)
        {
            remove_if(btn);
        }
        GPS_LOG("[GPS][EXIT] removed objs from group\n");
    }

    if (g_gps_state.zoom_modal.is_open())
    {
        GPS_LOG("[GPS][EXIT] closing zoom modal\n");
        modal_close(g_gps_state.zoom_modal);
    }
    if (g_gps_state.layer_modal.is_open())
    {
        GPS_LOG("[GPS][EXIT] closing layer modal\n");
        modal_close(g_gps_state.layer_modal);
    }
    if (g_gps_state.route_modal.is_open())
    {
        GPS_LOG("[GPS][EXIT] closing route modal\n");
        modal_close(g_gps_state.route_modal);
    }
    GPS_LOG("[GPS][EXIT] cleaning tracker overlay\n");
    gps_tracker_cleanup();
    gps_route_cleanup();
    if (g_gps_state.zoom_modal.group)
    {
        GPS_LOG("[GPS][EXIT] deleting zoom modal group\n");
        lv_group_del(g_gps_state.zoom_modal.group);
        g_gps_state.zoom_modal.group = nullptr;
    }
    if (g_gps_state.tracker_modal.group)
    {
        GPS_LOG("[GPS][EXIT] deleting tracker modal group\n");
        lv_group_del(g_gps_state.tracker_modal.group);
        g_gps_state.tracker_modal.group = nullptr;
    }
    if (g_gps_state.layer_modal.group)
    {
        GPS_LOG("[GPS][EXIT] deleting layer modal group\n");
        lv_group_del(g_gps_state.layer_modal.group);
        g_gps_state.layer_modal.group = nullptr;
    }
    if (g_gps_state.route_modal.group)
    {
        GPS_LOG("[GPS][EXIT] deleting route modal group\n");
        lv_group_del(g_gps_state.route_modal.group);
        g_gps_state.route_modal.group = nullptr;
    }

    GPS_LOG("[GPS][EXIT] cleanup tiles\n");
    ::cleanup_tiles(g_gps_state.tile_ctx);

    // Delete the root last.
    if (gps_root != nullptr)
    {
        GPS_LOG("[GPS][EXIT] deleting root %p\n", gps_root);
        lv_obj_del(gps_root);
        gps_root = nullptr;
    }

    reset_title_status_cache();

    // Do not rebind encoder here; menu_show() already sets the default/menu group.

    g_gps_state = GPSPageState{};
    s_host = nullptr;
    GPS_LOG("[GPS][EXIT] end: state reset, root=%p\n", gps_root);
}

} // namespace gps::ui::runtime

#else

using Host = gps::ui::shell::Host;
using Projection = gps::ui::shell::Projection;

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "gps/domain/gps_diagnostics.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/presentation_sources/runtime_map_workspace_source.h"
#include "ui/presentation_sources/team_map_overlay_source.h"
#include "ui/screens/gps/gps_constants.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_viewport.h"
#include "ui/widgets/top_bar.h"
#include "ui_gps_runtime/gps_page_runtime_pump.h"
#include "ui_map_runtime/map_overlay_snapshot_source.h"
#include "ui_presentation/gps/gps_status_model.h"
#include "ui_presentation/map/map_overlay_snapshot.h"
#include "ui_presentation/map/map_workspace_model.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

namespace
{

constexpr lv_coord_t kCompactTopBarHeight = 26;
constexpr lv_coord_t kCompactTopBarBackWidth = 34;
constexpr lv_coord_t kCompactTopBarBackHeight = 18;
constexpr lv_coord_t kCompactTopBarRightWidth = 56;
constexpr int kCardputerZeroMapDefaultZoom = gps_ui::kDefaultZoom;
constexpr lv_coord_t kMapControlBarHeight = 24;
constexpr lv_coord_t kMapControlButtonHeight = 20;
constexpr lv_coord_t kMapControlButtonSmallWidth = 26;
constexpr lv_coord_t kMapControlButtonMediumWidth = 36;
constexpr lv_coord_t kMapControlButtonWideWidth = 44;
constexpr lv_coord_t kMapControlButtonContourWidth = 56;
constexpr lv_coord_t kMapSideRailWidth = 46;
constexpr std::uint32_t kLvglFunctionKeyF1 = 0x110001U;

const Host* s_host = nullptr;
lv_obj_t* s_root = nullptr;
lv_timer_t* s_timer = nullptr;
::ui::widgets::TopBar s_top_bar;
::ui::widgets::map::Runtime s_map_runtime;
int s_map_zoom = kCardputerZeroMapDefaultZoom;
int s_map_pan_x = 0;
int s_map_pan_y = 0;
bool s_map_view_initialized = false;
::ui::map::MapOverlaySnapshot s_overlay_snapshot;
Projection s_projection = Projection::Map;
lv_obj_t* s_gps_status_label = nullptr;
lv_obj_t* s_gps_coord_label = nullptr;
lv_obj_t* s_gps_sat_label = nullptr;
lv_obj_t* s_gps_alt_label = nullptr;
lv_obj_t* s_gps_motion_label = nullptr;
lv_obj_t* s_gps_time_label = nullptr;
lv_obj_t* s_gps_diag_label = nullptr;
lv_obj_t* s_map_control_bar = nullptr;
lv_obj_t* s_map_zoom_label = nullptr;
lv_obj_t* s_map_zoom_out_btn = nullptr;
lv_obj_t* s_map_zoom_in_btn = nullptr;
lv_obj_t* s_map_center_btn = nullptr;
lv_obj_t* s_map_layer_btn = nullptr;
lv_obj_t* s_map_contour_btn = nullptr;
lv_obj_t* s_map_help_btn = nullptr;
lv_obj_t* s_map_notice_panel = nullptr;
lv_obj_t* s_map_notice_label = nullptr;
lv_obj_t* s_map_context_rail = nullptr;
lv_obj_t* s_map_route_btn = nullptr;
lv_obj_t* s_map_team_btn = nullptr;
lv_obj_t* s_map_help_modal = nullptr;
bool s_map_help_open_pending = false;
bool s_map_refresh_pending = false;
uint8_t s_map_context_mask = 0xFF;
char s_map_notice_text[64]{};
uint32_t s_map_notice_until_ms = 0;

void apply_compact_top_bar_style(::ui::widgets::TopBar& bar);
void refresh_view();
void root_key_event_cb(lv_event_t* e);
void open_map_help_modal();
void add_map_controls_to_group(lv_group_t* group);

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

::ui::map::MapWorkspaceModel& map_workspace_model()
{
    static ::ui::presentation_sources::RuntimeMapWorkspaceSource source(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state(),
        &::team::ui::team_ui_snapshot_store());
    static ::ui::presentation_sources::RuntimeMapActionSink sink(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state());
    static ::ui::map::MapWorkspaceModel model(source, sink);
    return model;
}

class LinuxMapOverlayGpsSource final : public ::ui::map_overlay::IMapOverlayGpsSource
{
  public:
    bool currentFix(double& lat, double& lon, bool& valid) const override
    {
        ::ui::gps::GpsStatusSnapshot gps;
        if (!::ui::presentation_sources::runtime_gps_status_source().buildGpsStatusSnapshot(gps))
        {
            return false;
        }

        lat = gps.latitude;
        lon = gps.longitude;
        valid = gps.header.valid && gps.fix_valid;
        return true;
    }
};

::ui::map::IMapOverlayPresentationSource& map_overlay_source()
{
    static LinuxMapOverlayGpsSource gps;
    static ::ui::presentation_sources::TeamMapOverlaySource team(
        ::team::ui::team_ui_snapshot_store());
    static ::ui::map_overlay::MapOverlaySnapshotSource source(&gps, &team);
    return source;
}

uint8_t current_map_zoom()
{
    return static_cast<uint8_t>(
        std::clamp(s_map_zoom,
                   ::ui::widgets::map::kMinZoom,
                   ::ui::widgets::map::kMaxZoom));
}

void sync_workspace_layers_from_renderer()
{
    auto& state = ::ui::presentation_sources::runtime_map_workspace_state();
    const auto layers = ::ui::widgets::map::current_layer_state();
    state.layers.osm = layers.map_source == 0;
    state.layers.terrain = layers.map_source == 1;
    state.layers.satellite = layers.map_source == 2;
    state.layers.contour = layers.contour_enabled;
}

void sync_workspace_viewport_from_renderer()
{
    auto& model = map_workspace_model();
    auto viewport = model.viewport();
    viewport.zoom = current_map_zoom();
    if (!std::isfinite(viewport.center_lat) || !std::isfinite(viewport.center_lon) ||
        (viewport.center_lat == 0.0 && viewport.center_lon == 0.0))
    {
        viewport.center_lat = gps_ui::kDefaultLat;
        viewport.center_lon = gps_ui::kDefaultLng;
    }
    (void)model.setViewport(viewport);
    (void)model.setActiveTool(::ui::map::MapToolKind::Pan);
}

bool sync_workspace_center_from_screen()
{
    if (app::configFacade().getConfig().map_coord_system != 0)
    {
        return false;
    }

    ::ui::widgets::map::GeoPoint center{};
    if (!::ui::widgets::map::screen_center(s_map_runtime, center) || !center.valid)
    {
        return false;
    }

    auto& model = map_workspace_model();
    auto viewport = model.viewport();
    viewport.center_lat = center.lat;
    viewport.center_lon = center.lon;
    viewport.zoom = current_map_zoom();
    (void)model.setViewport(viewport);
    return true;
}

bool commit_pending_map_pan_from_screen()
{
    if (s_map_pan_x == 0 && s_map_pan_y == 0)
    {
        return false;
    }

    if (!sync_workspace_center_from_screen())
    {
        return false;
    }

    s_map_pan_x = 0;
    s_map_pan_y = 0;
    sync_workspace_viewport_from_renderer();
    return true;
}

::ui::widgets::map::Model build_map_model(
    const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    const auto& config = app::configFacade().getConfig();

    ::ui::widgets::map::Model model{};
    const bool has_viewport_center =
        std::isfinite(snapshot.viewport.center_lat) &&
        std::isfinite(snapshot.viewport.center_lon) &&
        (snapshot.viewport.center_lat != 0.0 || snapshot.viewport.center_lon != 0.0);
    model.focus_point.valid = has_viewport_center || snapshot.self.valid || snapshot.header.valid;
    model.focus_point.lat = has_viewport_center
                                ? snapshot.viewport.center_lat
                                : (snapshot.self.valid ? snapshot.self.lat
                                                       : gps_ui::kDefaultLat);
    model.focus_point.lon = has_viewport_center
                                ? snapshot.viewport.center_lon
                                : (snapshot.self.valid ? snapshot.self.lon
                                                       : gps_ui::kDefaultLng);
    model.zoom = snapshot.viewport.zoom == 0 ? s_map_zoom : snapshot.viewport.zoom;
    model.pan_x = s_map_pan_x;
    model.pan_y = s_map_pan_y;
    model.map_source = config.map_source;
    model.contour_enabled = snapshot.layers.contour;
    model.coord_system = config.map_coord_system;
    return model;
}

const char* diagnostic_label()
{
    const auto diagnostics = ::platform::ui::gps::diagnostics();
    return ::gps::gpsDiagnosticCodeName(diagnostics.code);
}

::ui::gps::GpsStatusModel& gps_status_model()
{
    static ::ui::gps::GpsStatusModel model(
        ::ui::presentation_sources::runtime_gps_status_source());
    return model;
}

void set_compact_label(lv_obj_t* label, const char* text)
{
    if (!label || !lv_obj_is_valid(label))
    {
        return;
    }
    lv_label_set_text(label, text ? text : "");
}

void set_map_notice(const char* text, uint32_t duration_ms)
{
    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    if (!text || text[0] == '\0')
    {
        return;
    }

    std::snprintf(s_map_notice_text, sizeof(s_map_notice_text), "%s", text);
    s_map_notice_until_ms = sys::millis_now() + duration_ms;
}

const char* compact_map_source_label(uint8_t map_source)
{
    switch (map_source)
    {
    case 1:
        return "Ter";
    case 2:
        return "Sat";
    case 0:
    default:
        return "OSM";
    }
}

void set_button_label(lv_obj_t* btn, const char* text)
{
    if (!btn || !lv_obj_is_valid(btn))
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    set_compact_label(label, text);
}

void clear_map_controls()
{
    s_map_control_bar = nullptr;
    s_map_zoom_label = nullptr;
    s_map_zoom_out_btn = nullptr;
    s_map_zoom_in_btn = nullptr;
    s_map_center_btn = nullptr;
    s_map_layer_btn = nullptr;
    s_map_contour_btn = nullptr;
    s_map_help_btn = nullptr;
    s_map_notice_panel = nullptr;
    s_map_notice_label = nullptr;
    s_map_context_rail = nullptr;
    s_map_route_btn = nullptr;
    s_map_team_btn = nullptr;
    s_map_help_modal = nullptr;
    s_map_help_open_pending = false;
    s_map_refresh_pending = false;
    s_map_context_mask = 0xFF;
    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
}

void set_hidden(lv_obj_t* obj, bool hidden)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }

    if (hidden)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

bool map_control_visible(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj) && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

bool route_context_available()
{
    const auto& config = app::configFacade().getConfig();
    return config.route_enabled && config.route_path[0] != '\0';
}

void sync_map_notice_overlay()
{
    if (!s_map_notice_panel || !lv_obj_is_valid(s_map_notice_panel) ||
        !s_map_notice_label || !lv_obj_is_valid(s_map_notice_label))
    {
        return;
    }

    const uint32_t now = sys::millis_now();
    if (s_map_notice_text[0] != '\0' && now < s_map_notice_until_ms)
    {
        set_compact_label(s_map_notice_label, s_map_notice_text);
        lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_map_notice_panel);
        return;
    }

    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
}

void sync_map_context_buttons(const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    const bool show_route = route_context_available();
    const bool show_team = snapshot.team.available && snapshot.team.visible_members > 0;
    const uint8_t next_mask = static_cast<uint8_t>((show_route ? 0x01 : 0x00) |
                                                   (show_team ? 0x02 : 0x00));

    set_hidden(s_map_route_btn, !show_route);
    set_hidden(s_map_team_btn, !show_team);
    set_hidden(s_map_context_rail, next_mask == 0);

    if (next_mask != s_map_context_mask)
    {
        s_map_context_mask = next_mask;
        if (app_g && (!s_map_help_modal || !lv_obj_is_valid(s_map_help_modal)))
        {
            lv_group_remove_all_objs(app_g);
            if (s_top_bar.back_btn)
            {
                lv_group_add_obj(app_g, s_top_bar.back_btn);
            }
            add_map_controls_to_group(app_g);
        }
    }
}

void sync_map_control_labels(const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    if (!s_map_control_bar || !lv_obj_is_valid(s_map_control_bar))
    {
        return;
    }

    const auto layers = ::ui::widgets::map::current_layer_state();
    set_button_label(s_map_layer_btn, compact_map_source_label(layers.map_source));
    set_button_label(s_map_contour_btn, layers.contour_enabled ? "Contour*" : "Contour");
    sync_map_context_buttons(snapshot);

    char zoom_buf[8]{};
    std::snprintf(zoom_buf, sizeof(zoom_buf), "Z%d", static_cast<int>(current_map_zoom()));
    set_compact_label(s_map_zoom_label, zoom_buf);

    uint8_t missing_source = 0;
    if (::ui::widgets::map::take_missing_tile_notice(s_map_runtime, &missing_source))
    {
        char notice[48]{};
        std::snprintf(notice,
                      sizeof(notice),
                      "%s tile loading",
                      compact_map_source_label(missing_source));
        set_map_notice(notice, 1400);
    }

    sync_map_notice_overlay();
}

lv_obj_t* create_status_row(lv_obj_t* parent, const char* title, lv_obj_t** out_value)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 18);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* key = lv_label_create(row);
    lv_label_set_text(key, title);
    lv_obj_set_width(key, 76);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(key, lv_color_hex(0x6D5B43), 0);
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);

    lv_obj_t* value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_width(value, 0);
    lv_obj_set_flex_grow(value, 1);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0x1D160F), 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);

    if (out_value)
    {
        *out_value = value;
    }
    return row;
}

void refresh_gps_status_view()
{
    if (!s_root || s_projection != Projection::GpsStatus)
    {
        return;
    }

    ui_update_top_bar_battery(s_top_bar);
    apply_compact_top_bar_style(s_top_bar);

    const auto snapshot = gps_status_model().snapshot();
    if (!snapshot.header.valid)
    {
        set_compact_label(s_gps_status_label, "Unavailable");
        set_compact_label(s_gps_coord_label, "--");
        set_compact_label(s_gps_sat_label, "--");
        set_compact_label(s_gps_alt_label, "--");
        set_compact_label(s_gps_motion_label, "--");
        set_compact_label(s_gps_time_label, "--");
        set_compact_label(s_gps_diag_label, "No status source");
        return;
    }

    set_compact_label(s_gps_status_label, snapshot.fix_label.c_str());
    set_compact_label(s_gps_coord_label, snapshot.coordinate_label.c_str());
    set_compact_label(s_gps_sat_label, snapshot.satellite_label.c_str());

    char line[48]{};
    if (snapshot.fix_valid && std::isfinite(snapshot.altitude_m))
    {
        std::snprintf(line, sizeof(line), "%.0f m", static_cast<double>(snapshot.altitude_m));
    }
    else
    {
        std::snprintf(line, sizeof(line), "--");
    }
    set_compact_label(s_gps_alt_label, line);

    if (snapshot.fix_valid)
    {
        std::snprintf(line,
                      sizeof(line),
                      "%.1f m/s %.0f deg",
                      static_cast<double>(snapshot.speed_mps),
                      static_cast<double>(snapshot.course_deg));
    }
    else
    {
        std::snprintf(line, sizeof(line), "--");
    }
    set_compact_label(s_gps_motion_label, line);
    set_compact_label(s_gps_time_label, snapshot.time_label.c_str());
    set_compact_label(s_gps_diag_label, diagnostic_label());
}

void apply_compact_top_bar_style(::ui::widgets::TopBar& bar)
{
    if (!bar.container || !lv_obj_is_valid(bar.container))
    {
        return;
    }

    lv_obj_set_height(bar.container, kCompactTopBarHeight);
    lv_obj_set_style_pad_left(bar.container, 8, 0);
    lv_obj_set_style_pad_right(bar.container, 8, 0);
    lv_obj_set_style_pad_top(bar.container, 2, 0);
    lv_obj_set_style_pad_bottom(bar.container, 2, 0);
    lv_obj_set_style_pad_column(bar.container, 4, 0);

    if (bar.back_btn && lv_obj_is_valid(bar.back_btn))
    {
        lv_obj_set_size(bar.back_btn, kCompactTopBarBackWidth, kCompactTopBarBackHeight);
        lv_obj_set_style_radius(bar.back_btn, kCompactTopBarBackHeight / 2, LV_PART_MAIN);
        lv_obj_set_style_text_font(bar.back_btn, &lv_font_montserrat_12, 0);
        lv_obj_t* back_label = lv_obj_get_child(bar.back_btn, 0);
        if (back_label && lv_obj_is_valid(back_label))
        {
            lv_obj_set_style_text_font(back_label, &lv_font_montserrat_12, 0);
        }
    }

    if (bar.title_label && lv_obj_is_valid(bar.title_label))
    {
        lv_obj_set_style_text_font(bar.title_label, &lv_font_montserrat_12, 0);
    }
    if (bar.right_label && lv_obj_is_valid(bar.right_label))
    {
        lv_obj_set_width(bar.right_label, kCompactTopBarRightWidth);
        lv_obj_set_style_text_font(bar.right_label, &lv_font_montserrat_12, 0);
    }
}

void refresh_view()
{
    if (!s_root)
    {
        return;
    }

    ui_update_top_bar_battery(s_top_bar);
    apply_compact_top_bar_style(s_top_bar);

    sync_workspace_layers_from_renderer();
    auto snapshot = map_workspace_model().snapshot();
    (void)map_overlay_source().buildMapOverlaySnapshot(s_overlay_snapshot);

    if (snapshot.header.valid)
    {
        ::ui::widgets::map::apply_model(s_map_runtime, build_map_model(snapshot));
        if (commit_pending_map_pan_from_screen())
        {
            snapshot = map_workspace_model().snapshot();
            ::ui::widgets::map::apply_model(s_map_runtime, build_map_model(snapshot));
        }
        ::ui::widgets::map::apply_overlay(s_map_runtime, s_overlay_snapshot);
    }
    else
    {
        ::ui::widgets::map::clear(s_map_runtime);
    }
    sync_map_control_labels(snapshot);
}

void refresh_view_async(void*)
{
    s_map_refresh_pending = false;
    refresh_view();
}

void request_refresh_view()
{
    if (s_map_refresh_pending)
    {
        return;
    }

    s_map_refresh_pending = true;
    lv_async_call(refresh_view_async, nullptr);
}

class LinuxGpsRuntimeRefreshModel final : public ::ui::screens::gps::IGpsStatusRefreshModel
{
  public:
    void refresh() override {}
};

class LinuxGpsUiRefreshSink final : public ::ui::screens::gps::IGpsUiRefreshSink
{
  public:
    void onGpsRuntimeUpdated() override
    {
        if (s_projection == Projection::GpsStatus)
        {
            refresh_gps_status_view();
            return;
        }
        refresh_view();
    }
};

LinuxGpsRuntimeRefreshModel& gps_runtime_refresh_model()
{
    static LinuxGpsRuntimeRefreshModel model;
    return model;
}

LinuxGpsUiRefreshSink& gps_runtime_refresh_sink()
{
    static LinuxGpsUiRefreshSink sink;
    return sink;
}

::ui::screens::gps::GpsPageRuntimePump& gps_runtime_pump()
{
    static ::ui::screens::gps::GpsPageRuntimePump pump(
        gps_runtime_refresh_model(),
        &gps_runtime_refresh_sink(),
        750);
    return pump;
}

void refresh_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    gps_runtime_pump().update(sys::millis_now());
}

void consume_key_event(lv_event_t* e)
{
    if (!e)
    {
        return;
    }

    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
}

void open_map_help_modal_async(void*)
{
    s_map_help_open_pending = false;
    open_map_help_modal();
}

void request_open_map_help_modal()
{
    if (s_map_help_open_pending)
    {
        return;
    }

    s_map_help_open_pending = true;
    lv_async_call(open_map_help_modal_async, nullptr);
}

enum class MapControlAction : uint8_t
{
    ZoomOut = 0,
    ZoomIn,
    Center,
    Layer,
    Contour,
    Help,
    Route,
    Team,
};

lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action);
void add_map_controls_to_group(lv_group_t* group);

void adjust_map_zoom(int delta)
{
    const int next_zoom = std::clamp(s_map_zoom + delta,
                                     ::ui::widgets::map::kMinZoom,
                                     ::ui::widgets::map::kMaxZoom);
    if (next_zoom == s_map_zoom)
    {
        return;
    }

    (void)sync_workspace_center_from_screen();
    s_map_pan_x = 0;
    s_map_pan_y = 0;
    s_map_zoom = next_zoom;
    sync_workspace_viewport_from_renderer();
    request_refresh_view();
}

void center_map_on_self()
{
    if (map_workspace_model().centerOnSelf().ok)
    {
        s_map_pan_x = 0;
        s_map_pan_y = 0;
        set_map_notice("Centered", 900);
    }
    else
    {
        set_map_notice("No GPS fix", 1200);
    }
    sync_workspace_viewport_from_renderer();
    request_refresh_view();
}

void cycle_map_layer()
{
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    // Prefer the next source that can actually render: the engine now refuses switches to
    // unavailable layers, so a blind +1 on a card carrying only one layer would toast and
    // go nowhere. If no other source is available the +1 fallback surfaces that toast.
    uint8_t next_source = static_cast<uint8_t>((layer_state.map_source + 1U) % 3U);
    for (uint8_t step = 1U; step < 3U; ++step)
    {
        const uint8_t candidate = static_cast<uint8_t>((layer_state.map_source + step) % 3U);
        if (map_source_directory_available(candidate))
        {
            next_source = candidate;
            break;
        }
    }
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::set_layer_map_source(next_source, &notice);
    if (notice.has_message)
    {
        set_map_notice(notice.message, notice.duration_ms > 0 ? notice.duration_ms : 1400);
    }
    else
    {
        char text[40]{};
        std::snprintf(text,
                      sizeof(text),
                      "Base %s",
                      compact_map_source_label(next_source));
        set_map_notice(text, 900);
    }
    sync_workspace_layers_from_renderer();
    request_refresh_view();
}

void toggle_map_contour()
{
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::toggle_layer_contour(&notice);
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    if (notice.has_message)
    {
        set_map_notice(notice.message, notice.duration_ms > 0 ? notice.duration_ms : 1400);
    }
    else
    {
        set_map_notice(layer_state.contour_enabled ? "Contour on" : "Contour off", 900);
    }
    sync_workspace_layers_from_renderer();
    request_refresh_view();
}

void close_map_help_modal()
{
    if (!s_map_help_modal || !lv_obj_is_valid(s_map_help_modal))
    {
        s_map_help_modal = nullptr;
        return;
    }

    lv_obj_del(s_map_help_modal);
    s_map_help_modal = nullptr;
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        if (s_top_bar.back_btn)
        {
            lv_group_add_obj(app_g, s_top_bar.back_btn);
        }
        add_map_controls_to_group(app_g);
        if (s_map_help_btn)
        {
            lv_group_focus_obj(s_map_help_btn);
        }
    }
}

void on_map_help_modal_key(lv_event_t* e)
{
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC || key == LV_KEY_ENTER ||
        key == kLvglFunctionKeyF1)
    {
        consume_key_event(e);
        close_map_help_modal();
        return;
    }

    consume_key_event(e);
}

void open_map_help_modal()
{
    if (s_map_help_modal && lv_obj_is_valid(s_map_help_modal))
    {
        close_map_help_modal();
        return;
    }
    if (!s_root || !lv_obj_is_valid(s_root))
    {
        return;
    }

    s_map_help_modal = lv_obj_create(s_root);
    lv_obj_set_size(s_map_help_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_map_help_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(s_map_help_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_map_help_modal, lv_color_hex(0x1C1812), 0);
    lv_obj_set_style_bg_opa(s_map_help_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_help_modal, 0, 0);
    lv_obj_set_style_pad_all(s_map_help_modal, 4, 0);
    lv_obj_clear_flag(s_map_help_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_map_help_modal, on_map_help_modal_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* panel = lv_obj_create(s_map_help_modal);
    lv_obj_set_size(panel, 304, 158);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x8A6E43), 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_left(panel, 7, 0);
    lv_obj_set_style_pad_right(panel, 7, 0);
    lv_obj_set_style_pad_top(panel, 5, 0);
    lv_obj_set_style_pad_bottom(panel, 5, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, on_map_help_modal_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "Map Help");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25170D), 0);

    auto add_keycap = [](lv_obj_t* parent, const char* text, lv_coord_t width)
    {
        lv_obj_t* keycap = lv_label_create(parent);
        lv_obj_set_size(keycap, width, 14);
        lv_obj_set_style_bg_color(keycap, lv_color_hex(0xF8E6C3), 0);
        lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(keycap, 1, 0);
        lv_obj_set_style_border_color(keycap, lv_color_hex(0x8A6E43), 0);
        lv_obj_set_style_radius(keycap, 3, 0);
        lv_obj_set_style_text_font(keycap, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(keycap, lv_color_hex(0x25170D), 0);
        lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
        lv_label_set_text(keycap, text ? text : "");
        return keycap;
    };

    auto add_help_row = [&](const char* primary,
                            const char* secondary,
                            const char* description)
    {
        lv_obj_t* row = lv_obj_create(panel);
        lv_obj_set_size(row, LV_PCT(100), 15);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 3, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* keys = lv_obj_create(row);
        lv_obj_set_size(keys, 76, 15);
        lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(keys,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(keys, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(keys, 0, 0);
        lv_obj_set_style_pad_all(keys, 0, 0);
        lv_obj_set_style_pad_column(keys, 2, 0);
        lv_obj_clear_flag(keys, LV_OBJ_FLAG_SCROLLABLE);

        if (secondary && secondary[0] != '\0')
        {
            const lv_coord_t secondary_width =
                std::strlen(secondary) > 4 ? 48 : (std::strlen(secondary) > 2 ? 34 : 22);
            add_keycap(keys, primary, std::strlen(primary) > 2 ? 34 : 22);
            add_keycap(keys, secondary, secondary_width);
        }
        else
        {
            add_keycap(keys, primary, 72);
        }

        lv_obj_t* text = lv_label_create(row);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_style_text_font(text, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(text, lv_color_hex(0x3E2B18), 0);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_label_set_text(text, description ? description : "");
    };

    add_help_row("WASD", nullptr, "Move map");
    add_help_row("-", "+", "Zoom map");
    add_help_row("P", "Pos", "Center current position");
    add_help_row("L", nullptr, "Change base layer");
    add_help_row("O", "Contour", "Toggle contour overlay");
    add_help_row("Route", nullptr, "Shown when route active");
    add_help_row("Team", nullptr, "Shown when team active");
    add_help_row("F1", "Back", "Close help");

    lv_obj_move_foreground(s_map_help_modal);
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, panel);
        lv_group_focus_obj(panel);
    }
}

void show_route_context_notice()
{
    if (!route_context_available())
    {
        set_map_notice("No route", 1200);
        request_refresh_view();
        return;
    }

    set_map_notice("Route active", 1200);
    request_refresh_view();
}

void show_team_overlay_notice()
{
    const auto snapshot = map_workspace_model().snapshot();
    if (!snapshot.team.available)
    {
        set_map_notice("No team", 1200);
        request_refresh_view();
        return;
    }

    char message[48]{};
    std::snprintf(message,
                  sizeof(message),
                  "Team %u/%u",
                  static_cast<unsigned>(snapshot.team.visible_members),
                  static_cast<unsigned>(snapshot.team.stale_members));
    set_map_notice(message, 1400);
    request_refresh_view();
}

void on_map_control_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const auto action = static_cast<MapControlAction>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    switch (action)
    {
    case MapControlAction::ZoomOut:
        adjust_map_zoom(-1);
        break;
    case MapControlAction::ZoomIn:
        adjust_map_zoom(1);
        break;
    case MapControlAction::Center:
        center_map_on_self();
        break;
    case MapControlAction::Layer:
        cycle_map_layer();
        break;
    case MapControlAction::Contour:
        toggle_map_contour();
        break;
    case MapControlAction::Help:
        request_open_map_help_modal();
        break;
    case MapControlAction::Route:
        show_route_context_notice();
        break;
    case MapControlAction::Team:
        show_team_overlay_notice();
        break;
    }
}

bool handle_map_key(uint32_t key, lv_event_t* e)
{
    switch (key)
    {
    case kLvglFunctionKeyF1:
        consume_key_event(e);
        request_open_map_help_modal();
        return true;
    case 'a':
    case 'A':
    case LV_KEY_LEFT:
        s_map_pan_x += gps_ui::kMapPanStep;
        break;
    case 'd':
    case 'D':
    case LV_KEY_RIGHT:
        s_map_pan_x -= gps_ui::kMapPanStep;
        break;
    case 'w':
    case 'W':
    case LV_KEY_UP:
        s_map_pan_y += gps_ui::kMapPanStep;
        break;
    case 's':
    case 'S':
    case LV_KEY_DOWN:
        s_map_pan_y -= gps_ui::kMapPanStep;
        break;
    case '+':
    case '=':
        adjust_map_zoom(1);
        consume_key_event(e);
        return true;
    case '-':
    case '_':
        adjust_map_zoom(-1);
        consume_key_event(e);
        return true;
    case 'c':
    case 'C':
    case 'p':
    case 'P':
        center_map_on_self();
        consume_key_event(e);
        return true;
    case 'l':
    case 'L':
        cycle_map_layer();
        consume_key_event(e);
        return true;
    case 'o':
    case 'O':
        toggle_map_contour();
        consume_key_event(e);
        return true;
    case 'r':
    case 'R':
        show_route_context_notice();
        consume_key_event(e);
        return true;
    default:
        return false;
    }

    sync_workspace_viewport_from_renderer();
    request_refresh_view();
    consume_key_event(e);
    return true;
}

lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, kMapControlButtonHeight);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xF8E6C3), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8A6E43), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x25170D), 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn,
                        on_map_control_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
    lv_obj_add_event_cb(btn, root_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_center(label);
    return btn;
}

void add_map_controls_to_group(lv_group_t* group)
{
    if (!group)
    {
        return;
    }
    if (s_map_zoom_out_btn) lv_group_add_obj(group, s_map_zoom_out_btn);
    if (s_map_zoom_in_btn) lv_group_add_obj(group, s_map_zoom_in_btn);
    if (s_map_center_btn) lv_group_add_obj(group, s_map_center_btn);
    if (s_map_layer_btn) lv_group_add_obj(group, s_map_layer_btn);
    if (s_map_contour_btn) lv_group_add_obj(group, s_map_contour_btn);
    if (s_map_help_btn) lv_group_add_obj(group, s_map_help_btn);
    if (map_control_visible(s_map_route_btn)) lv_group_add_obj(group, s_map_route_btn);
    if (map_control_visible(s_map_team_btn)) lv_group_add_obj(group, s_map_team_btn);
}

void create_map_control_bar(lv_obj_t* viewport)
{
    s_map_control_bar = lv_obj_create(viewport);
    lv_obj_set_size(s_map_control_bar, LV_PCT(100), kMapControlBarHeight);
    lv_obj_align(s_map_control_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s_map_control_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_map_control_bar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_map_control_bar, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_map_control_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_map_control_bar, 1, 0);
    lv_obj_set_style_border_color(s_map_control_bar, lv_color_hex(0xB3915D), 0);
    lv_obj_set_style_radius(s_map_control_bar, 0, 0);
    lv_obj_set_style_pad_left(s_map_control_bar, 3, 0);
    lv_obj_set_style_pad_right(s_map_control_bar, 3, 0);
    lv_obj_set_style_pad_top(s_map_control_bar, 2, 0);
    lv_obj_set_style_pad_bottom(s_map_control_bar, 2, 0);
    lv_obj_set_style_pad_column(s_map_control_bar, 3, 0);
    lv_obj_clear_flag(s_map_control_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_map_zoom_out_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        "-",
        MapControlAction::ZoomOut);
    s_map_zoom_label = lv_label_create(s_map_control_bar);
    lv_label_set_text(s_map_zoom_label, "Z7");
    lv_obj_set_width(s_map_zoom_label, 24);
    lv_obj_set_style_text_font(s_map_zoom_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_map_zoom_label, lv_color_hex(0x3E2B18), 0);
    lv_obj_set_style_text_align(s_map_zoom_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_map_zoom_label, LV_LABEL_LONG_CLIP);

    s_map_zoom_in_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        "+",
        MapControlAction::ZoomIn);
    s_map_center_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonMediumWidth,
        "Pos",
        MapControlAction::Center);
    s_map_layer_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonMediumWidth,
        "OSM",
        MapControlAction::Layer);
    s_map_contour_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonContourWidth,
        "Contour",
        MapControlAction::Contour);
    s_map_help_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        "F1",
        MapControlAction::Help);
    lv_obj_move_foreground(s_map_control_bar);
}

void create_map_notice_overlay(lv_obj_t* viewport)
{
    s_map_notice_panel = lv_obj_create(viewport);
    lv_obj_set_size(s_map_notice_panel, LV_SIZE_CONTENT, 18);
    lv_obj_align(s_map_notice_panel, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_map_notice_panel, lv_color_hex(0x25170D), 0);
    lv_obj_set_style_bg_opa(s_map_notice_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_notice_panel, 0, 0);
    lv_obj_set_style_radius(s_map_notice_panel, 4, 0);
    lv_obj_set_style_pad_left(s_map_notice_panel, 6, 0);
    lv_obj_set_style_pad_right(s_map_notice_panel, 6, 0);
    lv_obj_set_style_pad_top(s_map_notice_panel, 2, 0);
    lv_obj_set_style_pad_bottom(s_map_notice_panel, 2, 0);
    lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_map_notice_label = lv_label_create(s_map_notice_panel);
    lv_label_set_text(s_map_notice_label, "");
    lv_obj_set_width(s_map_notice_label, 154);
    lv_obj_set_style_text_font(s_map_notice_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_map_notice_label, lv_color_hex(0xFFF3DF), 0);
    lv_label_set_long_mode(s_map_notice_label, LV_LABEL_LONG_DOT);
    lv_obj_center(s_map_notice_label);
}

void create_map_context_rail(lv_obj_t* viewport)
{
    s_map_context_rail = lv_obj_create(viewport);
    lv_obj_set_size(s_map_context_rail, kMapSideRailWidth + 4, LV_SIZE_CONTENT);
    lv_obj_align(s_map_context_rail, LV_ALIGN_TOP_RIGHT, -3, 4);
    lv_obj_set_flex_flow(s_map_context_rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_map_context_rail,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_map_context_rail, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_map_context_rail, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_context_rail, 1, 0);
    lv_obj_set_style_border_color(s_map_context_rail, lv_color_hex(0xB3915D), 0);
    lv_obj_set_style_radius(s_map_context_rail, 4, 0);
    lv_obj_set_style_pad_all(s_map_context_rail, 3, 0);
    lv_obj_set_style_pad_row(s_map_context_rail, 3, 0);
    lv_obj_clear_flag(s_map_context_rail, LV_OBJ_FLAG_SCROLLABLE);

    s_map_route_btn = create_map_control_button(
        s_map_context_rail,
        kMapControlButtonWideWidth,
        "Route",
        MapControlAction::Route);
    s_map_team_btn = create_map_control_button(
        s_map_context_rail,
        kMapControlButtonWideWidth,
        "Team",
        MapControlAction::Team);
    set_hidden(s_map_route_btn, true);
    set_hidden(s_map_team_btn, true);
    set_hidden(s_map_context_rail, true);
    lv_obj_move_foreground(s_map_context_rail);
}

void on_back(void*)
{
    request_exit();
}

void root_key_event_cb(lv_event_t* e)
{
    const uint32_t key = lv_event_get_key(e);
    if (s_projection == Projection::Map && handle_map_key(key, e))
    {
        return;
    }
    if (key == LV_KEY_BACKSPACE)
    {
        consume_key_event(e);
        request_exit();
    }
}

void clear_gps_status_labels()
{
    s_gps_status_label = nullptr;
    s_gps_coord_label = nullptr;
    s_gps_sat_label = nullptr;
    s_gps_alt_label = nullptr;
    s_gps_motion_label = nullptr;
    s_gps_time_label = nullptr;
    s_gps_diag_label = nullptr;
}

void create_gps_status_content(lv_obj_t* content)
{
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 10, 0);
    lv_obj_set_style_pad_right(panel, 10, 0);
    lv_obj_set_style_pad_top(panel, 5, 0);
    lv_obj_set_style_pad_bottom(panel, 4, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    create_status_row(panel, "Fix", &s_gps_status_label);
    create_status_row(panel, "Coord", &s_gps_coord_label);
    create_status_row(panel, "Sat", &s_gps_sat_label);
    create_status_row(panel, "Alt", &s_gps_alt_label);
    create_status_row(panel, "Motion", &s_gps_motion_label);
    create_status_row(panel, "Time", &s_gps_time_label);
    create_status_row(panel, "Diag", &s_gps_diag_label);

    refresh_gps_status_view();
}

void create_map_content(lv_obj_t* content)
{
    lv_obj_t* viewport = lv_obj_create(content);
    lv_obj_set_size(viewport, LV_PCT(100), 0);
    lv_obj_set_flex_grow(viewport, 1);
    lv_obj_set_style_bg_color(viewport, lv_color_hex(0xEAD9B2), 0);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_set_style_radius(viewport, 0, 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);

    const auto map_widgets = ::ui::widgets::map::create(s_map_runtime, viewport, 180);
    lv_obj_update_layout(content);
    lv_obj_update_layout(viewport);
    ::ui::widgets::map::set_size(s_map_runtime,
                                 lv_obj_get_content_width(viewport),
                                 lv_obj_get_content_height(viewport));
    if (map_widgets.root)
    {
        lv_obj_align(map_widgets.root, LV_ALIGN_CENTER, 0, 0);
    }
    create_map_control_bar(viewport);
    create_map_notice_overlay(viewport);
    create_map_context_rail(viewport);

    refresh_view();
}

} // namespace

namespace gps::ui::runtime
{

bool is_available()
{
    return platform::ui::device::gps_supported();
}

void remember_gps_view_state()
{
}

bool restore_gps_view_state()
{
    return s_map_view_initialized;
}

void enter(const shell::Host* host, lv_obj_t* parent, shell::Projection projection)
{
    s_host = host;
    s_projection = projection;
    clear_gps_status_labels();
    clear_map_controls();
    if (s_projection == Projection::Map && !s_map_view_initialized)
    {
        s_map_zoom = kCardputerZeroMapDefaultZoom;
        s_map_pan_x = 0;
        s_map_pan_y = 0;
        s_map_view_initialized = true;
    }
    if (s_projection == Projection::Map)
    {
        sync_workspace_viewport_from_renderer();
    }

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    ::ui::widgets::TopBarConfig top_bar_config{};
    top_bar_config.height = kCompactTopBarHeight;
    ::ui::widgets::top_bar_init(s_top_bar, s_root, top_bar_config);
    ::ui::widgets::top_bar_set_title(
        s_top_bar,
        ::ui::i18n::tr(s_projection == Projection::GpsStatus ? "GPS" : "Map"));
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, on_back, nullptr);
    if (s_top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_top_bar.back_btn, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_top_bar);
    apply_compact_top_bar_style(s_top_bar);

    if (app_g && s_top_bar.back_btn)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_top_bar.back_btn);
        lv_group_focus_obj(s_top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    lv_obj_t* content = lv_obj_create(s_root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    if (s_projection == Projection::GpsStatus)
    {
        create_gps_status_content(content);
    }
    else
    {
        create_map_content(content);
        if (app_g)
        {
            add_map_controls_to_group(app_g);
        }
    }
    gps_runtime_pump().setActive(true);
    if (!s_timer)
    {
        s_timer = lv_timer_create(refresh_timer_cb, 750, nullptr);
    }
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    gps_runtime_pump().setActive(false);
    if (s_projection == Projection::Map)
    {
        ::ui::widgets::map::destroy(s_map_runtime);
    }
    clear_gps_status_labels();
    clear_map_controls();
    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }
    s_host = nullptr;
    s_projection = Projection::Map;
}

} // namespace gps::ui::runtime

#endif
