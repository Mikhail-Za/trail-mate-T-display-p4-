#include "platform/esp/idf_common/team/idf_lora_pairing_service.h"

#include "team/protocol/team_mgmt.h"

#include "esp_log.h"

#include <cstring>

namespace platform::esp::idf_common::team_infra
{
namespace
{

constexpr const char* kTag = "idf-team-pair";

// The public, non-secret 16-byte sentinel the on-air Key frame carries in
// passphrase mode INSTEAD of the real PSK. It is length-16 (the coordinator's
// sendKey() refuses a zero-length PSK) but contains no secret -- both units
// derive the real PSK locally from the passphrase, so whatever lands here from
// the air is discarded by the member. Deliberately a recognizable constant so a
// sniffer capture obviously shows "no key material on air".
constexpr uint8_t kSentinelPsk[::team::proto::kTeamChannelPskSize] = {
    0x5A, 0x00, 0x5A, 0x00, 0x5A, 0x00, 0x5A, 0x00,
    0x5A, 0x00, 0x5A, 0x00, 0x5A, 0x00, 0x5A, 0x00};

} // namespace

IdfLoraTeamPairingService::IdfLoraTeamPairingService(::team::ITeamRuntime& runtime,
                                                     ::team::ITeamPairingEventSink& event_sink,
                                                     ::team::ITeamCrypto& crypto,
                                                     ::chat::IMeshAdapter& mesh)
    : runtime_(runtime),
      real_sink_(event_sink),
      crypto_(crypto),
      transport_(mesh),
      // The coordinator publishes to THIS service (so it can rewrite the member's
      // keydist PSK) and transmits through our LoRa transport.
      core_(runtime, *this, transport_)
{
}

void IdfLoraTeamPairingService::setPassphrase(const char* passphrase)
{
    if (passphrase == nullptr || passphrase[0] == '\0')
    {
        clearPassphrase();
        return;
    }
    std::strncpy(passphrase_, passphrase, kMaxPassphraseLen);
    passphrase_[kMaxPassphraseLen] = '\0';
    has_passphrase_ = true;
    ESP_LOGI(kTag, "pairing passphrase set (len=%u) -- PSK kept off-air (Option A)",
             static_cast<unsigned>(std::strlen(passphrase_)));
}

void IdfLoraTeamPairingService::clearPassphrase()
{
    std::memset(passphrase_, 0, sizeof(passphrase_));
    has_passphrase_ = false;
}

bool IdfLoraTeamPairingService::derivePsk(const ::team::TeamId& team_id, uint8_t* out16) const
{
    if (!has_passphrase_ || out16 == nullptr)
    {
        return false;
    }
    // PSK = sha256(passphrase || team_id)[:16]. deriveKey computes
    // SHA256(key || info)[:out_len] where info is hashed as a C-string (strlen,
    // no NUL). team_id is 6 RAW bytes that may contain 0x00, so it can't be the
    // strlen-bounded info; instead pass the full "passphrase||team_id" buffer as
    // the key and an EMPTY info (0 bytes), giving SHA256(passphrase||team_id).
    uint8_t buf[kMaxPassphraseLen + ::team::proto::kTeamIdSize];
    const size_t plen = std::strlen(passphrase_);
    std::memcpy(buf, passphrase_, plen);
    std::memcpy(buf + plen, team_id.data(), team_id.size());
    const bool ok = crypto_.deriveKey(buf, plen + team_id.size(), "",
                                      out16, ::team::proto::kTeamChannelPskSize);
    std::memset(buf, 0, sizeof(buf)); // don't leave the passphrase on the stack
    return ok;
}

bool IdfLoraTeamPairingService::ensureTransport()
{
    if (transport_ready_)
    {
        return true;
    }
    if (!transport_.begin(*this))
    {
        return false;
    }
    transport_ready_ = true;
    return true;
}

void IdfLoraTeamPairingService::shutdownTransport()
{
    if (!transport_ready_)
    {
        return;
    }
    transport_.end();
    transport_ready_ = false;
    rx_pending_ = false;
}

bool IdfLoraTeamPairingService::startLeader(const ::team::TeamId& team_id,
                                            uint32_t key_id,
                                            const uint8_t* psk,
                                            size_t psk_len,
                                            uint32_t leader_id,
                                            const char* team_name)
{
    if (!ensureTransport())
    {
        return false;
    }

    const uint8_t* coord_psk = psk;
    size_t coord_psk_len = psk_len;

    if (has_passphrase_)
    {
        // Derive the real PSK locally and apply it to THIS unit's own team keys
        // via a synthetic keydist (same downstream path the member uses), so
        // leader + member converge on the identical passphrase-derived key.
        uint8_t derived[::team::proto::kTeamChannelPskSize] = {0};
        if (!derivePsk(team_id, derived))
        {
            ESP_LOGW(kTag, "leader passphrase derive FAILED -- aborting create");
            shutdownTransport();
            return false;
        }

        ::team::TeamKeyDistEvent ev{};
        ev.ctx.team_id = team_id;
        ev.ctx.key_id = key_id;
        ev.ctx.from = leader_id;
        ev.ctx.timestamp = runtime_.nowUnixSeconds();
        ev.msg.team_id = team_id;
        ev.msg.key_id = key_id;
        std::memcpy(ev.msg.channel_psk.data(), derived, sizeof(derived));
        ev.msg.channel_psk_len = static_cast<uint8_t>(sizeof(derived));
        real_sink_.onTeamPairingKeyDist(ev);
        ESP_LOGI(kTag, "leader applied passphrase-derived PSK locally (team_id[0]=%02X key_id=%lu)",
                 team_id[0], static_cast<unsigned long>(key_id));

        // The on-air handshake carries only the public sentinel, never the secret.
        coord_psk = kSentinelPsk;
        coord_psk_len = sizeof(kSentinelPsk);
    }
    else
    {
        ESP_LOGW(kTag, "leader start WITHOUT passphrase -- PSK will be sent over LoRa "
                       "in the clear (insecure; set a passphrase for confidentiality)");
    }

    if (!core_.startLeader(team_id, key_id, coord_psk, coord_psk_len, leader_id, team_name))
    {
        shutdownTransport();
        return false;
    }
    ESP_LOGI(kTag, "leader beaconing: team='%s' leader=%08lX key_id=%lu (LoRa TEAM_PAIR_APP)",
             team_name ? team_name : "", static_cast<unsigned long>(leader_id),
             static_cast<unsigned long>(key_id));
    return true;
}

bool IdfLoraTeamPairingService::startMember(uint32_t self_id)
{
    if (!ensureTransport())
    {
        return false;
    }
    if (!core_.startMember(self_id))
    {
        shutdownTransport();
        return false;
    }
    ESP_LOGI(kTag, "member scanning self=%08lX (LoRa TEAM_PAIR_APP)%s",
             static_cast<unsigned long>(self_id),
             has_passphrase_ ? " [passphrase: will derive PSK locally]"
                             : " [no passphrase: will take PSK from air]");
    return true;
}

void IdfLoraTeamPairingService::stop()
{
    core_.stop();
    shutdownTransport();
}

::team::TeamPairingStatus IdfLoraTeamPairingService::getStatus() const
{
    return core_.getStatus();
}

void IdfLoraTeamPairingService::deliverPairingFrame(::chat::NodeId from_node_id,
                                                    const uint8_t* data, size_t len)
{
    if (!transport_ready_)
    {
        return;
    }
    // Hand to the transport, which synthesizes the sender MAC and calls back into
    // onPairingReceive() (buffered, drained next update()).
    transport_.deliverIncoming(from_node_id, data, len);
}

void IdfLoraTeamPairingService::onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (mac == nullptr || data == nullptr || len == 0 || len > sizeof(rx_packet_.data))
    {
        return;
    }
    // Single task (the facade pump calls both deliverPairingFrame() and update()),
    // so no cross-task mutex is needed. Last-writer-wins if two frames arrive
    // between ticks; the coordinator's retries/timeouts tolerate a dropped frame.
    std::memcpy(rx_packet_.mac, mac, 6);
    std::memcpy(rx_packet_.data, data, len);
    rx_packet_.len = len;
    rx_pending_ = true;
}

void IdfLoraTeamPairingService::update()
{
    if (!transport_ready_)
    {
        return;
    }

    if (rx_pending_)
    {
        rx_pending_ = false;
        core_.handleIncomingPacket(rx_packet_.mac, rx_packet_.data, rx_packet_.len);
    }

    core_.update();

    // Mirror the ESP-NOW service: once the coordinator returns to Idle (paired,
    // failed, or stopped), tear the transport down so a fresh start re-begins it.
    if (transport_ready_ && core_.getStatus().state == ::team::TeamPairingState::Idle)
    {
        shutdownTransport();
    }
}

// ---------------------------------------------------------------------------
// ITeamPairingEventSink interception. The coordinator publishes here; we forward
// to the real sink, rewriting the member's keydist PSK to the locally-derived one
// so the air-carried (sentinel) PSK is never adopted.
// ---------------------------------------------------------------------------

void IdfLoraTeamPairingService::onTeamPairingStateChanged(const ::team::TeamPairingEvent& event)
{
    ESP_LOGI(kTag, "pairing state role=%d state=%d peer=%08lX",
             static_cast<int>(event.role), static_cast<int>(event.state),
             static_cast<unsigned long>(event.peer_id));
    real_sink_.onTeamPairingStateChanged(event);
}

void IdfLoraTeamPairingService::onTeamPairingKeyDist(const ::team::TeamKeyDistEvent& event)
{
    if (!has_passphrase_)
    {
        // Legacy path: trust the air-carried PSK (already in the event).
        ESP_LOGI(kTag, "keydist (no passphrase) team_id[0]=%02X key_id=%lu -> applying air PSK",
                 event.msg.team_id[0], static_cast<unsigned long>(event.msg.key_id));
        real_sink_.onTeamPairingKeyDist(event);
        return;
    }

    // Passphrase mode: discard the air-carried (sentinel) PSK and substitute the
    // locally-derived one so the member ends up with the same real key the leader
    // computed, without it ever being transmitted.
    ::team::TeamKeyDistEvent rewritten = event;
    uint8_t derived[::team::proto::kTeamChannelPskSize] = {0};
    if (derivePsk(event.msg.team_id, derived))
    {
        std::memcpy(rewritten.msg.channel_psk.data(), derived, sizeof(derived));
        rewritten.msg.channel_psk_len = static_cast<uint8_t>(sizeof(derived));
        ESP_LOGI(kTag, "keydist team_id[0]=%02X key_id=%lu -> substituted passphrase-derived PSK (air PSK discarded)",
                 event.msg.team_id[0], static_cast<unsigned long>(event.msg.key_id));
    }
    else
    {
        ESP_LOGW(kTag, "keydist passphrase derive FAILED -- forwarding unchanged");
    }
    real_sink_.onTeamPairingKeyDist(rewritten);
}

} // namespace platform::esp::idf_common::team_infra
