#include "platform/esp/idf_common/team/idf_lora_pairing_transport.h"

#include "chat/ports/i_mesh_adapter.h"
#include "team/protocol/team_pairing_wire.h"
#include "team/protocol/team_portnum.h"

#include "esp_log.h"

namespace platform::esp::idf_common::team_infra
{
namespace
{

constexpr const char* kTag = "idf-team-pair-tx";

bool isBroadcastMac(const uint8_t* mac)
{
    if (mac == nullptr)
    {
        return false;
    }
    for (int i = 0; i < 6; ++i)
    {
        if (mac[i] != 0xFF)
        {
            return false;
        }
    }
    return true;
}

// Human-readable pairing message type for serial traces. Best-effort: if the
// wire can't be decoded we log the raw first byte.
const char* pairingMsgName(const uint8_t* data, size_t len)
{
    ::team::proto::pairing::MessageType type{};
    if (!::team::proto::pairing::decodeType(data, len, &type))
    {
        return "?";
    }
    switch (type)
    {
    case ::team::proto::pairing::MessageType::Beacon:
        return "Beacon";
    case ::team::proto::pairing::MessageType::Join:
        return "Join";
    case ::team::proto::pairing::MessageType::Key:
        return "Key";
    default:
        return "Unknown";
    }
}

} // namespace

void macFromNodeId(uint32_t node_id, uint8_t* out)
{
    if (out == nullptr)
    {
        return;
    }
    // Inverse of nodeIdFromMac(): pack the NodeId into MAC bytes [2..5]
    // big-endian, leaving [0..1] zero. node_id 0 (broadcast) is never passed
    // here -- the coordinator uses the all-0xFF broadcast MAC for that.
    out[0] = 0x00;
    out[1] = 0x00;
    out[2] = static_cast<uint8_t>((node_id >> 24) & 0xFF);
    out[3] = static_cast<uint8_t>((node_id >> 16) & 0xFF);
    out[4] = static_cast<uint8_t>((node_id >> 8) & 0xFF);
    out[5] = static_cast<uint8_t>(node_id & 0xFF);
}

uint32_t nodeIdFromMac(const uint8_t* mac)
{
    if (mac == nullptr)
    {
        return 0;
    }
    return (static_cast<uint32_t>(mac[2]) << 24) |
           (static_cast<uint32_t>(mac[3]) << 16) |
           (static_cast<uint32_t>(mac[4]) << 8) |
           static_cast<uint32_t>(mac[5]);
}

IdfLoraTeamPairingTransport::IdfLoraTeamPairingTransport(::chat::IMeshAdapter& mesh,
                                                         ::chat::ChannelId channel)
    : mesh_(mesh), channel_(channel)
{
}

bool IdfLoraTeamPairingTransport::begin(Receiver& receiver)
{
    // No Wi-Fi/ESP-NOW stack to bring up and no radio loop to own: the facade
    // pump drives RX via deliverIncoming(). Just latch the receiver so send()
    // and deliverIncoming() are live.
    receiver_ = &receiver;
    ESP_LOGI(kTag, "LoRa pairing transport begin (mesh adapter=%p)",
             static_cast<void*>(&mesh_));
    return true;
}

void IdfLoraTeamPairingTransport::end()
{
    receiver_ = nullptr;
    ESP_LOGI(kTag, "LoRa pairing transport end");
}

bool IdfLoraTeamPairingTransport::ensurePeer(const uint8_t* mac)
{
    // LoRa mesh has no per-peer association step (unlike esp_now_add_peer). The
    // synthetic MAC is just a routing token; nothing to register.
    (void)mac;
    return true;
}

bool IdfLoraTeamPairingTransport::send(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (mac == nullptr || data == nullptr || len == 0)
    {
        return false;
    }

    const bool broadcast = isBroadcastMac(mac);
    const ::chat::NodeId dest = broadcast ? 0 : static_cast<::chat::NodeId>(nodeIdFromMac(mac));

    const bool ok = mesh_.sendAppData(channel_, ::team::proto::TEAM_PAIR_APP,
                                      data, len, dest,
                                      /*want_ack=*/false, /*packet_id=*/0,
                                      /*want_response=*/false);

    ESP_LOGI(kTag, "TX %s %s dest=%08lX len=%u -> %s",
             pairingMsgName(data, len),
             broadcast ? "broadcast" : "unicast",
             static_cast<unsigned long>(dest),
             static_cast<unsigned>(len),
             ok ? "queued" : "FAILED");
    return ok;
}

void IdfLoraTeamPairingTransport::deliverIncoming(::chat::NodeId from_node_id,
                                                  const uint8_t* data, size_t len)
{
    if (receiver_ == nullptr || data == nullptr || len == 0)
    {
        return;
    }

    uint8_t mac[6];
    macFromNodeId(static_cast<uint32_t>(from_node_id), mac);

    ESP_LOGI("idf-team-pair-rx", "RX %s from=%08lX len=%u",
             pairingMsgName(data, len),
             static_cast<unsigned long>(from_node_id),
             static_cast<unsigned>(len));

    receiver_->onPairingReceive(mac, data, len);
}

} // namespace platform::esp::idf_common::team_infra
