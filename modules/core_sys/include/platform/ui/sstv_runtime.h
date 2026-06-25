#pragma once

#include <cstdint>

#include "platform/ui/capability_status.h"

namespace platform::ui::sstv
{

enum class State : uint8_t
{
    Idle = 0,
    Waiting,
    Receiving,
    Complete,
    Error,
};

struct Status
{
    State state = State::Idle;
    uint16_t line = 0;
    float progress = 0.0f;
    float audio_level = 0.0f;
    bool has_image = false;
    // True when the mic input is overdriving (a sustained fraction of the
    // captured PCM samples sit at/near full scale within a read block). The
    // screen colors the audio meter RED on this so the owner can drop GAIN until
    // the input is no longer clipping. Computed in the SSTV capture task.
    bool clipping = false;
};

bool is_supported();
bool start();
void stop();
bool is_active();

// Mic input gain (dB) for SSTV capture. Forwards to the SSTV service. set_gain()
// clamps to a sane range and, when a capture is active, applies the new gain to
// the open codec immediately so it takes effect live during RX. get_gain()
// returns the current runtime value. The on-screen GAIN -/+ buttons drive these.
void set_gain(float db);
float get_gain();

Status get_status();
const char* last_error();
const char* last_saved_path();
const char* mode_name();
const uint16_t* framebuffer();
uint16_t frame_width();
uint16_t frame_height();

/// Honest capability status for the current target.
/// May return Unsupported, Simulated, Available, Degraded, or Error.
CapabilityStatus capability_status();

} // namespace platform::ui::sstv
