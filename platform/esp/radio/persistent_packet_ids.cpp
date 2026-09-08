#include "platform/esp/radio/persistent_packet_ids.h"

#include "chat/domain/channel_hash.h"
#include "chat/domain/persistent_packet_id_allocator.h"

#include "mbedtls/sha256.h"
#include "nvs.h"

#include <array>
#include <cstring>
#include <mutex>
#include <optional>

namespace platform::esp::radio {
namespace {

constexpr char kNamespace[] = "tm_pkt_ids";
constexpr char kStateKey[] = "state_v1";
constexpr size_t kBlobSize = 273;
constexpr size_t kNodeOffset = 4;
constexpr size_t kHighWaterOffset = 8;
constexpr size_t kCountOffset = 16;
constexpr size_t kFingerprintsOffset = 17;
constexpr size_t kFingerprintSize = 32;
constexpr uint64_t kEnd = 1ULL << 32;

std::mutex mutex;
bool initialized = false;
bool failed = false;
uint32_t owner_node = 0;
std::array<uint8_t, kBlobSize> state{};
std::optional<chat::PersistentPacketIdAllocator> allocator;

uint32_t readLe32(const uint8_t* bytes)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(bytes[i]) << (8 * i);
    return value;
}

uint64_t readLe64(const uint8_t* bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    return value;
}

void writeLe32(uint8_t* bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        bytes[i] = static_cast<uint8_t>(value >> (8 * i));
}

void writeLe64(uint8_t* bytes, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i)
        bytes[i] = static_cast<uint8_t>(value >> (8 * i));
}

bool isPublicPsk(const uint8_t* key, size_t len)
{
    return len == chat::meshtastic::kDefaultPskLen &&
           std::memcmp(key, chat::meshtastic::kDefaultPskBytes,
                       chat::meshtastic::kDefaultPskLen - 1) == 0;
}

bool validateState(const std::array<uint8_t, kBlobSize>& blob, uint32_t node)
{
    if (std::memcmp(blob.data(), "TPI1", 4) != 0 || readLe32(blob.data() + kNodeOffset) != node)
        return false;
    const uint64_t high = readLe64(blob.data() + kHighWaterOffset);
    const uint8_t count = blob[kCountOffset];
    if (high < 1 || high > kEnd || count > chat::kMaxChannels)
        return false;
    const size_t used_end = kFingerprintsOffset + count * kFingerprintSize;
    for (size_t i = used_end; i < blob.size(); ++i)
        if (blob[i] != 0)
            return false;
    return true;
}

bool captureLegacyKeys(const chat::MeshConfig& config, std::array<uint8_t, kBlobSize>& blob)
{
    uint8_t count = 0;
    for (size_t i = 0; i < chat::kMaxChannels; ++i)
    {
        const chat::ChannelRecord& channel = config.channels[i];
        const uint8_t normalized = chat::normalizeMeshtasticChannelKeyLen(
            channel.key, sizeof(channel.key), channel.key_len);
        if (normalized == 0)
            continue;

        uint8_t expanded[chat::kMeshtasticChannelKeyMaxLen]{};
        size_t expanded_len = 0;
        chat::meshtastic::expandChannelPsk(channel.key, normalized, expanded, &expanded_len);
        if (expanded_len == 0 || isPublicPsk(expanded, expanded_len))
            continue;
        if (mbedtls_sha256(expanded, expanded_len,
                           blob.data() + kFingerprintsOffset + count * kFingerprintSize, 0) != 0)
            return false;
        ++count;
    }
    blob[kCountOffset] = count;
    return true;
}

bool persist(const std::array<uint8_t, kBlobSize>& blob)
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;
    const bool ok = nvs_set_blob(handle, kStateKey, blob.data(), blob.size()) == ESP_OK &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

} // namespace

bool initializePacketIds(uint32_t node, const chat::MeshConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (failed)
        return false;
    if (initialized)
        return node == owner_node;
    if (node == 0)
    {
        failed = true;
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
    {
        failed = true;
        return false;
    }

    std::array<uint8_t, kBlobSize> loaded{};
    size_t size = 0;
    esp_err_t result = nvs_get_blob(handle, kStateKey, nullptr, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND)
    {
        std::memcpy(loaded.data(), "TPI1", 4);
        writeLe32(loaded.data() + kNodeOffset, node);
        writeLe64(loaded.data() + kHighWaterOffset, 1);
        if (!captureLegacyKeys(config, loaded) ||
            nvs_set_blob(handle, kStateKey, loaded.data(), loaded.size()) != ESP_OK ||
            nvs_commit(handle) != ESP_OK)
        {
            nvs_close(handle);
            failed = true;
            return false;
        }
    }
    else
    {
        if (result != ESP_OK || size != loaded.size())
        {
            nvs_close(handle);
            failed = true;
            return false;
        }
        result = nvs_get_blob(handle, kStateKey, loaded.data(), &size);
        if (result != ESP_OK || size != loaded.size() || !validateState(loaded, node))
        {
            nvs_close(handle);
            failed = true;
            return false;
        }
    }
    nvs_close(handle);

    state = loaded;
    owner_node = node;
    allocator.emplace(readLe64(state.data() + kHighWaterOffset));
    initialized = true;
    return true;
}

bool allocatePacketId(uint32_t node, const uint8_t* psk, size_t psk_len, uint32_t& out)
{
    std::lock_guard<std::mutex> lock(mutex);
    out = 0;
    if (failed || !initialized || node == 0 || node != owner_node)
        return false;
    if (psk_len != 0 && (psk == nullptr || (psk_len != 16 && psk_len != 32)))
        return false;

    if (psk_len != 0)
    {
        uint8_t fingerprint[kFingerprintSize];
        if (mbedtls_sha256(psk, psk_len, fingerprint, 0) != 0)
        {
            failed = true;
            return false;
        }
        for (uint8_t i = 0; i < state[kCountOffset]; ++i)
            if (std::memcmp(fingerprint,
                            state.data() + kFingerprintsOffset + i * kFingerprintSize,
                            kFingerprintSize) == 0)
                return false;
    }

    const bool ok = allocator->allocate(0, [](uint64_t new_high) {
        std::array<uint8_t, kBlobSize> updated = state;
        writeLe64(updated.data() + kHighWaterOffset, new_high);
        if (!persist(updated))
        {
            failed = true;
            return false;
        }
        state = updated;
        return true;
    }, out);
    return ok;
}

} // namespace platform::esp::radio
