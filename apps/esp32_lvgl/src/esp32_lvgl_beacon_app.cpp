// Emergency Beacon: a self-contained "I need help" beacon that, while ARMED,
// simultaneously (a) strobes the whole screen white/black in International Morse
// SOS ( ... --- ... ), (b) plays an audible SOS tone through the ES8311 speaker,
// and (c) periodically broadcasts the current GPS position over the LoRa mesh so
// any nearby node can see it. When OFF, every timer is deleted and the codec +
// radio are released (nothing runs).
//
// Same self-contained inline pattern as the Flashlight / Field Guide / Translate
// apps: file-static state, enter()/exit() build and tear everything down, Back
// routes through ui_request_exit_to_menu(), and a global CallbackAppScreen symbol
// (g_beacon_app, OUTSIDE the anonymous namespace) is added to the IDF registry's
// s_apps[] via an extern declaration there.
//
// Boot self-test safety: enter() only builds the UI -- it never activates, opens
// the codec, disables sleep, raises brightness, touches the app facade, or reads
// GPS. So the self-test's bare enter -> exit leaves absolutely nothing running.
//
// (a) SCREEN STROBE: a full-bleed surface repainted white (lit) / black (dark).
//     One VARIABLE-period lv_timer walks a flat {lit, ms} SOS segment table and
//     re-arms itself to each segment's Morse duration (the same lv_timer_set_period
//     technique the Flashlight strobe uses), so timing is exact without busy-wait.
//     Each tick also updates the shared s_state.sos_lit flag the audio tick reads.
//
// (b) AUDIO (T-Display P4 only): a FIXED-period 20 ms lv_timer that ALWAYS writes
//     a full 160-sample (= 20 ms @ 8 kHz) mono block every tick -- a ~1 kHz sine
//     when sos_lit is true, or SILENCE (zeros) when false. Always feeding a full
//     block keeps the I2S DMA pipeline fed at all times (no underrun / no gating of
//     codec->write), and naturally gates the tone to the Morse pattern. A failed
//     write pauses the audio timer and shows a note; the light + radio continue.
//
// (c) MESH POSITION BROADCAST: a self-paced periodic send (immediate on activate,
//     then every 30 s). Each attempt is GUARDED by app::hasAppFacade() (calling a
//     facade getter unbound aborts the firmware) and broadcasts a compact "SOS
//     <lat>,<lng> B<batt>%" text on ChannelId::PRIMARY to peer 0 (all nodes).
//     sendText() is blocking and can silently fail (returns 0 if the radio is held
//     by Walkie or duty-cycle gated); any failure is treated as "skip, retry next
//     cycle" -- never spin-retry, never assume delivery.

#include "lvgl.h"
#include "ui/widgets/back_button.h"

#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"

#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/screen_runtime.h"

#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"    // chat::ChannelId
#include "chat/usecase/chat_service.h" // chat::ChatService::sendText

// The ES8311 codec wrapper lives in the t_display_p4 board sources, compiled only
// for the P4 IDF target; guard the include + all audio code on the P4 board (this
// mirrors how esp32_lvgl_idf_app_registry.cpp guards its System Test audio path).
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/codec_es8311.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>

namespace
{

// ---------------------------------------------------------------------------
// Morse SOS timing. A dot is the base unit; a dash is three dots; the gap inside
// a letter is one dot; between letters three dots; between words (before the SOS
// pattern repeats) seven dots. 200 ms/dot gives a clearly readable strobe.
// ---------------------------------------------------------------------------
constexpr uint32_t kDotMs = 200;
constexpr uint32_t kDashMs = kDotMs * 3;      // 600
constexpr uint32_t kSymbolGapMs = kDotMs;     // 200 (within a letter)
constexpr uint32_t kLetterGapMs = kDotMs * 3; // 600 (between letters)
constexpr uint32_t kWordGapMs = kDotMs * 7;   // 1400 (before the pattern repeats)

// Audio parameters: 8 kHz mono, a ~1 kHz output tone, a 20 ms timer tick -> a
// 160-sample block per tick == exactly 20 ms of audio, matching the timer period
// so the I2S pipeline stays continuously fed (copied from the System Test speaker
// test; see the underrun-safe note above).
constexpr uint32_t kAudioSampleRate = 8000;
constexpr float kAudioToneHz = 1000.0f;
constexpr uint32_t kAudioTimerPeriodMs = 20;
constexpr int kAudioFramesPerTick = kAudioSampleRate * kAudioTimerPeriodMs / 1000; // 160

// Broadcast cadence + the 1 Hz housekeeping tick that both drives the periodic
// send and refreshes the "sent N / last Xs ago" + live-position readouts.
constexpr uint32_t kBroadcastIntervalMs = 30000; // send at most every 30 s
constexpr uint32_t kUiTickMs = 1000;

// Panel / status colors (self-contained; no ui::theme dependency in this TU).
constexpr uint32_t kColText = 0xF5F5F5;
constexpr uint32_t kColPanel = 0x101014;
constexpr uint32_t kColArmed = 0xFF3B30;   // ARMED status + Deactivate button
constexpr uint32_t kColOff = 0x9AA0A6;     // OFF status text
constexpr uint32_t kColGo = 0x2E7D32;      // Activate button (green)
constexpr uint32_t kColStop = 0xC62828;    // Deactivate button (red)
constexpr uint32_t kColNote = 0xFFB020;    // audio-unavailable note (amber)

struct BeaconSegment
{
    bool lit;
    uint32_t duration_ms;
};

// One SOS cycle as a flat {lit, duration} table: S ( . . . ), letter gap, O
// ( - - - ), letter gap, S ( . . . ), word gap, then repeat. Lit paints white,
// unlit paints black; the same table also gates the audio tone via sos_lit.
const BeaconSegment kSosPattern[] = {
    // S: . . .
    {true, kDotMs},  {false, kSymbolGapMs},
    {true, kDotMs},  {false, kSymbolGapMs},
    {true, kDotMs},  {false, kLetterGapMs},
    // O: - - -
    {true, kDashMs}, {false, kSymbolGapMs},
    {true, kDashMs}, {false, kSymbolGapMs},
    {true, kDashMs}, {false, kLetterGapMs},
    // S: . . .
    {true, kDotMs},  {false, kSymbolGapMs},
    {true, kDotMs},  {false, kSymbolGapMs},
    {true, kDotMs},  {false, kWordGapMs},
};

constexpr int kSosPatternLen = static_cast<int>(sizeof(kSosPattern) / sizeof(kSosPattern[0]));

struct BeaconState
{
    // Widgets.
    lv_obj_t* root = nullptr;         // full-bleed app container
    lv_obj_t* strobe = nullptr;       // full-bleed white/black surface (at the back)
    lv_obj_t* status_label = nullptr; // ARMED / OFF
    lv_obj_t* toggle_label = nullptr; // ACTIVATE / DEACTIVATE (on the big button)
    lv_obj_t* toggle_btn = nullptr;   // the big activate/deactivate button
    lv_obj_t* pos_label = nullptr;    // position being broadcast / no position
    lv_obj_t* bcast_label = nullptr;  // "Sent N (last Xs ago)"
    lv_obj_t* note_label = nullptr;   // "Audio unavailable" (hidden unless failed)
    lv_obj_t* sound_label = nullptr;  // Sound: on/off

    // Timers (every one stored here and deleted in teardown -- no leaks across
    // enter/exit).
    lv_timer_t* strobe_timer = nullptr; // variable-period SOS strobe
    lv_timer_t* audio_timer = nullptr;  // fixed 20 ms audio feed (P4 only)
    lv_timer_t* bcast_timer = nullptr;  // 1 Hz housekeeping + periodic send

    // Run state.
    bool active = false;        // is the beacon ARMED?
    bool sos_lit = false;       // shared: strobe tick sets, audio tick reads
    bool sound_enabled = true;  // Sound on/off (default ON)
    bool audio_failed = false;  // codec open/write failed -> show note
    int seg = 0;                // index into kSosPattern

    // Broadcast bookkeeping (time tracked via LVGL's monotonic tick).
    uint32_t last_bcast_tick = 0;
    uint32_t sent_count = 0;
    bool have_bcast = false;

    // Balanced-once resources.
    bool sleep_disabled = false;   // did we disable the idle auto-sleep?
    bool brightness_saved = false; // did we capture + raise the backlight?
    uint8_t prev_brightness = 0;

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    boards::t_display_p4::CodecEs8311* codec = nullptr;
    double sine_phase = 0.0;
#endif
};

BeaconState s_state;

// ---- painting / small helpers ---------------------------------------------

void beacon_paint(BeaconState* st, bool lit)
{
    if (!st || !st->strobe || !lv_obj_is_valid(st->strobe))
    {
        return;
    }
    lv_obj_set_style_bg_color(st->strobe, lit ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(st->strobe, LV_OPA_COVER, 0);
}

// Inhibit / release the idle auto-sleep task, balanced exactly once via the bool
// (on this board the inhibit is a plain flag, so an unbalanced call would leave
// the panel stuck awake; guarding on sleep_disabled makes disable/enable idempotent).
void beacon_disable_sleep(BeaconState* st)
{
    if (!st || st->sleep_disabled)
    {
        return;
    }
    platform::ui::screen::disable_sleep();
    st->sleep_disabled = true;
}

void beacon_enable_sleep(BeaconState* st)
{
    if (!st || !st->sleep_disabled)
    {
        return;
    }
    platform::ui::screen::enable_sleep();
    st->sleep_disabled = false;
}

// Drive the backlight to maximum (capturing the prior level once so deactivate
// can restore it). set_screen_brightness maps to a percent the board clamps to
// 0..100, so 0xFF saturates to full brightness on any board max.
void beacon_raise_brightness(BeaconState* st)
{
    if (!st || st->brightness_saved || !platform::ui::device::supports_screen_brightness())
    {
        return;
    }
    st->prev_brightness = platform::ui::device::screen_brightness();
    st->brightness_saved = true;
    platform::ui::device::set_screen_brightness(0xFF);
}

void beacon_restore_brightness(BeaconState* st)
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

// ---- label refreshers ------------------------------------------------------

void beacon_update_status(BeaconState* st)
{
    if (!st)
    {
        return;
    }
    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        lv_label_set_text(st->status_label, st->active ? "ARMED - SENDING SOS" : "OFF");
        lv_obj_set_style_text_color(st->status_label,
                                    lv_color_hex(st->active ? kColArmed : kColOff), 0);
    }
    if (st->toggle_label && lv_obj_is_valid(st->toggle_label))
    {
        lv_label_set_text(st->toggle_label, st->active ? "DEACTIVATE" : "ACTIVATE");
    }
    if (st->toggle_btn && lv_obj_is_valid(st->toggle_btn))
    {
        lv_obj_set_style_bg_color(st->toggle_btn,
                                  lv_color_hex(st->active ? kColStop : kColGo), 0);
    }
}

void beacon_update_sound_label(BeaconState* st)
{
    if (st && st->sound_label && lv_obj_is_valid(st->sound_label))
    {
        lv_label_set_text(st->sound_label, st->sound_enabled ? "Sound: ON" : "Sound: OFF");
    }
}

void beacon_update_note(BeaconState* st)
{
    if (!st || !st->note_label || !lv_obj_is_valid(st->note_label))
    {
        return;
    }
    if (st->audio_failed)
    {
        lv_label_set_text(st->note_label, "Audio unavailable (light + radio still active)");
        lv_obj_clear_flag(st->note_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(st->note_label, "");
        lv_obj_add_flag(st->note_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// Refresh the live-position and broadcast-progress readouts. GPS is only read
// while ARMED, so the boot self-test (which never activates) never calls the GPS
// runtime.
void beacon_update_readouts(BeaconState* st)
{
    if (!st)
    {
        return;
    }
    if (st->pos_label && lv_obj_is_valid(st->pos_label))
    {
        char buf[80];
        if (!st->active)
        {
            std::snprintf(buf, sizeof(buf), "Standby (activate to broadcast)");
        }
        else
        {
            const platform::ui::gps::GpsState gps = platform::ui::gps::get_data();
            if (gps.valid)
            {
                std::snprintf(buf, sizeof(buf), "Pos: %.6f, %.6f", gps.lat, gps.lng);
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "No GPS fix (broadcasting SOS alert)");
            }
        }
        lv_label_set_text(st->pos_label, buf);
    }
    if (st->bcast_label && lv_obj_is_valid(st->bcast_label))
    {
        char buf[80];
        if (!st->active)
        {
            std::snprintf(buf, sizeof(buf), "Broadcast: off");
        }
        else if (st->have_bcast)
        {
            const uint32_t secs = lv_tick_elaps(st->last_bcast_tick) / 1000;
            std::snprintf(buf, sizeof(buf), "Sent %lu  (last %lus ago)",
                          static_cast<unsigned long>(st->sent_count),
                          static_cast<unsigned long>(secs));
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "Broadcast: pending (radio busy?)");
        }
        lv_label_set_text(st->bcast_label, buf);
    }
}

// ---- mesh broadcast --------------------------------------------------------

// Attempt one position broadcast. Stamps last_bcast_tick on EVERY attempt (so a
// failure waits the full interval before retrying -- no spin), and only counts a
// success (sendText returns a non-zero MessageId). GUARDED by hasAppFacade()
// because calling a facade getter while unbound aborts the firmware.
void beacon_broadcast(BeaconState* st)
{
    if (!st)
    {
        return;
    }
    st->last_bcast_tick = lv_tick_get();

    if (!app::hasAppFacade())
    {
        return;
    }

    const platform::ui::gps::GpsState gps = platform::ui::gps::get_data();
    const platform::ui::device::BatteryInfo batt = platform::ui::device::battery_info();

    char msg[80];
    if (gps.valid)
    {
        if (batt.level >= 0)
        {
            std::snprintf(msg, sizeof(msg), "SOS %.6f,%.6f B%d%%", gps.lat, gps.lng, batt.level);
        }
        else
        {
            std::snprintf(msg, sizeof(msg), "SOS %.6f,%.6f", gps.lat, gps.lng);
        }
    }
    else
    {
        if (batt.level >= 0)
        {
            std::snprintf(msg, sizeof(msg), "SOS no GPS fix B%d%%", batt.level);
        }
        else
        {
            std::snprintf(msg, sizeof(msg), "SOS no GPS fix");
        }
    }

    // Broadcast on the public/primary channel to peer 0 (all nodes). Blocking;
    // returns 0 on failure (radio held / duty-cycle gated) -- treated as skip.
    auto& chat = app::messagingFacade().getChatService();
    const chat::MessageId id = chat.sendText(chat::ChannelId::PRIMARY, std::string(msg), 0);
    if (id != 0)
    {
        ++st->sent_count;
        st->have_bcast = true;
    }
}

// ---- timer ticks -----------------------------------------------------------

// SOS strobe tick: paint the current segment, publish sos_lit for the audio tick,
// advance (wrapping), and re-arm to the next segment's Morse duration.
void beacon_strobe_tick(lv_timer_t* timer)
{
    auto* st = static_cast<BeaconState*>(lv_timer_get_user_data(timer));
    if (!st)
    {
        return;
    }
    if (st->seg < 0 || st->seg >= kSosPatternLen)
    {
        st->seg = 0;
    }
    const BeaconSegment& s = kSosPattern[st->seg];
    st->sos_lit = s.lit;
    beacon_paint(st, s.lit);
    lv_timer_set_period(timer, s.duration_ms);
    st->seg = (st->seg + 1) % kSosPatternLen;
}

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
// Audio tick: ALWAYS write a full 160-sample block -- a ~1 kHz sine while sos_lit,
// otherwise silence -- so the I2S pipeline never starves (underrun-safe) and the
// tone is gated to the Morse pattern. A failed write pauses the timer and flags
// the note; the strobe + radio keep running.
void beacon_audio_tick(lv_timer_t* timer)
{
    auto* st = static_cast<BeaconState*>(lv_timer_get_user_data(timer));
    if (!st || !st->codec)
    {
        return;
    }
    int16_t samples[kAudioFramesPerTick];
    if (st->sos_lit)
    {
        const double step = 2.0 * M_PI * kAudioToneHz / static_cast<double>(kAudioSampleRate);
        for (int i = 0; i < kAudioFramesPerTick; ++i)
        {
            samples[i] = static_cast<int16_t>(std::sin(st->sine_phase) * 9000.0);
            st->sine_phase += step;
            if (st->sine_phase >= 2.0 * M_PI)
            {
                st->sine_phase -= 2.0 * M_PI;
            }
        }
    }
    else
    {
        for (int i = 0; i < kAudioFramesPerTick; ++i)
        {
            samples[i] = 0;
        }
    }
    const int rc = st->codec->write(reinterpret_cast<uint8_t*>(samples), sizeof(samples));
    if (rc < 0)
    {
        lv_timer_pause(timer);
        st->audio_failed = true;
        beacon_update_note(st);
    }
}
#endif // TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4

// 1 Hz housekeeping tick: fire the periodic broadcast when due (self-paced, no
// spin) and refresh the readouts.
void beacon_ui_tick(lv_timer_t* timer)
{
    auto* st = static_cast<BeaconState*>(lv_timer_get_user_data(timer));
    if (!st || !st->active)
    {
        return;
    }
    if (lv_tick_elaps(st->last_bcast_tick) >= kBroadcastIntervalMs)
    {
        beacon_broadcast(st);
    }
    beacon_update_readouts(st);
}

// ---- audio start / stop (codec lifecycle) ----------------------------------

void beacon_audio_start(BeaconState* st)
{
    if (!st)
    {
        return;
    }
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    if (st->codec || st->audio_timer)
    {
        return; // already running
    }
    st->audio_failed = false;
    auto* codec = new (std::nothrow) boards::t_display_p4::CodecEs8311();
    if (codec == nullptr || codec->open(16, 1, kAudioSampleRate) != 0)
    {
        delete codec;
        st->audio_failed = true;
        beacon_update_note(st);
        return;
    }
    st->codec = codec;
    st->sine_phase = 0.0;
    codec->setOutMute(false);
    codec->setVolume(80);
    st->audio_timer = lv_timer_create(beacon_audio_tick, kAudioTimerPeriodMs, st);
    beacon_update_note(st);
#else
    // No codec on this board: the SOS tone is unavailable; strobe + radio still work.
    st->audio_failed = true;
    beacon_update_note(st);
#endif
}

void beacon_audio_stop(BeaconState* st)
{
    if (!st)
    {
        return;
    }
    if (st->audio_timer)
    {
        lv_timer_del(st->audio_timer);
        st->audio_timer = nullptr;
    }
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    if (st->codec)
    {
        st->codec->close();
        delete st->codec;
        st->codec = nullptr;
    }
    st->sine_phase = 0.0;
#endif
}

// ---- activate / deactivate -------------------------------------------------

void beacon_activate(BeaconState* st)
{
    if (!st || st->active)
    {
        return;
    }
    st->active = true;

    // Keep the panel awake + at full brightness while ARMED.
    beacon_disable_sleep(st);
    beacon_raise_brightness(st);

    // Start the strobe and fire the first segment immediately so the SOS begins on
    // the first dot (this also publishes the initial sos_lit for the audio tick).
    st->seg = 0;
    if (!st->strobe_timer)
    {
        st->strobe_timer = lv_timer_create(beacon_strobe_tick, kDotMs, st);
        if (st->strobe_timer)
        {
            beacon_strobe_tick(st->strobe_timer);
        }
    }

    // Audio (if enabled) and the 1 Hz housekeeping/broadcast timer.
    if (st->sound_enabled)
    {
        beacon_audio_start(st);
    }
    if (!st->bcast_timer)
    {
        st->bcast_timer = lv_timer_create(beacon_ui_tick, kUiTickMs, st);
    }

    // Broadcast the alert immediately on activation, then every 30 s thereafter.
    st->sent_count = 0;
    st->have_bcast = false;
    beacon_broadcast(st);

    beacon_update_status(st);
    beacon_update_readouts(st);
}

// Single teardown that returns the beacon fully to OFF: delete BOTH audio + strobe
// timers and the broadcast timer, close+delete the codec, restore brightness,
// re-enable sleep, clear sos_lit, and repaint black. Idempotent / safe to call
// when not active, and used by both the DEACTIVATE button and beacon_exit.
void beacon_deactivate(BeaconState* st)
{
    if (!st)
    {
        return;
    }
    if (st->strobe_timer)
    {
        lv_timer_del(st->strobe_timer);
        st->strobe_timer = nullptr;
    }
    beacon_audio_stop(st); // deletes audio_timer + closes/deletes codec
    if (st->bcast_timer)
    {
        lv_timer_del(st->bcast_timer);
        st->bcast_timer = nullptr;
    }

    beacon_restore_brightness(st);
    beacon_enable_sleep(st);

    st->active = false;
    st->sos_lit = false;
    st->audio_failed = false;
    st->seg = 0;

    beacon_paint(st, false); // black
    beacon_update_status(st);
    beacon_update_note(st);
    beacon_update_readouts(st);
}

// ---- enter / exit ----------------------------------------------------------

void beacon_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<BeaconState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }

    // Fully reset run state (re-entry safe) -- nothing is armed and no timer/codec
    // exists until the user presses ACTIVATE.
    st->root = nullptr;
    st->strobe = nullptr;
    st->status_label = nullptr;
    st->toggle_label = nullptr;
    st->toggle_btn = nullptr;
    st->pos_label = nullptr;
    st->bcast_label = nullptr;
    st->note_label = nullptr;
    st->sound_label = nullptr;
    st->strobe_timer = nullptr;
    st->audio_timer = nullptr;
    st->bcast_timer = nullptr;
    st->active = false;
    st->sos_lit = false;
    st->sound_enabled = true; // default ON
    st->audio_failed = false;
    st->seg = 0;
    st->last_bcast_tick = 0;
    st->sent_count = 0;
    st->have_bcast = false;
    st->sleep_disabled = false;
    st->brightness_saved = false;
    st->prev_brightness = 0;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    st->codec = nullptr;
    st->sine_phase = 0.0;
#endif

    // Root: full-bleed black container (no flex) so the strobe surface can cover
    // the whole panel (works on both 540x1168 and 568x1232 -- no hardcoded size).
    st->root = lv_obj_create(parent);
    lv_obj_set_size(st->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(st->root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(st->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(st->root, 0, 0);
    lv_obj_set_style_radius(st->root, 0, 0);
    lv_obj_set_style_pad_all(st->root, 0, 0);
    lv_obj_clear_flag(st->root, LV_OBJ_FLAG_SCROLLABLE);

    // Full-bleed strobe surface at the back (added first). Non-interactive.
    st->strobe = lv_obj_create(st->root);
    lv_obj_remove_style_all(st->strobe);
    lv_obj_set_size(st->strobe, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(st->strobe, 0, 0);
    lv_obj_clear_flag(st->strobe, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(st->strobe, LV_OBJ_FLAG_CLICKABLE);
    beacon_paint(st, false);

    // Control panel: an opaque dark card centered over the strobe, so the controls
    // stay legible while the surrounding margins flash white/black. Column flex,
    // percentage width (no hardcoded 540) so it fits either panel.
    lv_obj_t* panel = lv_obj_create(st->root);
    lv_obj_set_width(panel, LV_PCT(88));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x303038), 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Title.
    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "Emergency Beacon");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kColText), 0);

    // Status (ARMED / OFF).
    st->status_label = lv_label_create(panel);
    lv_obj_set_width(st->status_label, LV_PCT(100));
    lv_label_set_long_mode(st->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(st->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->status_label, &lv_font_montserrat_24, 0);

    // Big ACTIVATE / DEACTIVATE toggle.
    st->toggle_btn = lv_button_create(panel);
    lv_obj_set_width(st->toggle_btn, LV_PCT(100));
    lv_obj_set_style_pad_top(st->toggle_btn, 22, 0);
    lv_obj_set_style_pad_bottom(st->toggle_btn, 22, 0);
    st->toggle_label = lv_label_create(st->toggle_btn);
    lv_obj_set_style_text_font(st->toggle_label, &lv_font_montserrat_20, 0);
    lv_obj_center(st->toggle_label);
    lv_obj_add_event_cb(
        st->toggle_btn,
        [](lv_event_t* e)
        {
            auto* s = static_cast<BeaconState*>(lv_event_get_user_data(e));
            if (!s)
            {
                return;
            }
            if (s->active)
            {
                beacon_deactivate(s);
            }
            else
            {
                beacon_activate(s);
            }
        },
        LV_EVENT_CLICKED, st);

    // Live position readout.
    st->pos_label = lv_label_create(panel);
    lv_obj_set_width(st->pos_label, LV_PCT(100));
    lv_label_set_long_mode(st->pos_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(st->pos_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->pos_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(st->pos_label, lv_color_hex(kColText), 0);

    // Broadcast progress readout.
    st->bcast_label = lv_label_create(panel);
    lv_obj_set_width(st->bcast_label, LV_PCT(100));
    lv_label_set_long_mode(st->bcast_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(st->bcast_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->bcast_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(st->bcast_label, lv_color_hex(kColText), 0);

    // Audio-unavailable note (hidden unless the codec fails).
    st->note_label = lv_label_create(panel);
    lv_obj_set_width(st->note_label, LV_PCT(100));
    lv_label_set_long_mode(st->note_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(st->note_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->note_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(st->note_label, lv_color_hex(kColNote), 0);
    lv_label_set_text(st->note_label, "");
    lv_obj_add_flag(st->note_label, LV_OBJ_FLAG_HIDDEN);

    // Sound on/off toggle.
    lv_obj_t* sound_btn = lv_button_create(panel);
    st->sound_label = lv_label_create(sound_btn);
    lv_obj_set_style_text_font(st->sound_label, &lv_font_montserrat_14, 0);
    lv_obj_center(st->sound_label);
    lv_obj_add_event_cb(
        sound_btn,
        [](lv_event_t* e)
        {
            auto* s = static_cast<BeaconState*>(lv_event_get_user_data(e));
            if (!s)
            {
                return;
            }
            s->sound_enabled = !s->sound_enabled;
            // Apply live if armed: start feeding audio when turned on, release the
            // codec when turned off (strobe + radio are unaffected).
            if (s->active)
            {
                if (s->sound_enabled)
                {
                    beacon_audio_start(s);
                }
                else
                {
                    beacon_audio_stop(s);
                    s->audio_failed = false;
                    beacon_update_note(s);
                }
            }
            beacon_update_sound_label(s);
        },
        LV_EVENT_CLICKED, st);

    // Back to the launcher menu.
    lv_obj_t* back_btn = lv_button_create(panel);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    ::ui::style_back_button(back_btn, back_lbl);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { ::ui_request_exit_to_menu(); }, LV_EVENT_CLICKED, nullptr);

    // Paint the initial (OFF) UI.
    beacon_update_status(st);
    beacon_update_sound_label(st);
    beacon_update_note(st);
    beacon_update_readouts(st);
}

void beacon_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<BeaconState*>(user_data);
    if (!st)
    {
        return;
    }
    // FORCE everything off first (even if the user left it ARMED): this deletes
    // all three timers, closes the codec, restores brightness, and re-enables
    // sleep -- all balanced -- before the widgets are freed.
    beacon_deactivate(st);

    if (!st->root || !lv_obj_is_valid(st->root))
    {
        st->root = nullptr;
        st->strobe = nullptr;
        st->status_label = nullptr;
        st->toggle_label = nullptr;
        st->toggle_btn = nullptr;
        st->pos_label = nullptr;
        st->bcast_label = nullptr;
        st->note_label = nullptr;
        st->sound_label = nullptr;
        return;
    }
    lv_obj_del(st->root);
    st->root = nullptr;
    st->strobe = nullptr;
    st->status_label = nullptr;
    st->toggle_label = nullptr;
    st->toggle_btn = nullptr;
    st->pos_label = nullptr;
    st->bcast_label = nullptr;
    st->note_label = nullptr;
    st->sound_label = nullptr;
}

// Launcher icon: the SOS image descriptor, linked on the P4 UI-shared asset set.
extern "C"
{
    extern const lv_image_dsc_t sos;
}

} // namespace

// Registration symbol (external linkage, OUTSIDE the anonymous namespace) so the
// IDF app registry can pull it into s_apps[] via `extern ui::CallbackAppScreen
// g_beacon_app;` -- exactly like g_translate_app / g_field_guide_app.
ui::CallbackAppScreen g_beacon_app("beacon", "Emergency", &sos, beacon_enter, beacon_exit,
                                   &s_state);
