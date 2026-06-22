/**
 * @file mesh_protocol_select.h
 * @brief Portable mapping between the persisted mesh-protocol setting value and
 *        the chat::MeshProtocol enum.
 *
 * This is the single source of truth shared by BOTH the boot-time read and the
 * Settings UI persist so the persist->read round-trip always agrees on the NVS
 * namespace, key, and the integer value encoding.
 *
 * The on-device firmware supports exactly two protocols at runtime: Meshtastic
 * and MeshCore. RNode and LXMF are NOT supported targets, so any unsupported or
 * invalid stored value decodes to MeshCore (the safe default boot). Encoding is
 * therefore two-valued: Meshtastic -> 1, everything else -> 2 (MeshCore).
 *
 * Header-only / inline, portable C++17. Depends only on chat/domain/chat_types.h;
 * no IDF / Arduino / LVGL dependencies, so it compiles on the host test as well
 * as the target.
 */

#pragma once

#include "chat/domain/chat_types.h"

namespace chat
{

/// NVS namespace holding the persisted protocol selection. Shared with the paid
/// MeshOS license namespace -- only a single key is added here, never erased.
constexpr const char* kMeshProtocolNs = "settings";

/// NVS key (int) holding the persisted protocol selection.
constexpr const char* kMeshProtocolKey = "mesh_protocol";

/**
 * @brief Decode a persisted setting value into a runtime MeshProtocol.
 *
 * 1 -> Meshtastic, 2 -> MeshCore. Every other value (0, RNode=3, LXMF=4, and any
 * out-of-range/invalid integer) resolves to MeshCore, which is the default boot
 * protocol on this target.
 */
inline MeshProtocol meshProtocolFromSettingValue(int v)
{
    if (v == 1)
    {
        return MeshProtocol::Meshtastic;
    }
    return MeshProtocol::MeshCore;
}

/**
 * @brief Encode a runtime MeshProtocol into the value to persist.
 *
 * Meshtastic -> 1, anything else (MeshCore and any non-supported protocol) -> 2,
 * so the stored value always round-trips back through meshProtocolFromSettingValue
 * to one of the two supported protocols.
 */
inline int meshProtocolToSettingValue(MeshProtocol p)
{
    return (p == MeshProtocol::Meshtastic) ? 1 : 2;
}

} // namespace chat
