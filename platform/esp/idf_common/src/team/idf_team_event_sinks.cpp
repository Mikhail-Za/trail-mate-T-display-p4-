#include "platform/esp/idf_common/team/idf_team_event_sinks.h"

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
    ::sys::EventBus::publish(new ::sys::TeamKeyDistEvent(event), 0);
}

} // namespace platform::esp::idf_common::team_infra
