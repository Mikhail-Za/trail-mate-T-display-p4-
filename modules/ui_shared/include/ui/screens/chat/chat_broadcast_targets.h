/**
 * @file chat_broadcast_targets.h
 * @brief Shared enumeration of broadcast (channel) chat targets.
 *
 * Single source of truth for "which broadcast channels can the user chat on".
 * Reused by the Contacts Broadcast list and by the Chat screen new-message
 * picker so both surfaces grow automatically when multi-channel support lands.
 * Header-only (inline) so no new translation unit is required.
 */

#pragma once

#include "app/app_facade_access.h"
#include "chat/domain/channel_record.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "ui/localization.h"
#include "ui/screens/chat/chat_broadcast_targets_core.h"
#include "ui/screens/chat/chat_protocol_support.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat::ui::broadcast_targets
{

// TargetSpec lives in chat_broadcast_targets_core.h (the pure, host-tested core).

inline size_t count()
{
    switch (chat::ui::support::active_mesh_protocol())
    {
    case chat::MeshProtocol::Meshtastic:
        return 8U;
    case chat::MeshProtocol::MeshCore:
        return 2U;
    case chat::MeshProtocol::RNode:
        return 1U;
    default:
        return 0U;
    }
}

inline bool spec(int index, TargetSpec* out)
{
    if (!out || index < 0)
    {
        return false;
    }

    if (chat::ui::support::active_mesh_protocol() == chat::MeshProtocol::Meshtastic)
    {
        // Delegate to the pure, host-tested core reading the live per-channel
        // config. specFor preserves the slot index and drives both enabled and
        // chat_supported off the real per-slot channel_enabled flag
        // (channels[index].enabled) for all 8 slots -- the old chat_supported
        // clamp to the first two indices was the 2-channel bug and is gone.
        const auto& cfg = app::configFacade().getConfig();
        return specFor(cfg.meshtastic_config.channels,
                       static_cast<std::size_t>(chat::kMaxChannels),
                       index,
                       out);
    }

    if (chat::ui::support::active_mesh_protocol() == chat::MeshProtocol::RNode)
    {
        if (index != 0)
        {
            return false;
        }
        out->protocol = chat::MeshProtocol::RNode;
        out->channel = chat::ChannelId::PRIMARY;
        out->channel_index = 0;
        out->enabled = true;
        out->chat_supported = false;
        return true;
    }

    switch (index)
    {
    case 0:
        out->protocol = chat::MeshProtocol::MeshCore;
        out->channel = chat::ChannelId::PRIMARY;
        out->channel_index = 0;
        out->enabled = true;
        out->chat_supported = true;
        return true;
    case 1:
        out->protocol = chat::MeshProtocol::MeshCore;
        out->channel = chat::ChannelId::SECONDARY;
        out->channel_index = 1;
        out->enabled = true;
        out->chat_supported = true;
        return true;
    default:
        return false;
    }
}

inline std::string label(const TargetSpec& target)
{
    if (target.protocol == chat::MeshProtocol::Meshtastic)
    {
        // Use the per-slot channel name (slot index preserved in channel_index)
        // so channels 2..7 show their own names, not just Primary/Secondary.
        return std::string("[MT] ") +
               chat::meshtastic::channelName(app::configFacade().getConfig().meshtastic_config,
                                             static_cast<std::size_t>(target.channel_index));
    }
    if (target.protocol == chat::MeshProtocol::RNode)
    {
        return std::string("[RN] ") + ::ui::i18n::tr("Modem Bridge");
    }
    return std::string("[MC] ") +
           ::ui::i18n::tr((target.channel == chat::ChannelId::SECONDARY) ? "Secondary" : "Primary");
}

} // namespace chat::ui::broadcast_targets
