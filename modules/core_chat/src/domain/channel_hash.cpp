/**
 * @file channel_hash.cpp
 * @brief Single-source implementation of the Meshtastic channel-hash math.
 *
 * Pure (no nanopb / no meshtastic_*.pb.h). mt_protocol_helpers.cpp delegates its
 * expandShortPsk/computeChannelHash to the functions here so the algorithm is
 * defined exactly once. Do NOT change the algorithm: this must keep producing
 * the byte-identical on-air values (channel 0/1 must not regress).
 */

#include "chat/domain/channel_hash.h"

#include <cstring>

namespace chat
{
namespace meshtastic
{

const uint8_t kDefaultPskBytes[kDefaultPskLen] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                                  0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

uint8_t channelXorHash(const uint8_t* data, std::size_t len)
{
    uint8_t out = 0;
    if (data == nullptr)
    {
        return out;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        out ^= data[i];
    }
    return out;
}

void expandChannelPsk(const uint8_t* key, uint8_t key_len, uint8_t* out, std::size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!out || key_len == 0)
    {
        return;
    }
    if (key_len == 1)
    {
        // Short PSK: default PSK with (index-1) added to the final byte. index
        // here is key[0]; index 1 yields the unmodified default PSK (LongFast).
        std::memcpy(out, kDefaultPskBytes, kDefaultPskLen);
        const uint8_t index = key ? key[0] : 0;
        out[kDefaultPskLen - 1] = static_cast<uint8_t>(out[kDefaultPskLen - 1] + index - 1);
        if (out_len)
        {
            *out_len = kDefaultPskLen;
        }
        return;
    }
    // 16/32 (or any explicit length): used verbatim.
    if (key)
    {
        std::memcpy(out, key, key_len);
    }
    if (out_len)
    {
        *out_len = key_len;
    }
}

uint8_t computeChannelHashBytes(const char* name, const uint8_t* expanded_psk, std::size_t psk_len)
{
    const std::size_t name_len = name ? std::strlen(name) : 0;
    uint8_t h = channelXorHash(reinterpret_cast<const uint8_t*>(name), name_len);
    if (expanded_psk && psk_len > 0)
    {
        h ^= channelXorHash(expanded_psk, psk_len);
    }
    return h;
}

uint8_t channelHashFromRecord(const chat::ChannelRecord& rec)
{
    uint8_t expanded[32] = {};
    std::size_t expanded_len = 0;
    expandChannelPsk(rec.key, rec.key_len, expanded, &expanded_len);
    return computeChannelHashBytes(rec.name, expanded_len > 0 ? expanded : nullptr, expanded_len);
}

} // namespace meshtastic
} // namespace chat
