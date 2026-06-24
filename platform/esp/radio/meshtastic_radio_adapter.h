#pragma once

#include "board/LoraBoard.h"
#include "chat/domain/chat_types.h"
#include "chat/domain/contact_types.h"
#include "chat/domain/nodeinfo_broadcast_scheduler.h"
#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_dedup.h"
#include "chat/ports/i_mesh_adapter.h"

#include <map>
#include <queue>
#include <string>

namespace platform::esp::radio
{

class MeshtasticRadioAdapter final : public chat::IMeshAdapter
{
  public:
    explicit MeshtasticRadioAdapter(LoraBoard& board);

    chat::MeshCapabilities getCapabilities() const override;
    bool sendText(chat::ChannelId channel, const std::string& text,
                  chat::MessageId* out_msg_id, chat::NodeId peer = 0) override;
    bool pollIncomingText(chat::MeshIncomingText* out) override;
    bool sendAppData(chat::ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     chat::NodeId dest = 0, bool want_ack = false,
                     chat::MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(chat::MeshIncomingData* out) override;
    bool requestNodeInfo(chat::NodeId dest, bool want_response) override;
    bool triggerDiscoveryAction(chat::MeshDiscoveryAction action) override;
    void applyConfig(const chat::MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;
    chat::NodeId getNodeId() const override;

    bool broadcastNodeInfo();

  private:
    static constexpr uint32_t kBroadcastNodeId = 0xFFFFFFFFu;

    bool sendEncodedPayload(chat::ChannelId channel,
                            const uint8_t* payload,
                            size_t len,
                            chat::NodeId dest,
                            bool want_ack,
                            chat::MessageId packet_id,
                            bool publish_send_result);
    bool sendNodeInfoTo(chat::NodeId dest, bool want_response, chat::ChannelId channel);
    bool sendRoutingAck(chat::NodeId dest, chat::MessageId request_id, chat::ChannelId channel);
    void processReceivedPacket(const uint8_t* data, size_t size);
    void pollRadio();
    void configureRadio();
    void updateChannelKeys();
    void initNodeIdentity();
    void ensureReceiveStarted();
    bool publishNodePayload(const meshtastic_Data& data,
                            const chat::RxMeta& rx_meta,
                            chat::NodeId from_node,
                            uint8_t channel_index);
    void publishPositionEvent(chat::NodeId node_id,
                              const chat::contacts::NodePosition& pos);
    uint8_t channelHashFor(chat::ChannelId channel) const;
    const uint8_t* channelKeyFor(chat::ChannelId channel, size_t* out_len) const;
    // Map a wire channel-hash byte back to a configured, enabled channel index.
    // Multiple channels can share a hash (8-bit collisions), so the RX path
    // tries each match in turn; matchCount/matchAt enumerate them.
    size_t channelMatchCount(uint8_t hash) const;
    chat::ChannelId channelMatchAt(uint8_t hash, size_t which) const;

    LoraBoard& board_;
    chat::MeshConfig config_{};
    chat::meshtastic::MtDedup dedup_{};
    std::queue<chat::MeshIncomingText> text_queue_{};
    std::queue<chat::MeshIncomingData> data_queue_{};
    std::map<chat::NodeId, chat::ChannelId> node_last_channel_{};
    std::string user_long_name_{};
    std::string user_short_name_{};
    chat::MessageId next_packet_id_ = 1;
    chat::NodeId node_id_ = 0;
    uint8_t mac_addr_[6] = {};
    bool ready_ = false;
    bool rx_started_ = false;
    bool nodeinfo_broadcast_sent_ = false;
    chat::NodeInfoBroadcastScheduler nodeinfo_scheduler_;
    float last_rx_rssi_ = 0.0f;
    float last_rx_snr_ = 0.0f;
    uint32_t radio_freq_hz_ = 0;
    uint32_t radio_bw_hz_ = 0;
    uint8_t radio_sf_ = 0;
    uint8_t radio_cr_ = 0;
    // Per-channel derived hash + expanded PSK for all kMaxChannels slots. Only
    // enabled slots participate in TX/RX (channel_enabled_). Computed once per
    // applyConfig() in updateChannelKeys() using the SAME channelHashFromRecord
    // the host test verifies (single source -> host-tested hash IS on-air hash).
    bool channel_enabled_[chat::kMaxChannels] = {};
    uint8_t channel_hash_[chat::kMaxChannels] = {};
    uint8_t channel_psk_[chat::kMaxChannels][chat::kMeshtasticChannelKeyMaxLen] = {};
    size_t channel_psk_len_[chat::kMaxChannels] = {};
};

} // namespace platform::esp::radio
