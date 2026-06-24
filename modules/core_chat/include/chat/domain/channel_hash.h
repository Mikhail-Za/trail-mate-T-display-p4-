/**
 * @file channel_hash.h
 * @brief Pure, host-buildable Meshtastic channel-hash helper (single source).
 *
 * This is the ONE implementation of the Meshtastic channel-hash math. It owns
 * the tiny pure pieces previously inlined in mt_protocol_helpers.cpp:
 *   - xorHash (byte XOR fold)
 *   - the 16-byte default PSK
 *   - the short-PSK expansion rule
 *   - computeChannelHash == xorHash(name) ^ xorHash(expandedPsk)
 *
 * mt_protocol_helpers.cpp's computeChannelHash/expandShortPsk DELEGATE here so
 * there is no second, divergent copy: the host-tested hash IS the on-air hash,
 * which is what guarantees channel 0 and channels 2..7 interop with real
 * Meshtastic. The algorithm itself is unchanged.
 *
 * HOST-BUILDABILITY: this header pulls ONLY <cstddef>/<cstdint> and
 * channel_record.h. It must NOT include mt_protocol_helpers.h or any of the
 * generated meshtastic protobuf headers / pb.h (those are not host-buildable).
 * See the FLAG in test_mc_wire.cpp.
 */

#pragma once

#include "chat/domain/channel_record.h"

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace meshtastic
{

// The Meshtastic default PSK (channel "default"/LongFast). Single definition,
// shared by the adapter and the host test path.
constexpr std::size_t kDefaultPskLen = 16;
extern const uint8_t kDefaultPskBytes[kDefaultPskLen];

// XOR-fold a byte buffer (Meshtastic channel-hash primitive).
uint8_t channelXorHash(const uint8_t* data, std::size_t len);

// Expand a PSK following the Meshtastic short-PSK rule, writing up to 32 bytes
// into out and the produced length into out_len:
//   key_len == 0        -> no PSK (out_len = 0)
//   key_len == 1        -> 16-byte default PSK with (key[0]-1) added to the last
//                          byte; key[0]==1 yields the unmodified default PSK
//   key_len == 16 or 32 -> the key bytes verbatim
//   (any other explicit length is treated as verbatim of that length)
void expandChannelPsk(const uint8_t* key, uint8_t key_len, uint8_t* out, std::size_t* out_len);

// Compute the Meshtastic channel hash from a name + expanded PSK bytes. This is
// identical to the on-air computeChannelHash(name, expandedPsk, len).
uint8_t computeChannelHashBytes(const char* name, const uint8_t* expanded_psk, std::size_t psk_len);

// Compute the channel hash for a ChannelRecord (name + its PSK, expanded per the
// short-PSK rule). This is the byte that goes on the air for that channel.
uint8_t channelHashFromRecord(const chat::ChannelRecord& rec);

} // namespace meshtastic
} // namespace chat
