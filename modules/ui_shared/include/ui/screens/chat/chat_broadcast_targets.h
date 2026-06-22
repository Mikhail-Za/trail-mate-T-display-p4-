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
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "ui/localization.h"
#include "ui/screens/chat/chat_protocol_support.h"

#include <cstdint>
#include <string>

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
        if (index >= 8)
        {
            return false;
        }

        const auto& cfg = app::configFacade().getConfig();
        out->protocol = chat::MeshProtocol::Meshtastic;
        out->channel_index = static_cast<uint8_t>(index);
        out->channel = (index == 1) ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY;
        out->enabled = (index == 0) ? cfg.primary_enabled : ((index == 1) ? cfg.secondary_enabled : false);
        out->chat_supported = out->enabled && (index <= 1);
        return true;
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
        return std::string("[MT] ") +
               chat::meshtastic::channelName(app::configFacade().getConfig().meshtastic_config,
                                             target.channel);
    }
    if (target.protocol == chat::MeshProtocol::RNode)
    {
        return std::string("[RN] ") + ::ui::i18n::tr("Modem Bridge");
    }
    return std::string("[MC] ") +
           ::ui::i18n::tr((target.channel == chat::ChannelId::SECONDARY) ? "Secondary" : "Primary");
}

} // namespace chat::ui::broadcast_targets
