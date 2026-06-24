/**
 * @file chat_broadcast_targets_core.h
 * @brief Pure broadcast-target enumeration over a per-channel config array.
 *
 * specFor() maps a slot index onto a TargetSpec by reading the live channel
 * records directly: enabled and chat_supported both track channels[index].enabled
 * and the channel id is the slot index, for ALL slots (no idx<=1 clamp -- that
 * clamp WAS the 2-channel bug). It has NO app-facade dependency so it
 * host-compiles standalone; chat_broadcast_targets.h::spec() delegates to it,
 * passing the live MeshConfig.channels array.
 *
 * Header-only and dependency-light (chat_types.h for MeshProtocol/ChannelId,
 * channel_record.h for ChannelRecord). No LVGL, no app facade.
 */

#pragma once

#include "chat/domain/channel_record.h"
#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>

namespace chat::ui::broadcast_targets
{

struct TargetSpec
{
    chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    uint8_t channel_index = 0;
    bool enabled = false;
    bool chat_supported = false;
};

// Fill *out for the channel slot at `index` in [0, n). Returns false if out is
// null or index is out of [0, n). protocol is left at its default (Meshtastic);
// the caller sets the live protocol when it needs a non-Meshtastic value.
inline bool specFor(const chat::ChannelRecord* channels, size_t n, int index, TargetSpec* out)
{
    if (!out || !channels || index < 0 || static_cast<size_t>(index) >= n)
    {
        return false;
    }
    const chat::ChannelRecord& rec = channels[static_cast<size_t>(index)];
    out->protocol = chat::MeshProtocol::Meshtastic;
    out->channel = static_cast<chat::ChannelId>(index);
    out->channel_index = static_cast<uint8_t>(index);
    out->enabled = rec.enabled;
    out->chat_supported = rec.enabled; // every enabled slot is chat-capable (0..7)
    return true;
}

} // namespace chat::ui::broadcast_targets
