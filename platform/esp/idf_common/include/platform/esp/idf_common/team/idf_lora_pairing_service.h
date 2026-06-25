/**
 * @file idf_lora_pairing_service.h
 * @brief ESP-IDF TeamPairingService over LoRa (the IDF analogue of
 *        EspNowTeamPairingService) with the Option-A passphrase secrecy floor.
 *
 * Wraps the shared team::TeamPairingCoordinator (the 3-message Beacon/Join/Key
 * state machine, REUSED UNCHANGED) and drives it over a
 * IdfLoraTeamPairingTransport (chat::IMeshAdapter on team::proto::TEAM_PAIR_APP).
 * Lifted from team_pairing_service_espnow.cpp; the differences are:
 *   1. The transport carries frames over LoRa, not ESP-NOW, and owns no radio
 *      loop -- the IdfChatFacade pump feeds RX via deliverPairingFrame().
 *   2. SECURITY (the one real gap LoRa opens that ESP-NOW's ~5 m range hid): the
 *      coordinator's Key frame normally ships the 16-byte channel PSK in the
 *      clear, which over LoRa would broadcast the team secret for kilometres.
 *      Option A closes this: both units type a shared passphrase and derive
 *          PSK = sha256(passphrase || team_id)[:16]
 *      LOCALLY (ITeamCrypto::deriveKey). The on-air handshake then carries only
 *      the non-secret team_id/key_id/nonce plus a PUBLIC sentinel in the Key
 *      frame's PSK field; the real PSK never crosses the air.
 *        - Leader: on startLeader() the derived PSK is applied to this unit's own
 *          team keys (a synthetic TeamKeyDist published to the real event sink),
 *          and the coordinator beacons + answers Join with the sentinel PSK.
 *        - Member: when the coordinator surfaces the air Key frame as
 *          onTeamPairingKeyDist, this service OVERWRITES the (sentinel) PSK with
 *          the locally-derived PSK before forwarding downstream, so the member
 *          ends up with the identical real key without it ever being transmitted.
 *
 *      When NO passphrase is set the service still functions for bring-up but
 *      falls back to the legacy behaviour (the coordinator transmits the real
 *      PSK) -- callers that care about confidentiality MUST set a passphrase.
 *
 * The passphrase + ECDH/SAS logic deliberately lives in THIS IDF-only layer, not
 * the shared coordinator/wire, so the Linux/Arduino peers stay byte-compatible.
 * (Option B -- X25519 + 6-digit SAS -- is a documented follow-up; this is the
 * Option-A floor that gets two units paired.)
 */

#pragma once

#include "platform/esp/idf_common/team/idf_lora_pairing_transport.h"
#include "team/ports/i_team_crypto.h"
#include "team/ports/i_team_pairing_event_sink.h"
#include "team/ports/i_team_runtime.h"
#include "team/usecase/team_pairing_coordinator.h"

#include <cstddef>
#include <cstdint>

namespace chat
{
class IMeshAdapter;
}

namespace platform::esp::idf_common::team_infra
{

class IdfLoraTeamPairingService final : public ::team::TeamPairingService,
                                        private ::team::ITeamPairingTransport::Receiver,
                                        private ::team::ITeamPairingEventSink
{
  public:
    IdfLoraTeamPairingService(::team::ITeamRuntime& runtime,
                              ::team::ITeamPairingEventSink& event_sink,
                              ::team::ITeamCrypto& crypto,
                              ::chat::IMeshAdapter& mesh);

    // -- TeamPairingService -------------------------------------------------
    bool startLeader(const ::team::TeamId& team_id,
                     uint32_t key_id,
                     const uint8_t* psk,
                     size_t psk_len,
                     uint32_t leader_id,
                     const char* team_name) override;
    bool startMember(uint32_t self_id) override;
    void stop() override;
    void update() override;
    ::team::TeamPairingStatus getStatus() const override;

    /**
     * @brief Set the shared pairing passphrase (Option A). Both units must enter
     *        the same string. Stays in effect until cleared; copied (bounded).
     *        Pass nullptr/empty to clear (reverts to the insecure legacy path).
     */
    void setPassphrase(const char* passphrase);
    void clearPassphrase();
    bool hasPassphrase() const { return has_passphrase_; }

    /**
     * @brief Feed a received TEAM_PAIR_APP frame (decoded by the facade pump)
     *        into the handshake. from_node_id is the mesh sender; data/len is the
     *        raw pairing wire. Routed straight to the transport, which synthesizes
     *        the MAC and queues it for the next update() tick.
     */
    void deliverPairingFrame(::chat::NodeId from_node_id, const uint8_t* data, size_t len);

  private:
    bool ensureTransport();
    void shutdownTransport();

    // ITeamPairingTransport::Receiver -- buffered, drained in update(). Called on
    // the SAME task as update() (the facade pump), so no cross-task mutex needed.
    void onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len) override;

    // ITeamPairingEventSink interception -- forwards to real_sink_, rewriting the
    // member's keydist PSK to the locally-derived one in passphrase mode.
    void onTeamPairingStateChanged(const ::team::TeamPairingEvent& event) override;
    void onTeamPairingKeyDist(const ::team::TeamKeyDistEvent& event) override;

    // Derive PSK = sha256(passphrase || team_id)[:16] into out (16 bytes). Returns
    // false if no passphrase is set or the KDF fails.
    bool derivePsk(const ::team::TeamId& team_id, uint8_t* out16) const;

    ::team::ITeamRuntime& runtime_;
    ::team::ITeamPairingEventSink& real_sink_;
    ::team::ITeamCrypto& crypto_;
    IdfLoraTeamPairingTransport transport_;
    ::team::TeamPairingCoordinator core_;
    bool transport_ready_ = false;

    // Option A passphrase (bounded copy; 64 chars is plenty for a shared phrase).
    static constexpr size_t kMaxPassphraseLen = 64;
    char passphrase_[kMaxPassphraseLen + 1] = {0};
    bool has_passphrase_ = false;

    // Single-slot RX buffer (single-task, no mutex). 128 bytes mirrors the
    // ESP-NOW service's RxPacket and comfortably holds Beacon/Join/Key.
    struct RxPacket
    {
        uint8_t mac[6];
        uint8_t data[128];
        size_t len = 0;
    };
    bool rx_pending_ = false;
    RxPacket rx_packet_{};
};

} // namespace platform::esp::idf_common::team_infra
