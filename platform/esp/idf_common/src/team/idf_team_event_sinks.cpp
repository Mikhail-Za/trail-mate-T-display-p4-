#include "platform/esp/idf_common/team/idf_team_event_sinks.h"

#include "esp_log.h"

#include "sys/event_bus.h"
#include "team/protocol/team_position.h"

namespace platform::esp::idf_common::team_infra
{

// NOTE: These handlers are a direct port of LinuxTeamEventBusSink /
// LinuxTeamPairingEventQueue in
// platform/linux/common/src/app/linux_app_services.cpp. Keep them in sync: the
// team UI consumes the published sys::*Event types unchanged across platforms.

void IdfTeamEventBusSink::onTeamKick(const ::team::TeamKickEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamKickEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamTransferLeader(const ::team::TeamTransferLeaderEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamTransferLeaderEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamKeyDist(const ::team::TeamKeyDistEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamKeyDistEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamKeyRequest(const ::team::TeamKeyRequestEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamKeyRequestEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamStatus(const ::team::TeamStatusEvent& event)
{
    // RX trace for the on-air re-test: a status frame from a peer is what keeps
    // this unit's view of that peer "online" (the team page reducer touch-updates
    // ctx.from's last_seen). A members-listed status is the leader's roster; a
    // no-roster one is the peer's presence keepalive (tickTeamPresence()).
    ESP_LOGI("idf-team",
             "RX status from=%08lX key_id=%lu members=%u (peer presence/roster)",
             static_cast<unsigned long>(event.ctx.from),
             static_cast<unsigned long>(event.msg.key_id),
             static_cast<unsigned>(event.msg.has_members ? event.msg.members.size()
                                                         : 0U));
    ::sys::EventBus::publish(new ::sys::TeamStatusEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamPosition(const ::team::TeamPositionEvent& event)
{
    // Republish the decoded position as a NodePositionUpdateEvent so the shared
    // team-member location renders on the map / contact list, then forward the
    // raw TeamPositionEvent to the team UI. Identical to the Linux sink.
    ::team::proto::TeamPositionMessage pos{};
    if (event.ctx.from != 0 &&
        ::team::proto::decodeTeamPositionMessage(event.payload.data(),
                                                 event.payload.size(),
                                                 &pos))
    {
        const uint32_t timestamp = (pos.ts != 0) ? pos.ts : event.ctx.timestamp;
        ::sys::EventBus::publish(
            new ::sys::NodePositionUpdateEvent(
                event.ctx.from,
                pos.lat_e7,
                pos.lon_e7,
                ::team::proto::teamPositionHasAltitude(pos),
                ::team::proto::teamPositionHasAltitude(pos) ? pos.alt_m : 0,
                timestamp,
                0,
                0,
                0,
                0,
                0),
            0);
    }

    ::sys::EventBus::publish(new ::sys::TeamPositionEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamWaypoint(const ::team::TeamWaypointEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamWaypointEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamTrack(const ::team::TeamTrackEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamTrackEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamChat(const ::team::TeamChatEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamChatEvent(event), 0);
}

void IdfTeamEventBusSink::onTeamError(const ::team::TeamErrorEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamErrorEvent(event), 0);
}

void IdfTeamPairingEventQueue::onTeamPairingStateChanged(const ::team::TeamPairingEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamPairingEvent(event), 0);
}

void IdfTeamPairingEventQueue::onTeamPairingKeyDist(const ::team::TeamKeyDistEvent& event)
{
    // The member's pairing -> team handoff: this keydist is what the team page
    // reducer turns into in_team=true + setKeysFromPsk(), ending the "Scanning"
    // screen. Trace it so the on-air capture shows the member actually establishing
    // the team (not just the pairing handshake completing).
    ESP_LOGI("idf-team",
             "RX/keydist (pairing) from=%08lX key_id=%lu psk_len=%u -> establish team",
             static_cast<unsigned long>(event.ctx.from),
             static_cast<unsigned long>(event.msg.key_id),
             static_cast<unsigned>(event.msg.channel_psk_len));
    ::sys::EventBus::publish(new ::sys::TeamKeyDistEvent(event), 0);
}

} // namespace platform::esp::idf_common::team_infra
