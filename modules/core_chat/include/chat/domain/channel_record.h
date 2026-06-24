/**
 * @file channel_record.h
 * @brief Pure per-channel record for the Meshtastic multi-channel model.
 *
 * One ChannelRecord describes a single Meshtastic channel slot: whether it is
 * enabled, its display name, its PSK bytes (with the same 0/16/32 length
 * convention used everywhere else), and the derived channel id. MeshConfig
 * carries an array of kMaxChannels of these as the CANONICAL channel source;
 * the legacy primary_/secondary_ scalar fields are kept as compatibility
 * mirrors of slots 0/1.
 *
 * Deliberately dependency-light (only <cstddef>/<cstdint>) so it host-compiles
 * standalone for the behavioral tests AND links into the firmware unchanged.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{

// Maximum number of Meshtastic channels, matching real Meshtastic (0..7).
constexpr std::size_t kMaxChannels = 8;

/**
 * @brief A single Meshtastic channel slot.
 *
 * @note key_len follows the Meshtastic PSK convention used across this codebase:
 *       0 = no PSK, 1 = short-PSK index into the default PSK, 16/32 = verbatim.
 *       When stored in config from a parsed key, key_len is 0/16/32.
 */
struct ChannelRecord
{
    bool enabled;
    char name[32];
    uint8_t key[32];
    uint8_t key_len;
    uint32_t channel_id;
};

} // namespace chat
