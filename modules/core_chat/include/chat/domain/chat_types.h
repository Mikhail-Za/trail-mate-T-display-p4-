/**
 * @file chat_types.h
 * @brief Core type definitions for chat functionality
 */

#pragma once

#include "chat/domain/channel_record.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace chat
{

constexpr std::size_t kMeshCoreChannelKeyLen = 16;
constexpr std::size_t kMeshtasticChannelKeyDefaultLen = 16;
constexpr std::size_t kMeshtasticChannelKeyMaxLen = 32;

inline bool isAllZeroKeyBytes(const uint8_t* key, std::size_t len)
{
    if (!key || len == 0)
    {
        return true;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (key[index] != 0)
        {
            return false;
        }
    }
    return true;
}

inline uint8_t normalizeMeshtasticChannelKeyLen(const uint8_t* key,
                                                std::size_t capacity,
                                                uint8_t stored_len)
{
    if (!key || capacity < kMeshtasticChannelKeyDefaultLen)
    {
        return 0;
    }

    if ((stored_len == kMeshtasticChannelKeyDefaultLen ||
         stored_len == kMeshtasticChannelKeyMaxLen) &&
        stored_len <= capacity)
    {
        return isAllZeroKeyBytes(key, stored_len) ? 0 : stored_len;
    }

    const std::size_t inspect_len =
        capacity >= kMeshtasticChannelKeyMaxLen ? kMeshtasticChannelKeyMaxLen
                                                : kMeshtasticChannelKeyDefaultLen;
    if (isAllZeroKeyBytes(key, inspect_len))
    {
        return 0;
    }

    if (capacity >= kMeshtasticChannelKeyMaxLen &&
        !isAllZeroKeyBytes(key + kMeshtasticChannelKeyDefaultLen,
                           kMeshtasticChannelKeyDefaultLen))
    {
        return kMeshtasticChannelKeyMaxLen;
    }
    return kMeshtasticChannelKeyDefaultLen;
}

// Multi-channel generalization: kMaxChannels = 8 canonical channel slots
// (defined in channel_record.h). MeshConfig.channels[kMaxChannels] below is the
// source of truth; the legacy primary_/secondary_ scalars are compat mirrors of
// slots 0/1.
static_assert(kMaxChannels == 8, "kMaxChannels must be 8");

/**
 * @brief Channel identifier
 * Primary channel (0) is the default public channel. Channels are now
 * generalized to kMaxChannels slots (0..7); PRIMARY/SECONDARY remain as
 * source-compat aliases for slots 0 and 1.
 */
enum class ChannelId : uint8_t
{
    PRIMARY = 0,   // Public channel (default broadcast), slot 0
    SECONDARY = 1, // Squad channel (encrypted), slot 1
    MAX_CHANNELS = kMaxChannels
};

/**
 * @brief Node identifier (32-bit, compatible with Meshtastic)
 */
using NodeId = uint32_t;

/**
 * @brief Message identifier (32-bit)
 */
using MessageId = uint32_t;

/**
 * @brief Mesh protocol selection
 */
enum class MeshProtocol : uint8_t
{
    Meshtastic = 1,
    MeshCore = 2,
    RNode = 3,
    LXMF = 4
};

/**
 * @brief Active discovery actions (protocol-specific)
 */
enum class MeshDiscoveryAction : uint8_t
{
    ScanLocal = 1,
    SendIdLocal = 2,
    SendIdBroadcast = 3
};

enum class MeshOperationFailure : uint8_t
{
    None = 0,
    InvalidInput,
    Unsupported,
    NotReady,
    TxDisabled,
    RadioOffline,
    DutyCycleLimited,
    LocalIdentityMissing,
    PeerKeyMissing,
    ChannelKeyMissing,
    EncodeFailed,
    CryptoFailed,
    RadioTxFailed,
    Busy,
    Unknown,
};

struct MeshActionResult
{
    bool ok = false;
    MeshOperationFailure failure = MeshOperationFailure::None;
    int detail = 0;

    static MeshActionResult success()
    {
        MeshActionResult result;
        result.ok = true;
        result.failure = MeshOperationFailure::None;
        return result;
    }

    static MeshActionResult fail(MeshOperationFailure failure, int detail = 0)
    {
        MeshActionResult result;
        result.ok = false;
        result.failure = failure;
        result.detail = detail;
        return result;
    }
};

struct MeshSendResult
{
    bool ok = false;
    MessageId msg_id = 0;
    MeshOperationFailure failure = MeshOperationFailure::None;
    int detail = 0;

    static MeshSendResult success(MessageId id)
    {
        MeshSendResult result;
        result.ok = true;
        result.msg_id = id;
        result.failure = MeshOperationFailure::None;
        return result;
    }

    static MeshSendResult fail(MeshOperationFailure failure, MessageId id = 0, int detail = 0)
    {
        MeshSendResult result;
        result.ok = false;
        result.msg_id = id;
        result.failure = failure;
        result.detail = detail;
        return result;
    }
};

/**
 * @brief RX time source for received packets
 */
enum class RxTimeSource : uint8_t
{
    Unknown = 0,
    Uptime = 1,
    DeviceUtc = 2,
    GpsUtc = 3
};

/**
 * @brief RX origin classification
 */
enum class RxOrigin : uint8_t
{
    Unknown = 0,
    Mesh = 1,
    External = 2
};

/**
 * @brief RX metadata for mesh packets
 */
struct RxMeta
{
    uint32_t rx_timestamp_s;
    uint32_t rx_timestamp_ms;
    RxTimeSource time_source;
    RxOrigin origin;
    bool direct;
    bool from_is;
    uint8_t hop_count;
    uint8_t hop_limit;
    uint8_t channel_hash;
    uint8_t wire_flags;
    int16_t rssi_dbm_x10;
    int16_t snr_db_x10;
    uint32_t freq_hz;
    uint32_t bw_hz;
    uint8_t sf;
    uint8_t cr;
    uint32_t next_hop;
    uint32_t relay_node;

    RxMeta()
        : rx_timestamp_s(0),
          rx_timestamp_ms(0),
          time_source(RxTimeSource::Unknown),
          origin(RxOrigin::Unknown),
          direct(false),
          from_is(false),
          hop_count(0xFF),
          hop_limit(0xFF),
          channel_hash(0xFF),
          wire_flags(0xFF),
          rssi_dbm_x10(std::numeric_limits<int16_t>::min()),
          snr_db_x10(std::numeric_limits<int16_t>::min()),
          freq_hz(0),
          bw_hz(0),
          sf(0),
          cr(0),
          next_hop(0),
          relay_node(0)
    {
    }
};

/**
 * @brief Conversation identifier (channel + peer)
 * peer = 0 means channel-wide/broadcast
 */
struct ConversationId
{
    MeshProtocol protocol;
    ChannelId channel;
    NodeId peer; // 0 for broadcast/channel thread

    ConversationId(ChannelId ch = ChannelId::PRIMARY,
                   NodeId p = 0,
                   MeshProtocol proto = MeshProtocol::Meshtastic)
        : protocol(proto), channel(ch), peer(p) {}

    bool operator<(const ConversationId& other) const
    {
        if (protocol != other.protocol)
        {
            return static_cast<uint8_t>(protocol) < static_cast<uint8_t>(other.protocol);
        }
        if (channel != other.channel)
        {
            return static_cast<uint8_t>(channel) < static_cast<uint8_t>(other.channel);
        }
        return peer < other.peer;
    }
    bool operator==(const ConversationId& other) const
    {
        return protocol == other.protocol &&
               channel == other.channel &&
               peer == other.peer;
    }
};

/**
 * @brief Message status
 */
enum class MessageStatus
{
    Incoming, // Received message
    Queued,   // Queued for sending
    Sent,     // Successfully sent
    Failed    // Failed to send
};

/**
 * @brief Chat message structure
 */
struct ChatMessage
{
    MeshProtocol protocol;
    ChannelId channel;
    NodeId from; // 0 for local messages
    NodeId peer; // conversation peer (0 for broadcast)
    MessageId msg_id;
    uint32_t timestamp; // Unix timestamp (seconds)
    std::string text;
    uint8_t team_location_icon; // Team location semantic icon id (0 = none)
    bool has_geo;
    int32_t geo_lat_e7;
    int32_t geo_lon_e7;
    MessageStatus status;

    ChatMessage() : protocol(MeshProtocol::Meshtastic),
                    channel(ChannelId::PRIMARY), from(0), peer(0), msg_id(0),
                    timestamp(0),
                    team_location_icon(0),
                    has_geo(false),
                    geo_lat_e7(0),
                    geo_lon_e7(0),
                    status(MessageStatus::Incoming) {}
};

/**
 * @brief Conversation metadata for UI
 */
struct ConversationMeta
{
    ConversationId id;
    std::string name;
    std::string preview;
    uint32_t last_timestamp;
    int unread;

    ConversationMeta() : id(), last_timestamp(0), unread(0) {}
};

/**
 * @brief Incoming text message from mesh
 */
struct MeshIncomingText
{
    ChannelId channel;
    NodeId from;
    NodeId to;
    MessageId msg_id;
    uint32_t timestamp;
    std::string text;
    uint8_t hop_limit; // Remaining hops
    bool encrypted;    // Whether message was encrypted
    RxMeta rx_meta;
};

/**
 * @brief Incoming non-text mesh payload
 */
struct MeshIncomingData
{
    uint32_t portnum;
    NodeId from;
    NodeId to;
    MessageId packet_id;
    uint32_t request_id;
    ChannelId channel;
    uint8_t channel_hash;
    uint8_t hop_limit;
    bool want_response;
    std::vector<uint8_t> payload;
    RxMeta rx_meta;

    MeshIncomingData()
        : portnum(0), from(0), to(0), packet_id(0), request_id(0),
          channel(ChannelId::PRIMARY), channel_hash(0), hop_limit(0xFF),
          want_response(false) {}
};

/**
 * @brief Mesh configuration
 */
struct MeshConfig
{
    // Meshtastic radio configuration
    uint8_t region;           // LoRa region (0=US, 1=EU, etc.)
    bool use_preset;          // true: use modem_preset, false: use manual params
    uint8_t modem_preset;     // Modem preset index
    float bandwidth_khz;      // Manual bandwidth when use_preset=false
    uint8_t spread_factor;    // Manual SF when use_preset=false
    uint8_t coding_rate;      // Manual CR denominator (5..8) when use_preset=false
    int8_t tx_power;          // TX power in dBm
    uint8_t hop_limit;        // Maximum hop limit (1-7)
    bool tx_enabled;          // Disable TX when false
    bool override_duty_cycle; // Ignore duty cycle based throttling when true
    uint16_t channel_num;     // 0 = auto hash, otherwise 1..N channel slot
    float frequency_offset_mhz;
    float override_frequency_mhz; // 0 = disabled
    bool enable_relay;            // Reserved relay switch (not currently routed)
    bool ignore_mqtt;             // Ignore incoming packets that arrived via MQTT
    bool config_ok_to_mqtt;       // Set ok_to_mqtt bit on outgoing packets

    // Canonical per-slot Meshtastic channel array (0..kMaxChannels-1). This is
    // the source of truth for multi-channel TX/RX. The primary_/secondary_
    // scalar fields below are kept in sync as COMPATIBILITY MIRRORS of
    // channels[0]/[1] for the sibling targets (BLE phone core, arduino config
    // store, linux app/tests) that are not compiled into the P4 build and still
    // read the scalars.
    ChannelRecord channels[kMaxChannels];

    // Meshtastic channel identity and encryption keys.
    // COMPAT MIRRORS of channels[0]/[1] -- keep in sync via syncChannelMirrors().
    char primary_channel_name[32];
    char secondary_channel_name[32];
    uint32_t primary_channel_id;
    uint32_t secondary_channel_id;
    uint8_t primary_key[kMeshtasticChannelKeyMaxLen];   // Meshtastic PSK storage; MeshCore uses first 16 bytes.
    uint8_t primary_key_len;                            // Meshtastic PSK length: 0, 16, or 32.
    uint8_t secondary_key[kMeshtasticChannelKeyMaxLen]; // Meshtastic PSK storage; MeshCore uses first 16 bytes.
    uint8_t secondary_key_len;                          // Meshtastic PSK length: 0, 16, or 32.

    // MeshCore radio/channel tuning
    uint8_t meshcore_region_preset; // 0=Custom, >0 preset id
    float meshcore_freq_mhz;
    float meshcore_bw_khz;
    uint8_t meshcore_sf;
    uint8_t meshcore_cr;
    bool meshcore_client_repeat;
    float meshcore_rx_delay_base;
    float meshcore_airtime_factor;
    uint8_t meshcore_flood_max;
    bool meshcore_multi_acks;
    uint8_t meshcore_channel_slot;
    char meshcore_channel_name[32];

    MeshConfig()
        : region(0),
          use_preset(true),
          modem_preset(0),
          bandwidth_khz(250.0f),
          spread_factor(11),
          coding_rate(5),
          tx_power(14),
          hop_limit(2),
          tx_enabled(true),
          override_duty_cycle(false),
          channel_num(0),
          frequency_offset_mhz(0.0f),
          override_frequency_mhz(0.0f),
          enable_relay(true),
          ignore_mqtt(false),
          config_ok_to_mqtt(false),
          primary_channel_id(0),
          secondary_channel_id(0),
          primary_key_len(0),
          secondary_key_len(0),
          meshcore_region_preset(0),
          meshcore_freq_mhz(915.0f),
          meshcore_bw_khz(125.0f),
          meshcore_sf(9),
          meshcore_cr(5),
          meshcore_client_repeat(false),
          meshcore_rx_delay_base(0.0f),
          meshcore_airtime_factor(1.0f),
          meshcore_flood_max(16),
          meshcore_multi_acks(false),
          meshcore_channel_slot(0)
    {
        for (std::size_t i = 0; i < kMaxChannels; ++i)
        {
            channels[i] = ChannelRecord{};
        }
        // Slot 0 defaults to the LongFast public channel (enabled); the rest are
        // disabled empty slots.
        channels[0].enabled = true;
        strncpy(channels[0].name, "LongFast", sizeof(channels[0].name) - 1);
        channels[0].name[sizeof(channels[0].name) - 1] = '\0';
        strncpy(channels[1].name, "Secondary", sizeof(channels[1].name) - 1);
        channels[1].name[sizeof(channels[1].name) - 1] = '\0';

        strncpy(primary_channel_name, "LongFast", sizeof(primary_channel_name) - 1);
        primary_channel_name[sizeof(primary_channel_name) - 1] = '\0';
        strncpy(secondary_channel_name, "Secondary", sizeof(secondary_channel_name) - 1);
        secondary_channel_name[sizeof(secondary_channel_name) - 1] = '\0';
        memset(primary_key, 0, sizeof(primary_key));
        memset(secondary_key, 0, sizeof(secondary_key));
        meshcore_channel_name[0] = '\0';
    }

    // Push channels[0]/[1] out to the legacy primary_/secondary_ scalar mirrors.
    // Call after any edit to channels[0]/[1] so the sibling targets that still
    // read the scalars (BLE phone core, arduino config store, linux app) stay
    // consistent. Channels 2..7 have no scalar mirror by design.
    void syncChannelMirrors()
    {
        strncpy(primary_channel_name, channels[0].name, sizeof(primary_channel_name) - 1);
        primary_channel_name[sizeof(primary_channel_name) - 1] = '\0';
        strncpy(secondary_channel_name, channels[1].name, sizeof(secondary_channel_name) - 1);
        secondary_channel_name[sizeof(secondary_channel_name) - 1] = '\0';
        primary_channel_id = channels[0].channel_id;
        secondary_channel_id = channels[1].channel_id;

        memset(primary_key, 0, sizeof(primary_key));
        primary_key_len = channels[0].key_len;
        if (primary_key_len > sizeof(primary_key))
        {
            primary_key_len = sizeof(primary_key);
        }
        memcpy(primary_key, channels[0].key, primary_key_len);

        memset(secondary_key, 0, sizeof(secondary_key));
        secondary_key_len = channels[1].key_len;
        if (secondary_key_len > sizeof(secondary_key))
        {
            secondary_key_len = sizeof(secondary_key);
        }
        memcpy(secondary_key, channels[1].key, secondary_key_len);
    }

    // Migrate the legacy primary_/secondary_ scalars into channels[0]/[1]. Used
    // on boot/load so a config that only has the old scalar fields populates the
    // canonical array. enabled flags are supplied by the caller (they live in
    // app_config, not MeshConfig). Channels 2..7 are left untouched.
    void migrateLegacyIntoSlots(bool primary_enabled, bool secondary_enabled)
    {
        channels[0].enabled = primary_enabled;
        strncpy(channels[0].name, primary_channel_name, sizeof(channels[0].name) - 1);
        channels[0].name[sizeof(channels[0].name) - 1] = '\0';
        channels[0].channel_id = primary_channel_id;
        memset(channels[0].key, 0, sizeof(channels[0].key));
        channels[0].key_len = (primary_key_len > sizeof(channels[0].key))
                                  ? static_cast<uint8_t>(sizeof(channels[0].key))
                                  : primary_key_len;
        memcpy(channels[0].key, primary_key, channels[0].key_len);

        channels[1].enabled = secondary_enabled;
        strncpy(channels[1].name, secondary_channel_name, sizeof(channels[1].name) - 1);
        channels[1].name[sizeof(channels[1].name) - 1] = '\0';
        channels[1].channel_id = secondary_channel_id;
        memset(channels[1].key, 0, sizeof(channels[1].key));
        channels[1].key_len = (secondary_key_len > sizeof(channels[1].key))
                                  ? static_cast<uint8_t>(sizeof(channels[1].key))
                                  : secondary_key_len;
        memcpy(channels[1].key, secondary_key, channels[1].key_len);
    }
};

} // namespace chat
