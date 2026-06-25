#include "ui/screens/sstv/sstv_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)

#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/sstv_runtime.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"
#include "ui/widgets/system_notification.h"
#include "ui/widgets/top_bar.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace
{
const sstv_page::ui::shell::Host* s_host = nullptr;

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

constexpr lv_coord_t kClassicScreenW = 480;
constexpr lv_coord_t kClassicMainHeight = 192;
constexpr int kMeterSegments = 12;
constexpr float kGainStepDb = 6.0f; // per-tap mic-gain change for the -/+ buttons

lv_coord_t top_bar_height()
{
    const auto& profile = ::ui::page_profile::current();
    return profile.top_bar_height > 0 ? profile.top_bar_height
                                      : static_cast<lv_coord_t>(::ui::widgets::kTopBarHeight);
}

// All of the SSTV screen geometry that used to be `constexpr` landscape
// constants now lives in this runtime struct so the page can switch to a tall
// portrait arrangement on the P4 (540x1168). The defaults reproduce the
// original hand-tuned 480x192 landscape layout byte-for-byte, so every wide /
// landscape screen is completely unchanged.
struct SstvLayout
{
    bool portrait = false;

    lv_coord_t screen_w = kClassicScreenW;   // root width
    lv_coord_t main_h = kClassicMainHeight;  // main content area height (below top bar)
    lv_coord_t main_x = 0;                    // main content area x within root
    lv_coord_t padding = 8;

    // Image (waterfall) panel.
    lv_coord_t img_w = 288;
    lv_coord_t img_h = 192;
    lv_coord_t img_x = 8;   // == padding default
    lv_coord_t img_y = 0;

    // Info (status/readout) panel. info_y is the panel's top within the main
    // area: 0 in landscape (same row as the image), below the image in portrait.
    lv_coord_t info_x = 8 + 288 + 8; // img_x + img_w + padding (landscape)
    lv_coord_t info_y = 0;
    lv_coord_t info_w = 168;
    lv_coord_t info_h = 192;
    lv_coord_t info_text_w = 140;

    // Progress bar (positioned in the main area, in main coordinates).
    lv_coord_t progress_h = 8;
    lv_coord_t progress_x = 8 + 288 + 8; // info_x in landscape
    lv_coord_t progress_y = kClassicMainHeight - 14;
    lv_coord_t progress_w = 168;

    // Audio meter (child of the info panel).
    lv_coord_t meter_x = 136;
    lv_coord_t meter_y = 34;
    lv_coord_t meter_w = 32;
    lv_coord_t meter_h = 120;
    lv_coord_t meter_seg_h = 8;
    lv_coord_t meter_seg_gap = 2;

    // Info-panel child positions (within the info panel).
    lv_coord_t state_sub_y = 6;
    lv_coord_t mode_y = 56;
    lv_coord_t ready_y = 128;
    lv_coord_t btn_rx_x = 0;
    lv_coord_t btn_rx_y = 150;
    lv_coord_t btn_rx_w = 72;
    lv_coord_t btn_rx_h = 22;

    // Manual mic-gain controls ("Gain: NN dB" + big -/+ touch buttons). Only the
    // tall portrait layout has room for them; classic landscape leaves show_gain
    // false so its hand-tuned 480x192 geometry is unchanged. Coordinates are in
    // info-panel space, same as the RX button.
    bool show_gain = false;
    lv_coord_t gain_label_y = 0;
    lv_coord_t gain_btn_y = 0;
    lv_coord_t gain_btn_w = 72;
    lv_coord_t gain_btn_h = 56;

    // Fonts (default to the classic inline sizes).
    const lv_font_t* placeholder_font = &lv_font_montserrat_12;
    const lv_font_t* state_sub_font = &lv_font_montserrat_14;
    const lv_font_t* mode_font = &lv_font_montserrat_14;
    const lv_font_t* ready_font = &lv_font_montserrat_14;
    const lv_font_t* btn_rx_font = &lv_font_montserrat_16;
    const lv_font_t* gain_label_font = &lv_font_montserrat_20;
    const lv_font_t* gain_btn_font = &lv_font_montserrat_24;
};

SstvLayout make_classic_layout()
{
    return SstvLayout{}; // defaults == original landscape constants
}

// Tall portrait (P4 540x1168): the image fills the full width near the top,
// the status block + audio meter sit below it, and the big RX button anchors
// the bottom of the screen. Everything sits BELOW the top bar (which already
// reserves the camera cutout via the page profile).
SstvLayout make_portrait_tall_layout(int parent_w, int parent_h, int top_bar_h)
{
    SstvLayout l{};
    l.portrait = true;
    l.screen_w = parent_w > 0 ? static_cast<lv_coord_t>(parent_w) : 540;
    const lv_coord_t screen_h = parent_h > 0 ? static_cast<lv_coord_t>(parent_h) : 1168;

    const lv_coord_t side_margin = 12;
    const lv_coord_t content_top = top_bar_h + ::ui::page_profile::current().top_content_gap;
    const lv_coord_t bottom_margin = 16;
    const lv_coord_t content_w = l.screen_w - (side_margin * 2);

    // The main area spans from just below the top bar to the bottom of the
    // screen, full width.
    l.main_x = side_margin;
    l.main_h = screen_h - content_top - bottom_margin;
    l.padding = 0;

    // --- Image panel: full width, square-ish, anchored at the top of main.
    l.img_x = 0;
    l.img_y = 0;
    l.img_w = content_w;
    // Keep the decoded image's 288x192 (3:2) aspect ratio while filling width.
    l.img_h = static_cast<lv_coord_t>(content_w * 192 / 288);

    // --- Big RX button anchored at the bottom of the screen, full width.
    l.btn_rx_h = 84;
    l.btn_rx_w = content_w;
    l.btn_rx_x = 0;
    // btn_rx_y is in INFO-panel coordinates; the info panel starts right under
    // the image, so place the button near the bottom of that panel.

    // --- Info panel: fills the gap between the image and the RX button.
    const lv_coord_t info_gap = 16;
    l.info_x = 0;
    l.info_y = l.img_h + info_gap;                 // info panel sits below the image
    l.info_w = content_w;
    l.info_h = l.main_h - l.info_y - info_gap;     // remaining height for status block + button
    l.progress_x = 0; // progress lives inside info panel at its own x
    l.progress_w = content_w - 16;
    l.progress_h = 14;

    // Lay the status labels down the left, meter on the right, button at bottom.
    l.info_text_w = content_w - 96;
    l.state_sub_y = 16;
    l.mode_y = 96;
    l.ready_y = 156;
    l.btn_rx_y = l.info_h - l.btn_rx_h - 8;

    // Manual mic-gain row sits above the RX button: big -/+ touch buttons with a
    // "Gain: NN dB" label above them; the progress bar sits above the label. The
    // portrait info panel is tall enough to host this between the status labels
    // and the RX button (stacked top->bottom: status, progress, gain label,
    // gain buttons, RX).
    l.show_gain = true;
    l.gain_btn_h = 56;
    l.gain_btn_w = static_cast<lv_coord_t>((content_w - 24) / 2); // two side-by-side
    l.gain_btn_y = l.btn_rx_y - 18 - l.gain_btn_h;
    l.gain_label_y = l.gain_btn_y - 32;
    l.progress_y = l.gain_label_y - 24;

    // Audio meter: a wider/taller vertical bar on the right edge of the info panel.
    l.meter_w = 40;
    l.meter_h = std::min<lv_coord_t>(200, l.btn_rx_y - 24);
    l.meter_x = content_w - l.meter_w - 8;
    l.meter_y = 16;
    l.meter_seg_h = 12;
    l.meter_seg_gap = 3;

    // Larger fonts to fill the tall screen.
    l.placeholder_font = &lv_font_montserrat_16;
    l.state_sub_font = &lv_font_montserrat_16;
    l.mode_font = &lv_font_montserrat_20;
    l.ready_font = &lv_font_montserrat_24;
    l.btn_rx_font = &lv_font_montserrat_24;
    return l;
}

SstvLayout resolve_layout(int parent_w, int parent_h, int top_bar_h)
{
    if (parent_h >= 700 && parent_h > parent_w)
    {
        return make_portrait_tall_layout(parent_w, parent_h, top_bar_h);
    }
    return make_classic_layout();
}

SstvLayout s_layout{};
constexpr uint32_t kColorWarmBg = 0xF6E6C6;
constexpr uint32_t kColorAccent = 0xEBA341;
constexpr uint32_t kColorPanelBg = 0xFAF0D8;
constexpr uint32_t kColorLine = 0xE7C98F;
constexpr uint32_t kColorText = 0x6B4A1E;
constexpr uint32_t kColorTextDim = 0x8A6A3A;
constexpr uint32_t kColorOk = 0x3E7D3E;
constexpr uint32_t kColorWarn = 0xB94A2C;
constexpr uint32_t kColorGray = 0x6E6E6E;
constexpr uint32_t kColorMeterMid = 0xC18B2C;
// Bright, unmistakable red for the audio meter when the mic input is clipping
// (overdriving). Distinct from kColorWarn so a clip reads as an alarm, not just
// a hot top segment.
constexpr uint32_t kColorClip = 0xE51E1E;

struct SstvUi
{
    lv_obj_t* root = nullptr;
    ui::widgets::TopBar top_bar = {};
    lv_obj_t* img_box = nullptr;
    lv_obj_t* img = nullptr;
    lv_obj_t* img_placeholder = nullptr;
    lv_obj_t* info_area = nullptr;
    lv_obj_t* label_state_sub = nullptr;
    lv_obj_t* label_mode = nullptr;
    lv_obj_t* label_ready = nullptr;
    lv_obj_t* progress = nullptr;
    lv_obj_t* meter_box = nullptr;
    lv_obj_t* meter_segments[kMeterSegments] = {};
    lv_obj_t* btn_rx = nullptr;
    lv_obj_t* btn_rx_label = nullptr;
    lv_obj_t* label_gain = nullptr;
    lv_obj_t* btn_gain_minus = nullptr;
    lv_obj_t* btn_gain_plus = nullptr;
    lv_obj_t* label_clip = nullptr;
};

SstvUi s_ui;
lv_timer_t* s_refresh_timer = nullptr;
int s_last_meter_active = -1;
// When true, the audio meter renders its active segments RED to warn that the
// mic input is overdriving (clipping). Driven from refresh_cb via the SSTV
// Status.clipping flag; consumed by ui_sstv_set_audio_level.
bool s_meter_clipping = false;
bool s_last_meter_clipping = false;
platform::ui::sstv::State s_last_state = platform::ui::sstv::State::Idle;
uint16_t s_last_line = 0;
char s_last_mode[24] = "";
lv_image_dsc_t s_frame_dsc = {};
bool s_frame_ready = false;

void ensure_frame_dsc();

void on_back(lv_event_t*)
{
    request_exit();
}

void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE)
    {
        return;
    }
    on_back(nullptr);
}

void update_battery_labels()
{
    if (!s_ui.top_bar.right_label)
    {
        return;
    }
    ui_update_top_bar_battery(s_ui.top_bar);
}

void update_rx_button_label()
{
    if (!s_ui.btn_rx_label)
    {
        return;
    }
    ::ui::i18n::set_label_text(s_ui.btn_rx_label,
                               platform::ui::sstv::is_active() ? "STOP" : "RX");
}

void on_rx_btn_clicked(lv_event_t*)
{
    if (platform::ui::sstv::is_active())
    {
        platform::ui::sstv::stop();
    }
    else
    {
        bool ok = platform::ui::sstv::start();
        if (!ok && s_ui.label_state_sub)
        {
            const char* err = platform::ui::sstv::last_error();
            ::ui::i18n::set_label_text(s_ui.label_state_sub, err ? err : "SSTV start failed");
        }
    }
    update_rx_button_label();
}

void on_rx_btn_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_ENTER)
    {
        return;
    }
    on_rx_btn_clicked(nullptr);
}

// Push the current SSTV mic gain into the "Gain: NN dB" label.
void refresh_gain_label()
{
    if (!s_ui.label_gain)
    {
        return;
    }
    int db = static_cast<int>(lroundf(platform::ui::sstv::get_gain()));
    const std::string text = ::ui::i18n::format("Gain: %d dB", db);
    lv_label_set_text(s_ui.label_gain, text.c_str());
}

// Touch mic-gain control: the P4 is touch-only, so on-screen -/+ buttons step
// the SSTV capture gain. The service clamps to its sane window and, if RX is
// live, applies the change to the open codec immediately. Updating the label
// reflects the clamped value the service actually settled on.
void gain_step(float delta)
{
    platform::ui::sstv::set_gain(platform::ui::sstv::get_gain() + delta);
    refresh_gain_label();
}

void on_gain_minus(lv_event_t*)
{
    gain_step(-kGainStepDb);
}

void on_gain_plus(lv_event_t*)
{
    gain_step(kGainStepDb);
}

void refresh_cb(lv_timer_t*)
{
    update_battery_labels();
    platform::ui::sstv::Status st = platform::ui::sstv::get_status();
    s_meter_clipping = st.clipping;
    ui_sstv_set_audio_level(st.audio_level);

    const char* mode = platform::ui::sstv::mode_name();
    if (!mode || mode[0] == '\0' || strcmp(mode, "Unknown") == 0 ||
        st.state == platform::ui::sstv::State::Waiting)
    {
        mode = "Auto";
    }
    if (s_ui.label_mode && strcmp(mode, s_last_mode) != 0)
    {
        ui_sstv_set_mode(mode);
        snprintf(s_last_mode, sizeof(s_last_mode), "%s", mode);
    }

    if (st.state != s_last_state)
    {
        s_last_state = st.state;
        if (st.state == platform::ui::sstv::State::Waiting)
        {
            ui_sstv_set_state(SSTV_STATE_WAITING);
        }
        else if (st.state == platform::ui::sstv::State::Receiving)
        {
            ui_sstv_set_state(SSTV_STATE_RECEIVING);
        }
        else if (st.state == platform::ui::sstv::State::Complete)
        {
            ui_sstv_set_state(SSTV_STATE_COMPLETE);
        }
        else if (st.state == platform::ui::sstv::State::Error)
        {
            if (s_ui.label_state_sub)
            {
                const char* err = platform::ui::sstv::last_error();
                ::ui::i18n::set_label_text(s_ui.label_state_sub, err ? err : "Decoder error");
            }
            if (s_ui.label_ready)
            {
                ::ui::i18n::set_label_text(s_ui.label_ready, "ERROR");
                lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorWarn), 0);
            }
        }
    }

    if (st.state == platform::ui::sstv::State::Receiving)
    {
        if (s_ui.label_state_sub)
        {
            int pct = static_cast<int>(st.progress * 100.0f + 0.5f);
            const std::string text = ::ui::i18n::format("Decoding: %d%%", pct);
            lv_label_set_text(s_ui.label_state_sub, text.c_str());
        }
        ui_sstv_set_progress(st.progress);
    }
    else if (st.state == platform::ui::sstv::State::Complete)
    {
        ui_sstv_set_progress(1.0f);
        if (s_ui.label_state_sub)
        {
            const char* saved = platform::ui::sstv::last_saved_path();
            if (saved && saved[0] != '\0')
            {
                const std::string text = ::ui::i18n::format("Saved: %s", saved);
                lv_label_set_text(s_ui.label_state_sub, text.c_str());
            }
        }
    }

    if (st.state == platform::ui::sstv::State::Receiving || st.state == platform::ui::sstv::State::Complete)
    {
        if (st.has_image)
        {
            ensure_frame_dsc();
            if (s_frame_ready && s_ui.img)
            {
                ui_sstv_set_image(&s_frame_dsc);
                if (st.line != s_last_line)
                {
                    lv_obj_invalidate(s_ui.img);
                }
            }
        }
    }
    else if (st.state == platform::ui::sstv::State::Waiting)
    {
        ui_sstv_set_image(nullptr);
    }

    if (st.line != s_last_line)
    {
        s_last_line = st.line;
    }
    update_rx_button_label();
}

void ensure_frame_dsc()
{
    if (s_frame_ready)
    {
        return;
    }
    const uint16_t* frame = platform::ui::sstv::framebuffer();
    if (!frame)
    {
        s_frame_ready = false;
        return;
    }

    const uint16_t w = platform::ui::sstv::frame_width();
    const uint16_t h = platform::ui::sstv::frame_height();
    if (w == 0 || h == 0)
    {
        s_frame_ready = false;
        return;
    }

    s_frame_dsc.header.w = w;
    s_frame_dsc.header.h = h;
    s_frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_frame_dsc.data = reinterpret_cast<const uint8_t*>(frame);
    s_frame_dsc.data_size = static_cast<uint32_t>(w) * h * 2;
    s_frame_ready = true;
}

void apply_label_style(lv_obj_t* label, const lv_font_t* font, uint32_t color)
{
    if (!label)
    {
        return;
    }
    if (font)
    {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

void build_top_bar(lv_obj_t* parent)
{
    ::ui::widgets::TopBarConfig cfg;
    ::ui::widgets::top_bar_init(s_ui.top_bar, parent, cfg);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, ::ui::i18n::tr("SSTV RECEIVER"));
    ::ui::widgets::top_bar_set_back_callback(
        s_ui.top_bar, [](void*)
        { request_exit(); },
        nullptr);
    if (s_ui.top_bar.container)
    {
        lv_obj_set_pos(s_ui.top_bar.container, 0, 0);
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_ui.top_bar.back_btn, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    update_battery_labels();
}

void build_main_area(lv_obj_t* parent)
{
    const lv_coord_t main_w = s_layout.screen_w - (s_layout.portrait ? s_layout.main_x * 2 : 0);

    lv_obj_t* main = lv_obj_create(parent);
    lv_obj_set_size(main, main_w, s_layout.main_h);
    lv_obj_set_pos(main, s_layout.main_x, top_bar_height());
    lv_obj_set_style_bg_opa(main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main, 0, 0);
    lv_obj_set_style_pad_all(main, 0, 0);
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.img_box = lv_obj_create(main);
    lv_obj_set_size(s_ui.img_box, s_layout.img_w, s_layout.img_h);
    lv_obj_set_pos(s_ui.img_box, s_layout.img_x, s_layout.img_y);
    lv_obj_set_style_bg_color(s_ui.img_box, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.img_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.img_box, 2, 0);
    lv_obj_set_style_border_color(s_ui.img_box, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.img_box, 8, 0);
    lv_obj_set_style_pad_all(s_ui.img_box, 0, 0);
    lv_obj_clear_flag(s_ui.img_box, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.img = lv_image_create(s_ui.img_box);
    lv_obj_center(s_ui.img);
    lv_obj_add_flag(s_ui.img, LV_OBJ_FLAG_HIDDEN);

    s_ui.img_placeholder = lv_label_create(s_ui.img_box);
    apply_label_style(s_ui.img_placeholder, s_layout.placeholder_font, kColorTextDim);
    ::ui::i18n::set_label_text(s_ui.img_placeholder, "No image");
    lv_obj_center(s_ui.img_placeholder);

    s_ui.info_area = lv_obj_create(main);
    lv_obj_set_size(s_ui.info_area, s_layout.info_w, s_layout.info_h);
    lv_obj_set_pos(s_ui.info_area, s_layout.info_x, s_layout.info_y);
    lv_obj_set_style_bg_opa(s_ui.info_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.info_area, 0, 0);
    lv_obj_set_style_pad_all(s_ui.info_area, 0, 0);
    lv_obj_clear_flag(s_ui.info_area, LV_OBJ_FLAG_SCROLLABLE);

    // Progress bar lives inside the info panel (portrait) or the main area
    // (landscape, info_x == progress_x so it overlaps the info column).
    s_ui.progress = lv_bar_create(s_layout.portrait ? s_ui.info_area : main);
    lv_obj_set_size(s_ui.progress, s_layout.progress_w, s_layout.progress_h);
    lv_obj_set_pos(s_ui.progress, s_layout.progress_x, s_layout.progress_y);
    lv_bar_set_range(s_ui.progress, 0, 100);
    lv_bar_set_value(s_ui.progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.progress, lv_color_hex(kColorLine), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.progress, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.progress, lv_color_hex(kColorAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.progress, 4, LV_PART_INDICATOR);

    s_ui.label_state_sub = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_state_sub, 0, s_layout.state_sub_y);
    lv_obj_set_width(s_ui.label_state_sub, s_layout.info_text_w);
    lv_obj_set_style_text_align(s_ui.label_state_sub, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_state_sub, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_state_sub, s_layout.state_sub_font, kColorTextDim);

    s_ui.label_mode = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_mode, 0, s_layout.mode_y);
    lv_obj_set_width(s_ui.label_mode, s_layout.info_text_w);
    lv_obj_set_style_text_align(s_ui.label_mode, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_mode, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_mode, s_layout.mode_font, kColorTextDim);

    s_ui.label_ready = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_ready, 0, s_layout.ready_y);
    lv_obj_set_width(s_ui.label_ready, s_layout.info_text_w);
    lv_obj_set_style_text_align(s_ui.label_ready, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_ready, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_ready, s_layout.ready_font, kColorText);

    s_ui.btn_rx = lv_btn_create(s_ui.info_area);
    lv_obj_set_size(s_ui.btn_rx, s_layout.btn_rx_w, s_layout.btn_rx_h);
    lv_obj_set_pos(s_ui.btn_rx, s_layout.btn_rx_x, s_layout.btn_rx_y);
    lv_obj_set_style_bg_color(s_ui.btn_rx, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.btn_rx, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.btn_rx, 1, 0);
    lv_obj_set_style_border_color(s_ui.btn_rx, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.btn_rx, 6, 0);
    lv_obj_t* rx_label = lv_label_create(s_ui.btn_rx);
    apply_label_style(rx_label, s_layout.btn_rx_font, kColorText);
    ::ui::i18n::set_label_text(rx_label, "RX");
    lv_obj_center(rx_label);
    s_ui.btn_rx_label = rx_label;
    lv_obj_add_event_cb(s_ui.btn_rx, on_rx_btn_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_ui.btn_rx, on_rx_btn_key, LV_EVENT_KEY, nullptr);

    // Manual mic-gain controls (portrait/touch only). A "Gain: NN dB" readout
    // above a pair of big -/+ touch buttons that step the SSTV capture gain. They
    // work before RX (set the gain RX starts with) and during RX (live adjust).
    if (s_layout.show_gain)
    {
        s_ui.label_gain = lv_label_create(s_ui.info_area);
        lv_obj_set_pos(s_ui.label_gain, 0, s_layout.gain_label_y);
        lv_obj_set_width(s_ui.label_gain, s_layout.info_w);
        lv_obj_set_style_text_align(s_ui.label_gain, LV_TEXT_ALIGN_CENTER, 0);
        apply_label_style(s_ui.label_gain, s_layout.gain_label_font, kColorText);
        ::ui::i18n::set_label_text(s_ui.label_gain, "Gain: -- dB");

        const lv_coord_t minus_x = 0;
        const lv_coord_t plus_x = static_cast<lv_coord_t>(s_layout.info_w - s_layout.gain_btn_w);

        s_ui.btn_gain_minus = lv_btn_create(s_ui.info_area);
        lv_obj_set_size(s_ui.btn_gain_minus, s_layout.gain_btn_w, s_layout.gain_btn_h);
        lv_obj_set_pos(s_ui.btn_gain_minus, minus_x, s_layout.gain_btn_y);
        lv_obj_set_style_bg_color(s_ui.btn_gain_minus, lv_color_hex(kColorPanelBg), 0);
        lv_obj_set_style_bg_opa(s_ui.btn_gain_minus, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_ui.btn_gain_minus, 1, 0);
        lv_obj_set_style_border_color(s_ui.btn_gain_minus, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_radius(s_ui.btn_gain_minus, 6, 0);
        lv_obj_clear_flag(s_ui.btn_gain_minus, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_ui.btn_gain_minus, on_gain_minus, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* gain_minus_label = lv_label_create(s_ui.btn_gain_minus);
        apply_label_style(gain_minus_label, s_layout.gain_btn_font, kColorText);
        ::ui::i18n::set_label_text(gain_minus_label, "GAIN -");
        lv_obj_center(gain_minus_label);

        s_ui.btn_gain_plus = lv_btn_create(s_ui.info_area);
        lv_obj_set_size(s_ui.btn_gain_plus, s_layout.gain_btn_w, s_layout.gain_btn_h);
        lv_obj_set_pos(s_ui.btn_gain_plus, plus_x, s_layout.gain_btn_y);
        lv_obj_set_style_bg_color(s_ui.btn_gain_plus, lv_color_hex(kColorPanelBg), 0);
        lv_obj_set_style_bg_opa(s_ui.btn_gain_plus, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_ui.btn_gain_plus, 1, 0);
        lv_obj_set_style_border_color(s_ui.btn_gain_plus, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_radius(s_ui.btn_gain_plus, 6, 0);
        lv_obj_clear_flag(s_ui.btn_gain_plus, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_ui.btn_gain_plus, on_gain_plus, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* gain_plus_label = lv_label_create(s_ui.btn_gain_plus);
        apply_label_style(gain_plus_label, s_layout.gain_btn_font, kColorText);
        ::ui::i18n::set_label_text(gain_plus_label, "GAIN +");
        lv_obj_center(gain_plus_label);

        refresh_gain_label();
    }

    s_ui.meter_box = lv_obj_create(s_ui.info_area);
    lv_obj_set_size(s_ui.meter_box, s_layout.meter_w, s_layout.meter_h);
    lv_obj_set_pos(s_ui.meter_box, s_layout.meter_x, s_layout.meter_y);
    lv_obj_set_style_bg_opa(s_ui.meter_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.meter_box, 1, 0);
    lv_obj_set_style_border_color(s_ui.meter_box, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.meter_box, 2, 0);
    lv_obj_set_style_pad_all(s_ui.meter_box, 0, 0);
    lv_obj_clear_flag(s_ui.meter_box, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < kMeterSegments; ++i)
    {
        lv_obj_t* seg = lv_obj_create(s_ui.meter_box);
        lv_obj_set_size(seg, s_layout.meter_w - 4, s_layout.meter_seg_h);
        lv_coord_t y = s_layout.meter_h - 2 - s_layout.meter_seg_h -
                       (i * (s_layout.meter_seg_h + s_layout.meter_seg_gap));
        lv_obj_set_pos(seg, 2, y);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_radius(seg, 2, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_40, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        s_ui.meter_segments[i] = seg;
    }

    // "CLIP" caption, parented to the info panel and positioned just above the
    // meter box. Hidden until the input clips, then shown in red alongside the
    // RED meter so the overdrive warning is unmistakable.
    s_ui.label_clip = lv_label_create(s_ui.info_area);
    apply_label_style(s_ui.label_clip, s_layout.placeholder_font, kColorClip);
    ::ui::i18n::set_label_text(s_ui.label_clip, "CLIP");
    {
        lv_coord_t clip_y = static_cast<lv_coord_t>(s_layout.meter_y - 16);
        if (clip_y < 0)
        {
            clip_y = 0;
        }
        lv_obj_set_pos(s_ui.label_clip, s_layout.meter_x, clip_y);
    }
    lv_obj_add_flag(s_ui.label_clip, LV_OBJ_FLAG_HIDDEN);

    if (s_ui.progress)
    {
        lv_obj_move_foreground(s_ui.progress);
    }
}

void reset_ui_pointers()
{
    s_ui = {};
    s_last_meter_active = -1;
    s_meter_clipping = false;
    s_last_meter_clipping = false;
    s_last_state = platform::ui::sstv::State::Idle;
    s_last_line = 0;
    s_last_mode[0] = '\0';
    s_frame_ready = false;
}

} // namespace

lv_obj_t* ui_sstv_create(lv_obj_t* parent)
{
    if (!parent)
    {
        return nullptr;
    }
    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_pointers();
    }

    // Resolve a responsive layout from the actual parent/screen size. The tall
    // portrait arrangement (P4 540x1168) is chosen only when the screen is both
    // large and taller than wide; every other screen keeps the original 480x192
    // landscape geometry byte-for-byte.
    lv_obj_update_layout(parent);
    const int parent_w = lv_obj_get_width(parent);
    const int parent_h = lv_obj_get_height(parent);
    const int top_bar_h = top_bar_height();
    s_layout = resolve_layout(parent_w, parent_h, top_bar_h);

    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, s_layout.screen_w, top_bar_height() + s_layout.main_h);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    build_top_bar(s_ui.root);
    build_main_area(s_ui.root);

    // Keep the top bar (and its back button) above the panels so the back
    // chevron stays tappable in every layout.
    if (s_ui.top_bar.container)
    {
        lv_obj_move_foreground(s_ui.top_bar.container);
    }

    ui_sstv_set_state(SSTV_STATE_WAITING);
    ui_sstv_set_mode("Auto");
    ui_sstv_set_progress(0.0f);
    ui_sstv_set_audio_level(0.0f);
    update_rx_button_label();

    update_battery_labels();
    return s_ui.root;
}

void ui_sstv_enter(lv_obj_t* parent)
{
    if (platform::ui::device::power_tier() >= 1)
    {
        ui::SystemNotification::show(::ui::i18n::tr("Low battery - audio disabled"), 3000);
    }

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    ui_sstv_create(parent);

    if (::app_g && s_ui.top_bar.back_btn)
    {
        lv_group_remove_all_objs(::app_g);
        lv_group_add_obj(::app_g, s_ui.top_bar.back_btn);
        if (s_ui.btn_rx)
        {
            lv_group_add_obj(::app_g, s_ui.btn_rx);
        }
        if (s_ui.btn_gain_minus)
        {
            lv_group_add_obj(::app_g, s_ui.btn_gain_minus);
        }
        if (s_ui.btn_gain_plus)
        {
            lv_group_add_obj(::app_g, s_ui.btn_gain_plus);
        }
        lv_group_focus_obj(s_ui.top_bar.back_btn);
        set_default_group(::app_g);
        lv_group_set_editing(::app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    if (s_ui.label_state_sub)
    {
        ::ui::i18n::set_label_text(s_ui.label_state_sub, "Press RX to start");
    }
    update_rx_button_label();

    platform::ui::screen::disable_sleep();

    if (!s_refresh_timer)
    {
        s_refresh_timer = lv_timer_create(refresh_cb, 120, nullptr);
    }
    refresh_cb(nullptr);
}

void ui_sstv_exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_refresh_timer)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    platform::ui::sstv::stop();
    platform::ui::screen::enable_sleep();

    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_pointers();
    }
}

void ui_sstv_set_state(SstvState state)
{
    if (!s_ui.label_state_sub || !s_ui.label_ready)
    {
        return;
    }

    switch (state)
    {
    case SSTV_STATE_WAITING:
        ::ui::i18n::set_label_text(s_ui.label_state_sub, "Listening for SSTV signal...");
        ::ui::i18n::set_label_text(s_ui.label_ready, "SSTV RX READY");
        lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorText), 0);
        ui_sstv_set_image(nullptr);
        ui_sstv_set_progress(0.0f);
        break;
    case SSTV_STATE_RECEIVING:
        ::ui::i18n::set_label_text(s_ui.label_state_sub, "Decoding: 0%");
        ::ui::i18n::set_label_text(s_ui.label_ready, "RECEIVING");
        lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorOk), 0);
        break;
    case SSTV_STATE_COMPLETE:
        ::ui::i18n::set_label_text(s_ui.label_state_sub, "Image received");
        ::ui::i18n::set_label_text(s_ui.label_ready, "COMPLETE");
        lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorOk), 0);
        ui_sstv_set_progress(1.0f);
        break;
    default:
        break;
    }
}

void ui_sstv_set_mode(const char* mode_str)
{
    if (!s_ui.label_mode)
    {
        return;
    }
    char buf[48];
    if (!mode_str || mode_str[0] == '\0')
    {
        snprintf(buf, sizeof(buf), "%s", ::ui::i18n::tr("MODE: Auto"));
    }
    else
    {
        const std::string text = ::ui::i18n::format("MODE: %s", mode_str);
        snprintf(buf, sizeof(buf), "%s", text.c_str());
    }
    lv_label_set_text(s_ui.label_mode, buf);
}

void ui_sstv_set_audio_level(float level_0_1)
{
    if (!s_ui.meter_segments[0])
    {
        return;
    }
    if (level_0_1 < 0.0f)
    {
        level_0_1 = 0.0f;
    }
    if (level_0_1 > 1.0f)
    {
        level_0_1 = 1.0f;
    }
    int active = static_cast<int>(lroundf(level_0_1 * kMeterSegments));
    if (active < 0)
    {
        active = 0;
    }
    if (active > kMeterSegments)
    {
        active = kMeterSegments;
    }
    // Re-render when either the level OR the clip state changed. The clip flag
    // recolors every active segment, so a clip transition with an unchanged level
    // must still repaint.
    const bool clipping = s_meter_clipping;
    if (active == s_last_meter_active && clipping == s_last_meter_clipping)
    {
        return;
    }
    s_last_meter_active = active;
    s_last_meter_clipping = clipping;

    for (int i = 0; i < kMeterSegments; ++i)
    {
        lv_obj_t* seg = s_ui.meter_segments[i];
        if (!seg)
        {
            continue;
        }
        bool on = (i < active);
        uint32_t color;
        // When the input is clipping, every lit segment goes RED so the whole
        // meter reads as an alarm; the owner drops GAIN until it clears.
        if (clipping)
        {
            color = kColorClip;
        }
        else if (i >= 8)
        {
            color = kColorWarn;
        }
        else if (i >= 4)
        {
            color = kColorMeterMid;
        }
        else
        {
            color = kColorOk;
        }
        if (on)
        {
            lv_obj_set_style_bg_color(seg, lv_color_hex(color), 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(seg, lv_color_hex(kColorLine), 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_40, 0);
        }
    }

    // Small "CLIP" caption above the meter, shown only while clipping.
    if (s_ui.label_clip)
    {
        if (clipping)
        {
            lv_obj_clear_flag(s_ui.label_clip, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_ui.label_clip, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_sstv_set_progress(float p_0_1)
{
    if (!s_ui.progress)
    {
        return;
    }
    if (p_0_1 < 0.0f)
    {
        p_0_1 = 0.0f;
    }
    if (p_0_1 > 1.0f)
    {
        p_0_1 = 1.0f;
    }
    int value = static_cast<int>(lroundf(p_0_1 * 100.0f));
    lv_bar_set_value(s_ui.progress, value, LV_ANIM_OFF);
}

void ui_sstv_set_image(const void* img_src_or_lv_img_dsc)
{
    if (!s_ui.img)
    {
        return;
    }
    if (img_src_or_lv_img_dsc)
    {
        lv_image_set_src(s_ui.img, img_src_or_lv_img_dsc);
        lv_obj_center(s_ui.img);
        lv_obj_clear_flag(s_ui.img, LV_OBJ_FLAG_HIDDEN);
        if (s_ui.img_placeholder)
        {
            lv_obj_add_flag(s_ui.img_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        lv_obj_add_flag(s_ui.img, LV_OBJ_FLAG_HIDDEN);
        if (s_ui.img_placeholder)
        {
            lv_obj_clear_flag(s_ui.img_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

namespace sstv_page::ui::runtime
{

bool is_available()
{
    return platform::ui::sstv::is_supported();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    s_host = host;
    ui_sstv_enter(parent);
}

void exit(lv_obj_t* parent)
{
    ui_sstv_exit(parent);
    s_host = nullptr;
}

} // namespace sstv_page::ui::runtime

#else

namespace sstv_page::ui::runtime
{

bool is_available()
{
    return false;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    (void)host;
    (void)parent;
}

void exit(lv_obj_t* parent)
{
    (void)parent;
}

} // namespace sstv_page::ui::runtime

#endif
