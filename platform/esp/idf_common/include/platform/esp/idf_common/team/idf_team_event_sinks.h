/**
 * @file idf_team_event_sinks.h
 * @brief ESP-IDF team event sinks that publish onto sys::EventBus.
 *
 * Lifted from platform/linux/common/src/app/linux_app_services.cpp
 * (LinuxTeamEventBusSink + LinuxTeamPairingEventQueue). The team usecase emits
 * domain events through these ITeamEventSink / ITeamPairingEventSink ports; each
 * handler wraps the event in the matching sys::*Event and publishes it to the
 * global sys::EventBus, which the IdfChatFacade pump drains and routes to the
 * team UI (team_page_handle_event). The position handler also republishes a
 * NodePositionUpdateEvent so a team member's shared position lands on the map,
 * exactly as the Linux sink does.
 */

#pragma once

#include "team/ports/i_team_event_sink.h"
#include "team/ports/i_team_pairing_event_sink.h"

namespace platform::esp::idf_common::team_infra
{

class IdfTeamEventBusSink final : public ::team::ITeamEventSink
{
  public:
    void onTeamKick(const ::team::TeamKickEvent& event) override;
    void onTeamTransferLeader(const ::team::TeamTransferLeaderEvent& event) override;
    void onTeamKeyDist(const ::team::TeamKeyDistEvent& event) override;
    void onTeamKeyRequest(const ::team::TeamKeyRequestEvent& event) override;
    void onTeamStatus(const ::team::TeamStatusEvent& event) override;
    void onTeamPosition(const ::team::TeamPositionEvent& event) override;
    void onTeamWaypoint(const ::team::TeamWaypointEvent& event) override;
    void onTeamTrack(const ::team::TeamTrackEvent& event) override;
    void onTeamChat(const ::team::TeamChatEvent& event) override;
    void onTeamError(const ::team::TeamErrorEvent& event) override;
};

class IdfTeamPairingEventQueue final : public ::team::ITeamPairingEventSink
{
  public:
    void onTeamPairingStateChanged(const ::team::TeamPairingEvent& event) override;
    void onTeamPairingKeyDist(const ::team::TeamKeyDistEvent& event) override;
};

} // namespace platform::esp::idf_common::team_infra
