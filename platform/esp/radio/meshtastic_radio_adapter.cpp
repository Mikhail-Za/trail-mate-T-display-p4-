#include "platform/esp/radio/meshtastic_radio_adapter.h"

#include "chat/domain/channel_hash.h"
#include "chat/domain/contact_types.h"
#include "chat/domain/nodeinfo_broadcast_scheduler.h"
#include "chat/infra/meshtastic/mt_node_payload.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/time_utils.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/mesh.pb.h"
#include "pb_encode.h"
#include "sys/event_bus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace platform::esp::radio
{
namespace
{

constexpr const char* kTag = "idf-mt";
constexpr uint8_t kDefaultPskIndex = 1;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqHeaderErr = 0x0020;
constexpr uint16_t kIrqCrcErr = 0x0040;
constexpr uint16_t kIrqTimeout = 0x0200;
constexpr uint8_t kBitfieldWantResponseMask = 0x02;
constexpr uint32_t kRadioOk = 0;
// Re-announce our own NodeInfo on this cadence so peers keep discovering us
// (stock Meshtastic announces once at boot otherwise). 0 disables the periodic.
constexpr uint32_t kNodeInfoBroadcastIntervalMs = 5U * 60U * 1000U;

uint32_t now_millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint8_t to_channel_index(chat::ChannelId channel)
{
    return static_cast<uint8_t>(channel);
}

void fill_rx_meta(chat::RxMeta& rx_meta,
                  const chat::meshtastic::PacketHeaderWire& header,
                  float rssi,
                  float snr,
                  uint32_t freq_hz,
                  uint32_t bw_hz,
                  uint8_t sf,
                  uint8_t cr)
{
    rx_meta.rx_timestamp_ms = now_millis();
    const uint32_t epoch_s = chat::now_epoch_seconds();
    if (chat::is_valid_epoch(epoch_s))
    {
        rx_meta.rx_timestamp_s = epoch_s;
        rx_meta.time_source = chat::RxTimeSource::DeviceUtc;
    }
    else
    {
        rx_meta.rx_timestamp_s = rx_meta.rx_timestamp_ms / 1000U;
        rx_meta.time_source = chat::RxTimeSource::Uptime;
    }

    rx_meta.origin = chat::RxOrigin::Mesh;
    rx_meta.channel_hash = header.channel;
    rx_meta.wire_flags = header.flags;
    rx_meta.next_hop = header.next_hop;
    rx_meta.relay_node = header.relay_node;
    rx_meta.hop_count = chat::meshtastic::computeHopsAway(header.flags);
    rx_meta.hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    rx_meta.direct = (rx_meta.hop_count == 0);
    rx_meta.from_is = false;
    rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(rssi * 10.0f));
    rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(snr * 10.0f));
    rx_meta.freq_hz = freq_hz;
    rx_meta.bw_hz = bw_hz;
    rx_meta.sf = sf;
    rx_meta.cr = cr;
}

} // namespace

MeshtasticRadioAdapter::MeshtasticRadioAdapter(LoraBoard& board)
    : board_(board)
{
    initNodeIdentity();
    nodeinfo_scheduler_.configure(kNodeInfoBroadcastIntervalMs);
}

chat::MeshCapabilities MeshtasticRadioAdapter::getCapabilities() const
{
    chat::MeshCapabilities caps{};
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_node_info = true;
    return caps;
}

bool MeshtasticRadioAdapter::sendText(chat::ChannelId channel,
                                      const std::string& text,
                                      chat::MessageId* out_msg_id,
                                      chat::NodeId peer)
{
    if (out_msg_id)
    {
        *out_msg_id = 0;
    }
    if (!ready_ || text.empty())
    {
        return false;
    }

    const chat::NodeId dest = (peer != 0) ? peer : kBroadcastNodeId;
    const chat::MessageId msg_id = next_packet_id_++;
    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);
    if (!chat::meshtastic::encodeTextMessage(channel, text, node_id_, msg_id, dest,
                                             data_buffer, &data_size))
    {
        return false;
    }

    if (out_msg_id)
    {
        *out_msg_id = msg_id;
    }
    return sendEncodedPayload(channel, data_buffer, data_size, peer, false, msg_id, true);
}

bool MeshtasticRadioAdapter::pollIncomingText(chat::MeshIncomingText* out)
{
    if (!out || text_queue_.empty())
    {
        return false;
    }
    *out = text_queue_.front();
    text_queue_.pop();
    return true;
}

bool MeshtasticRadioAdapter::sendAppData(chat::ChannelId channel,
                                         uint32_t portnum,
                                         const uint8_t* payload,
                                         size_t len,
                                         chat::NodeId dest,
                                         bool want_ack,
                                         chat::MessageId packet_id,
                                         bool want_response)
{
    if (!ready_)
    {
        return false;
    }

    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);
    if (!chat::meshtastic::encodeAppData(portnum, payload, len, want_response,
                                         data_buffer, &data_size))
    {
        return false;
    }

    return sendEncodedPayload(channel, data_buffer, data_size, dest, want_ack, packet_id, false);
}

bool MeshtasticRadioAdapter::pollIncomingData(chat::MeshIncomingData* out)
{
    if (!out || data_queue_.empty())
    {
        return false;
    }
    *out = data_queue_.front();
    data_queue_.pop();
    return true;
}

bool MeshtasticRadioAdapter::requestNodeInfo(chat::NodeId dest, bool want_response)
{
    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    if (dest != 0)
    {
        auto it = node_last_channel_.find(dest);
        if (it != node_last_channel_.end())
        {
            channel = it->second;
        }
    }
    return sendNodeInfoTo(dest == 0 ? kBroadcastNodeId : dest, want_response, channel);
}

bool MeshtasticRadioAdapter::triggerDiscoveryAction(chat::MeshDiscoveryAction action)
{
    switch (action)
    {
    case chat::MeshDiscoveryAction::SendIdBroadcast:
        return broadcastNodeInfo();
    case chat::MeshDiscoveryAction::SendIdLocal:
    case chat::MeshDiscoveryAction::ScanLocal:
        return requestNodeInfo(0, true);
    default:
        return false;
    }
}

void MeshtasticRadioAdapter::applyConfig(const chat::MeshConfig& config)
{
    config_ = config;
    updateChannelKeys();
    configureRadio();
}

void MeshtasticRadioAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    user_long_name_ = long_name ? long_name : "";
    user_short_name_ = short_name ? short_name : "";
    nodeinfo_broadcast_sent_ = false;
}

bool MeshtasticRadioAdapter::isReady() const
{
    return ready_;
}

bool MeshtasticRadioAdapter::pollIncomingRawPacket(uint8_t* out_data,
                                                   size_t& out_len,
                                                   size_t max_len)
{
    (void)out_data;
    (void)out_len;
    (void)max_len;
    return false;
}

void MeshtasticRadioAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    processReceivedPacket(data, size);
}

void MeshtasticRadioAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

void MeshtasticRadioAdapter::processSendQueue()
{
    // TEMP DIAG (radio pump trace): ground truth at the convergence point.
    static uint32_t s_psq_n = 0;
    pollRadio();
    bool attempted = false;
    bool result = false;
    if (ready_)
    {
        const uint32_t now_ms = now_millis();
        if (nodeinfo_scheduler_.due(now_ms))
        {
            attempted = true;
            result = broadcastNodeInfo();
            if (result)
            {
                nodeinfo_scheduler_.markSent(now_ms);
                nodeinfo_broadcast_sent_ = true;
            }
        }
    }
    if (s_psq_n < 5U || (s_psq_n % 256U) == 0U || attempted)
    {
        ESP_LOGI(kTag, "psq n=%lu ready=%d online=%d ni_sent=%d attempt=%d result=%d",
                 static_cast<unsigned long>(s_psq_n), ready_ ? 1 : 0,
                 board_.isRadioOnline() ? 1 : 0, nodeinfo_broadcast_sent_ ? 1 : 0,
                 attempted ? 1 : 0, result ? 1 : 0);
    }
    ++s_psq_n;
}

chat::NodeId MeshtasticRadioAdapter::getNodeId() const
{
    return node_id_;
}

bool MeshtasticRadioAdapter::broadcastNodeInfo()
{
    return sendNodeInfoTo(kBroadcastNodeId, false, chat::ChannelId::PRIMARY);
}

bool MeshtasticRadioAdapter::sendEncodedPayload(chat::ChannelId channel,
                                                const uint8_t* payload,
                                                size_t len,
                                                chat::NodeId dest,
                                                bool want_ack,
                                                chat::MessageId packet_id,
                                                bool publish_send_result)
{
    if (!ready_ || !payload || len == 0 || !board_.isRadioOnline())
    {
        return false;
    }

    size_t psk_len = 0;
    const uint8_t* psk = channelKeyFor(channel, &psk_len);
    const uint8_t channel_hash = channelHashFor(channel);
    const chat::NodeId dest_node = (dest != 0) ? dest : kBroadcastNodeId;
    const chat::MessageId msg_id = (packet_id != 0) ? packet_id : next_packet_id_++;

    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);
    if (!chat::meshtastic::buildWirePacket(payload, len, node_id_, msg_id,
                                           dest_node, channel_hash, config_.hop_limit,
                                           want_ack, psk, psk_len,
                                           wire_buffer, &wire_size))
    {
        return false;
    }

    const int state = board_.transmitRadio(wire_buffer, wire_size);
    const bool ok = (state == static_cast<int>(kRadioOk));
    if (ok)
    {
        rx_started_ = false;
        ensureReceiveStarted();
        if (publish_send_result)
        {
            sys::EventBus::publish(new sys::ChatSendResultEvent(msg_id, true), 0);
        }
    }
    else if (publish_send_result)
    {
        sys::EventBus::publish(new sys::ChatSendResultEvent(msg_id, false), 0);
    }

    ESP_LOGI(kTag,
             "tx from=%08lX to=%08lX id=%08lX ch=%u len=%u ok=%d",
             static_cast<unsigned long>(node_id_),
             static_cast<unsigned long>(dest_node),
             static_cast<unsigned long>(msg_id),
             static_cast<unsigned>(channel_hash),
             static_cast<unsigned>(wire_size),
             ok ? 1 : 0);
    return ok;
}

bool MeshtasticRadioAdapter::sendNodeInfoTo(chat::NodeId dest,
                                            bool want_response,
                                            chat::ChannelId channel)
{
    uint8_t payload[256];
    size_t payload_len = sizeof(payload);
    if (!chat::meshtastic::encodeNodeInfoMessage(std::to_string(node_id_),
                                                 user_long_name_,
                                                 user_short_name_,
                                                 meshtastic_HardwareModel_PRIVATE_HW,
                                                 mac_addr_,
                                                 nullptr,
                                                 0,
                                                 want_response,
                                                 payload,
                                                 &payload_len))
    {
        return false;
    }

    return sendEncodedPayload(channel,
                              payload,
                              payload_len,
                              dest == kBroadcastNodeId ? 0 : dest,
                              false,
                              0,
                              false);
}

bool MeshtasticRadioAdapter::sendRoutingAck(chat::NodeId dest,
                                            chat::MessageId request_id,
                                            chat::ChannelId channel)
{
    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = meshtastic_Routing_Error_NONE;

    uint8_t routing_buf[64];
    pb_ostream_t routing_stream = pb_ostream_from_buffer(routing_buf, sizeof(routing_buf));
    if (!pb_encode(&routing_stream, meshtastic_Routing_fields, &routing))
    {
        return false;
    }

    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_ROUTING_APP;
    data.dest = dest;
    data.source = node_id_;
    data.request_id = request_id;
    data.has_bitfield = true;
    data.bitfield = 0;
    data.payload.size = routing_stream.bytes_written;
    std::memcpy(data.payload.bytes, routing_buf, data.payload.size);

    uint8_t data_buf[128];
    pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    return sendEncodedPayload(channel, data_buf, data_stream.bytes_written, dest, false, 0, false);
}

void MeshtasticRadioAdapter::processReceivedPacket(const uint8_t* data, size_t size)
{
    if (!data || size < sizeof(chat::meshtastic::PacketHeaderWire))
    {
        return;
    }

    chat::meshtastic::PacketHeaderWire header{};
    uint8_t payload[256];
    size_t payload_size = sizeof(payload);
    if (!chat::meshtastic::parseWirePacket(data, size, &header, payload, &payload_size))
    {
        return;
    }

    if (header.from == node_id_ || dedup_.isDuplicate(header.from, header.id))
    {
        return;
    }
    dedup_.markSeen(header.from, header.id);

    // Multi-channel RX: the wire carries only an 8-bit channel hash, which can
    // collide across configured channels. Enumerate every enabled channel whose
    // hash matches header.channel and try each one's key in turn, accepting the
    // first whose payload decrypts AND pb_decodes. This is what lets channels
    // 2..7 receive; channel 0 still wins on its own (usually unique) hash.
    const size_t match_count = channelMatchCount(header.channel);
    if (match_count == 0)
    {
        ESP_LOGI(kTag,
                 "drop unknown channel from=%08lX id=%08lX hash=0x%02X",
                 static_cast<unsigned long>(header.from),
                 static_cast<unsigned long>(header.id),
                 static_cast<unsigned>(header.channel));
        return;
    }

    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    uint8_t plaintext[256];
    size_t plaintext_len = 0;
    bool decoded_ok = false;
    meshtastic_Data decoded = meshtastic_Data_init_default;

    for (size_t candidate = 0; candidate < match_count && !decoded_ok; ++candidate)
    {
        const chat::ChannelId cand_channel = channelMatchAt(header.channel, candidate);
        size_t psk_len = 0;
        const uint8_t* psk = channelKeyFor(cand_channel, &psk_len);

        uint8_t attempt[256];
        size_t attempt_len = sizeof(attempt);
        if (psk_len > 0)
        {
            if (!chat::meshtastic::decryptPayload(header, payload, payload_size, psk, psk_len,
                                                  attempt, &attempt_len))
            {
                continue; // try the next colliding channel
            }
        }
        else
        {
            std::memcpy(attempt, payload, payload_size);
            attempt_len = payload_size;
        }

        meshtastic_Data attempt_decoded = meshtastic_Data_init_default;
        pb_istream_t stream = pb_istream_from_buffer(attempt, attempt_len);
        if (!pb_decode(&stream, meshtastic_Data_fields, &attempt_decoded))
        {
            continue; // wrong key (hash collision) -> retry next candidate
        }

        // Accepted: this channel's key produced a valid Data payload.
        channel = cand_channel;
        std::memcpy(plaintext, attempt, attempt_len);
        plaintext_len = attempt_len;
        decoded = attempt_decoded;
        decoded_ok = true;
    }

    if (!decoded_ok)
    {
        return;
    }

    chat::RxMeta rx_meta{};
    fill_rx_meta(rx_meta, header, last_rx_rssi_, last_rx_snr_,
                 radio_freq_hz_, radio_bw_hz_, radio_sf_, radio_cr_);

    size_t resolved_psk_len = 0;
    (void)channelKeyFor(channel, &resolved_psk_len);

    node_last_channel_[header.from] = channel;

    // Symmetric to the tx log: a real received+decoded packet (passed parse, dedup,
    // channel match, decrypt, and decode). header.from is never our own id (filtered
    // above), and rssi reflects the actual reception, so this is a trustworthy RX signal.
    ESP_LOGI(kTag, "rx from=%08lX to=%08lX id=%08lX port=%u rssi=%.0f snr=%.1f",
             static_cast<unsigned long>(header.from), static_cast<unsigned long>(header.to),
             static_cast<unsigned long>(header.id), static_cast<unsigned>(decoded.portnum),
             static_cast<double>(last_rx_rssi_), static_cast<double>(last_rx_snr_));

    const bool to_us = (header.to == node_id_);
    const bool is_broadcast = (header.to == kBroadcastNodeId);
    const bool want_ack = (header.flags & chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    const bool want_response =
        decoded.want_response ||
        (decoded.has_bitfield && ((decoded.bitfield & kBitfieldWantResponseMask) != 0));

    if (want_ack && to_us)
    {
        (void)sendRoutingAck(header.from, header.id, channel);
    }

    if (decoded.portnum == meshtastic_PortNum_NODEINFO_APP)
    {
        if (decoded.payload.size > 0)
        {
            (void)publishNodePayload(decoded,
                                     rx_meta,
                                     header.from,
                                     to_channel_index(channel));
        }
        if (want_response && (to_us || is_broadcast))
        {
            (void)sendNodeInfoTo(header.from, false, channel);
        }
        return;
    }

    if (decoded.portnum == meshtastic_PortNum_POSITION_APP && decoded.payload.size > 0)
    {
        chat::meshtastic::DecodedPositionPayload position{};
        if (chat::meshtastic::decodePositionPayload(
                decoded,
                header.from,
                rx_meta.rx_timestamp_s,
                &position))
        {
            publishPositionEvent(position.node_id, position.position);
        }
    }

    if (decoded.portnum == meshtastic_PortNum_ROUTING_APP)
    {
        return;
    }

    chat::MeshIncomingText incoming_text{};
    if (chat::meshtastic::decodeTextMessage(plaintext, plaintext_len, &incoming_text))
    {
        incoming_text.from = header.from;
        incoming_text.to = header.to;
        incoming_text.msg_id = header.id;
        incoming_text.channel = channel;
        incoming_text.hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
        incoming_text.encrypted = (resolved_psk_len > 0);
        incoming_text.rx_meta = rx_meta;
        text_queue_.push(incoming_text);
        return;
    }

    if (decoded.payload.size > 0)
    {
        chat::MeshIncomingData incoming_data{};
        incoming_data.portnum = decoded.portnum;
        incoming_data.from = header.from;
        incoming_data.to = header.to;
        incoming_data.packet_id = header.id;
        incoming_data.request_id = decoded.request_id;
        incoming_data.channel = channel;
        incoming_data.channel_hash = header.channel;
        incoming_data.hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
        incoming_data.want_response = want_response;
        incoming_data.payload.assign(decoded.payload.bytes,
                                     decoded.payload.bytes + decoded.payload.size);
        incoming_data.rx_meta = rx_meta;
        data_queue_.push(incoming_data);
    }
}

void MeshtasticRadioAdapter::pollRadio()
{
    if (!ready_ || !board_.isRadioOnline())
    {
        return;
    }

    ensureReceiveStarted();

    const uint32_t irq = board_.getRadioIrqFlags();
    if (irq == 0)
    {
        return;
    }

    // The SX1262 IRQ-status register reports ALL events that have occurred, not
    // just the ones routed to a DIO pin. During a reception in continuous RX it
    // raises non-terminal progress flags (PreambleDetected / SyncWordValid /
    // HeaderValid) well before RxDone. If we clear those and re-arm RX here we
    // abort the in-flight packet and RxDone never fires. Only act on terminal
    // events; leave a reception that is merely in progress untouched.
    const bool rx_done = (irq & kIrqRxDone) != 0;
    const bool rx_error = (irq & (kIrqHeaderErr | kIrqCrcErr | kIrqTimeout)) != 0;
    if (!rx_done && !rx_error)
    {
        // Reception in progress (or a stray progress IRQ); do not disturb RX.
        return;
    }

    board_.clearRadioIrqFlags(irq);
    if (!rx_done)
    {
        if (rx_error)
        {
            ESP_LOGI(kTag, "radio irq=0x%04lX", static_cast<unsigned long>(irq));
        }
        rx_started_ = false;
        ensureReceiveStarted();
        return;
    }

    const int packet_length = board_.getRadioPacketLength(true);
    if (packet_length <= 0 || packet_length > 255)
    {
        rx_started_ = false;
        ensureReceiveStarted();
        return;
    }

    uint8_t buffer[255];
    if (board_.readRadioData(buffer, static_cast<size_t>(packet_length)) == static_cast<int>(kRadioOk))
    {
        setLastRxStats(board_.getRadioRSSI(), board_.getRadioSNR());
        processReceivedPacket(buffer, static_cast<size_t>(packet_length));
    }

    if (rx_error)
    {
        ESP_LOGI(kTag, "radio irq=0x%04lX", static_cast<unsigned long>(irq));
    }

    rx_started_ = false;
    ensureReceiveStarted();
}

void MeshtasticRadioAdapter::configureRadio()
{
    if (!board_.isRadioOnline())
    {
        ready_ = false;
        return;
    }

    const chat::meshtastic::RadioConfig radio =
        chat::meshtastic::deriveRadioConfig(config_);

    board_.configureLoraRadio(radio.freq_mhz,
                              radio.bw_khz,
                              radio.sf,
                              radio.cr_denom,
                              radio.tx_power_dbm,
                              radio.preamble_len,
                              radio.sync_word,
                              radio.crc_len);
    radio_freq_hz_ = static_cast<uint32_t>(std::lround(radio.freq_mhz * 1000000.0f));
    radio_bw_hz_ = static_cast<uint32_t>(std::lround(radio.bw_khz * 1000.0f));
    radio_sf_ = radio.sf;
    radio_cr_ = radio.cr_denom;
    ready_ = true;
    rx_started_ = false;
    ensureReceiveStarted();

    ESP_LOGI(kTag,
             "radio ready node=%08lX region=%u preset=%u use_preset=%u freq=%.3f bw=%.1f sf=%u cr=4/%u tx=%d ch=%lu sync=0x%02X preamble=%u hash=(%02X,%02X)",
             static_cast<unsigned long>(node_id_),
             static_cast<unsigned>(radio.region_code),
             static_cast<unsigned>(radio.modem_preset),
             radio.using_preset ? 1U : 0U,
             radio.freq_mhz,
             radio.bw_khz,
             static_cast<unsigned>(radio.sf),
             static_cast<unsigned>(radio.cr_denom),
             static_cast<int>(radio.tx_power_dbm),
             static_cast<unsigned long>(radio.channel_slot),
             static_cast<unsigned>(radio.sync_word),
             static_cast<unsigned>(radio.preamble_len),
             static_cast<unsigned>(channel_hash_[0]),
             static_cast<unsigned>(channel_hash_[1]));
}

void MeshtasticRadioAdapter::updateChannelKeys()
{
    // Compute the on-air hash + expanded PSK for every configured channel slot
    // using the SAME channelHashFromRecord the host test verifies, so the
    // host-tested hash IS the on-air hash for channels 0 and 2..7 alike.
    //
    // Channel 0/1 no-regression: slot 0 with an empty key defaults to the
    // LongFast default PSK (short-PSK index 1), exactly as the old PRIMARY path
    // did; any other slot with an empty key stays open (no PSK), exactly as the
    // old SECONDARY path did.
    for (std::size_t i = 0; i < chat::kMaxChannels; ++i)
    {
        channel_enabled_[i] = config_.channels[i].enabled;
        std::memset(channel_psk_[i], 0, sizeof(channel_psk_[i]));
        channel_psk_len_[i] = 0;
        channel_hash_[i] = 0;
        if (!config_.channels[i].enabled)
        {
            continue;
        }

        // Build an effective record: normalize the stored key length, and apply
        // the slot-0 default-PSK rule for an empty key.
        chat::ChannelRecord eff = config_.channels[i];
        const uint8_t normalized_len = chat::normalizeMeshtasticChannelKeyLen(
            eff.key, sizeof(eff.key), eff.key_len);
        if (normalized_len == 0)
        {
            if (i == 0)
            {
                // Empty key on the public channel => LongFast default PSK.
                eff.key_len = 1;
                eff.key[0] = kDefaultPskIndex;
            }
            else
            {
                eff.key_len = 0;
            }
        }
        else
        {
            eff.key_len = normalized_len;
        }
        // Slot 0's name follows the modem-preset fallback (primaryChannelName).
        const char* name = chat::meshtastic::channelName(config_, i);
        std::snprintf(eff.name, sizeof(eff.name), "%s", name ? name : "");

        // Expand the PSK bytes (what buildWirePacket / decryptPayload consume).
        std::size_t expanded_len = 0;
        chat::meshtastic::expandChannelPsk(eff.key, eff.key_len, channel_psk_[i], &expanded_len);
        channel_psk_len_[i] = expanded_len;

        // Hash is derived from the SAME effective record (single source).
        channel_hash_[i] = chat::meshtastic::channelHashFromRecord(eff);
    }
}

void MeshtasticRadioAdapter::initNodeIdentity()
{
    std::memset(mac_addr_, 0, sizeof(mac_addr_));
    (void)esp_efuse_mac_get_default(mac_addr_);
    node_id_ = (static_cast<uint32_t>(mac_addr_[2]) << 24) |
               (static_cast<uint32_t>(mac_addr_[3]) << 16) |
               (static_cast<uint32_t>(mac_addr_[4]) << 8) |
               static_cast<uint32_t>(mac_addr_[5]);
}

void MeshtasticRadioAdapter::ensureReceiveStarted()
{
    if (!rx_started_)
    {
        rx_started_ = (board_.startRadioReceive() == static_cast<int>(kRadioOk));
    }
}

bool MeshtasticRadioAdapter::publishNodePayload(const meshtastic_Data& data,
                                                const chat::RxMeta& rx_meta,
                                                chat::NodeId from_node,
                                                uint8_t channel_index)
{
    chat::meshtastic::NodePayloadDecodeContext context{};
    context.fallback_node_id = from_node;
    context.snr = static_cast<float>(rx_meta.snr_db_x10) / 10.0f;
    context.rssi = static_cast<float>(rx_meta.rssi_dbm_x10) / 10.0f;
    context.timestamp = rx_meta.rx_timestamp_s != 0
                            ? rx_meta.rx_timestamp_s
                            : chat::now_message_timestamp();
    context.hops_away = rx_meta.hop_count;
    context.channel = channel_index;
    context.via_mqtt =
        (rx_meta.wire_flags &
         chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0;

    chat::meshtastic::DecodedNodePayload node{};
    if (!chat::meshtastic::decodeNodeInfoPayload(data, context, &node))
    {
        return false;
    }

    sys::EventBus::publish(
        new sys::NodeInfoUpdateEvent(
            node.node_id,
            node.short_name.c_str(),
            node.long_name.c_str(),
            node.snr,
            node.rssi,
            node.timestamp,
            node.protocol,
            node.role,
            node.hops_away,
            node.hw_model,
            node.channel,
            node.has_macaddr,
            node.has_macaddr ? node.macaddr.data() : nullptr,
            node.via_mqtt,
            node.is_ignored,
            node.has_public_key,
            false,
            node.has_device_metrics,
            node.has_device_metrics ? &node.device_metrics : nullptr),
        0);
    if (node.has_position)
    {
        publishPositionEvent(node.node_id, node.position);
    }
    return true;
}

void MeshtasticRadioAdapter::publishPositionEvent(chat::NodeId node_id,
                                                  const chat::contacts::NodePosition& pos)
{
    if (node_id == 0 || !pos.valid)
    {
        return;
    }

    sys::EventBus::publish(
        new sys::NodePositionUpdateEvent(node_id,
                                         pos.latitude_i,
                                         pos.longitude_i,
                                         pos.has_altitude,
                                         pos.altitude,
                                         pos.timestamp,
                                         pos.precision_bits,
                                         pos.pdop,
                                         pos.hdop,
                                         pos.vdop,
                                         pos.gps_accuracy_mm),
        0);
}

uint8_t MeshtasticRadioAdapter::channelHashFor(chat::ChannelId channel) const
{
    const std::size_t idx = static_cast<std::size_t>(channel);
    if (idx >= chat::kMaxChannels)
    {
        return channel_hash_[0];
    }
    return channel_hash_[idx];
}

const uint8_t* MeshtasticRadioAdapter::channelKeyFor(chat::ChannelId channel, size_t* out_len) const
{
    std::size_t idx = static_cast<std::size_t>(channel);
    if (idx >= chat::kMaxChannels)
    {
        idx = 0;
    }
    if (out_len)
    {
        *out_len = channel_psk_len_[idx];
    }
    return channel_psk_[idx];
}

size_t MeshtasticRadioAdapter::channelMatchCount(uint8_t hash) const
{
    size_t count = 0;
    for (std::size_t i = 0; i < chat::kMaxChannels; ++i)
    {
        if (channel_enabled_[i] && channel_hash_[i] == hash)
        {
            ++count;
        }
    }
    return count;
}

chat::ChannelId MeshtasticRadioAdapter::channelMatchAt(uint8_t hash, size_t which) const
{
    size_t seen = 0;
    for (std::size_t i = 0; i < chat::kMaxChannels; ++i)
    {
        if (channel_enabled_[i] && channel_hash_[i] == hash)
        {
            if (seen == which)
            {
                return static_cast<chat::ChannelId>(i);
            }
            ++seen;
        }
    }
    return chat::ChannelId::PRIMARY;
}

} // namespace platform::esp::radio
