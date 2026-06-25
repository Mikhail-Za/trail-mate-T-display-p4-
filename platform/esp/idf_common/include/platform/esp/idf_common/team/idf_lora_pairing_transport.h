/**
 * @file idf_lora_pairing_transport.h
 * @brief ESP-IDF ITeamPairingTransport carried over the live LoRa mesh.
 *
 * The Arduino team build bootstraps pairing over ESP-NOW
 * (EspNowTeamPairingTransport). The T-Display P4 has no usable Wi-Fi/ESP-NOW, so
 * this is the drop-in replacement that carries the SAME 3-message
 * Beacon/Join/Key handshake (team::TeamPairingCoordinator, reused UNCHANGED) over
 * the same chat::IMeshAdapter the chat/team services already transmit on, on a
 * dedicated app port (team::proto::TEAM_PAIR_APP).
 *
 * ADDRESSING BRIDGE: ITeamPairingTransport is MAC-addressed (the coordinator
 * treats the 6-byte MAC as an opaque routing token), but LoRa is node-id
 * addressed. We bridge with a synthetic 6-byte MAC {0,0,b3,b2,b1,b0} packed from
 * a 32-bit NodeId, inverting the ESP-NOW code's node_id_from_mac() (which reads
 * MAC bytes [2..5] big-endian). The broadcast MAC FF:FF:FF:FF:FF:FF maps to
 * sendAppData(dest=0) (mesh broadcast); any other MAC unicasts to the decoded
 * NodeId.
 *
 * NO RADIO LOOP: unlike the ESP-NOW transport (which registers an esp_now recv
 * callback), this transport does NOT own a receive loop. The IdfChatFacade pump
 * already drains the adapter's RX queue every tick; it routes TEAM_PAIR_APP
 * frames here via deliverIncoming(), which synthesizes the sender MAC and hands
 * the frame to the coordinator's Receiver. begin()/end()/ensurePeer() are
 * therefore near-no-ops (no Wi-Fi stack, no peer table).
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "team/ports/i_team_pairing_transport.h"

#include <cstddef>
#include <cstdint>

namespace chat
{
class IMeshAdapter;
}

namespace platform::esp::idf_common::team_infra
{

/**
 * @brief Pack a 32-bit NodeId into the synthetic 6-byte MAC the coordinator
 *        routes with. Inverse of node_id_from_mac(): MAC = {0,0,b3,b2,b1,b0}
 *        with b3 the NodeId high byte. out must hold 6 bytes.
 */
void macFromNodeId(uint32_t node_id, uint8_t* out);

/**
 * @brief Recover the 32-bit NodeId from the synthetic MAC (bytes [2..5],
 *        big-endian). Matches the ESP-NOW transport's node_id_from_mac().
 */
uint32_t nodeIdFromMac(const uint8_t* mac);

class IdfLoraTeamPairingTransport final : public ::team::ITeamPairingTransport
{
  public:
    /**
     * @param mesh The live mesh adapter chat/team transmit on (must outlive this).
     * @param channel The channel id pairing frames are sent on (PRIMARY).
     */
    explicit IdfLoraTeamPairingTransport(::chat::IMeshAdapter& mesh,
                                         ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY);

    // -- ITeamPairingTransport ----------------------------------------------
    bool begin(Receiver& receiver) override;
    void end() override;
    bool ensurePeer(const uint8_t* mac) override;
    bool send(const uint8_t* mac, const uint8_t* data, size_t len) override;

    /**
     * @brief Feed a received TEAM_PAIR_APP frame into the handshake. Called by
     *        the facade pump for every mesh frame whose portnum == TEAM_PAIR_APP.
     *        Synthesizes the sender MAC from from_node_id and forwards to the
     *        registered Receiver (the coordinator's RX entry point). No-op if the
     *        transport has not been begun (no receiver) yet.
     */
    void deliverIncoming(::chat::NodeId from_node_id, const uint8_t* data, size_t len);

    /// True once begin() has stored a receiver (pairing is "active").
    bool isActive() const { return receiver_ != nullptr; }

  private:
    ::chat::IMeshAdapter& mesh_;
    ::chat::ChannelId channel_;
    Receiver* receiver_ = nullptr;
};

} // namespace platform::esp::idf_common::team_infra
