/**
 * @file channel_persist.cpp
 * @brief Pure codec for the channel-config blob. See channel_persist.h.
 */

#include "chat/domain/channel_persist.h"

#include <cstring>

namespace chat
{
namespace channel_persist
{
namespace
{

constexpr uint8_t kMagic0 = 'T';
constexpr uint8_t kMagic1 = 'M';
constexpr uint8_t kMagic2 = 'H';
constexpr uint8_t kVersion = 0x01;
constexpr size_t kHeaderSize = 5; // magic(3) + version(1) + count(1)

// On-blob record size: enabled(1) + name(32) + key(32) + key_len(1) + id(4).
constexpr size_t kNameBytes = 32;
constexpr size_t kKeyBytes = 32;
constexpr size_t kRecordSize = 1 + kNameBytes + kKeyBytes + 1 + 4;

void put_u32_le(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

uint32_t get_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

std::vector<uint8_t> encode(const ChannelRecord* recs, size_t n)
{
    std::vector<uint8_t> out;
    // Count is stored in a single byte; clamp defensively (kMaxChannels == 8).
    if (n > 0xFFu)
    {
        n = 0xFFu;
    }
    out.reserve(kHeaderSize + n * kRecordSize);
    out.push_back(kMagic0);
    out.push_back(kMagic1);
    out.push_back(kMagic2);
    out.push_back(kVersion);
    out.push_back(static_cast<uint8_t>(n));

    for (size_t i = 0; i < n; ++i)
    {
        const ChannelRecord& r = recs ? recs[i] : ChannelRecord{};
        out.push_back(r.enabled ? 1u : 0u);
        // name[32], byte-for-byte (incl. trailing zero pad).
        out.insert(out.end(), r.name, r.name + kNameBytes);
        // key[32], byte-for-byte.
        out.insert(out.end(), r.key, r.key + kKeyBytes);
        out.push_back(r.key_len);
        put_u32_le(out, r.channel_id);
    }
    return out;
}

size_t decode(const uint8_t* data, size_t len, ChannelRecord* out, size_t max)
{
    if (!data || len < kHeaderSize)
    {
        return 0;
    }
    if (data[0] != kMagic0 || data[1] != kMagic1 || data[2] != kMagic2 || data[3] != kVersion)
    {
        return 0;
    }
    const size_t count = data[4];
    // The declared count must fully fit the buffer (reject truncation).
    if (len < kHeaderSize + count * kRecordSize)
    {
        return 0;
    }

    const size_t to_write = (out && count <= max) ? count : 0;
    const uint8_t* p = data + kHeaderSize;
    for (size_t i = 0; i < count; ++i)
    {
        if (i < to_write)
        {
            ChannelRecord& r = out[i];
            std::memset(&r, 0, sizeof(r));
            r.enabled = (p[0] != 0);
            std::memcpy(r.name, p + 1, kNameBytes);
            std::memcpy(r.key, p + 1 + kNameBytes, kKeyBytes);
            r.key_len = p[1 + kNameBytes + kKeyBytes];
            r.channel_id = get_u32_le(p + 1 + kNameBytes + kKeyBytes + 1);
        }
        p += kRecordSize;
    }
    return count;
}

} // namespace channel_persist
} // namespace chat
