#pragma once

#include <cstdint>

namespace sstv
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
    float progress = 0.0f;    // 0..1
    float audio_level = 0.0f; // 0..1
    bool has_image = false;
};

bool start();
void stop();
bool is_active();

// Mic input gain (dB) for SSTV capture. set_gain() clamps to a sane range and,
// if a capture is active, applies the new gain to the open codec immediately so
// it takes effect live during RX. get_gain() returns the current runtime value.
float get_gain();
void set_gain(float db);

Status get_status();
const char* get_last_error();
const char* get_last_saved_path();
const char* get_mode_name();

const uint16_t* get_framebuffer();
uint16_t frame_width();
uint16_t frame_height();

} // namespace sstv
