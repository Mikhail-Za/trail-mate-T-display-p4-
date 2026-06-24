/**
 * @file channel_persist.h
 * @brief Pure, host-buildable codec for the channel-config NVS blob.
 *
 * encode() serializes N ChannelRecord into a versioned blob; decode() validates
 * the header + declared count against the buffer length and reconstructs the
 * records. This pins the on-flash wire format independently of any storage
 * backend; the IDF NvsChannelBlobStore (idf_chat_factory.cpp) wraps this codec
 * with the tm_chans / channels_v1 NVS key.
 *
 * Blob layout (little-endian for channel_id):
 *   [0..2]  magic  'T','M','H'
 *   [3]     version 0x01
 *   [4]     record count N
 *   [5..]   N records, each kRecordSize bytes:
 *             enabled(1) | name[32] | key[32] | key_len(1) | channel_id(4 LE)
 *
 * decode() returns the record count, or 0 on empty / truncated / bad-magic /
 * bad-version / count-exceeds-buffer input. Pure (no IDF/LVGL/nanopb).
 */

#pragma once

#include "chat/domain/channel_record.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{
namespace channel_persist
{

std::vector<uint8_t> encode(const ChannelRecord* recs, size_t n);
size_t decode(const uint8_t* data, size_t len, ChannelRecord* out, size_t max);

} // namespace channel_persist
} // namespace chat
