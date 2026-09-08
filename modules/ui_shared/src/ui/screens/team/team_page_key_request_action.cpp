#include "ui/screens/team/team_page_key_request_action.h"

#include "chat/domain/chat_types.h"

namespace team
{
namespace ui
{
namespace
{

TeamPageKeyRequestFailure makeFailure(
    TeamPageKeyRequestFailureKind kind,
    team::TeamService::SendError error = team::TeamService::SendError::None)
{
    TeamPageKeyRequestFailure failure;
    failure.kind = kind;
    failure.error = error;
    return failure;
}

} // namespace

TeamPageKeyRequestEffects TeamPageKeyRequestAction::handleRequest(
    const TeamPageCommandState& state,
    const team::TeamKeyRequestEvent& event,
    const TeamPageRuntimePort& runtime) const
{
    (void)runtime;

    TeamPageKeyRequestEffects effects;
    const uint32_t requester =
        event.msg.requester_id != 0 ? event.msg.requester_id : event.ctx.from;

    if (!state.in_team || !state.self_is_leader || !state.has_team_id ||
        !state.has_team_psk || requester == 0 ||
        event.msg.team_id != state.team_id)
    {
        effects.failures.push_back(
            makeFailure(TeamPageKeyRequestFailureKind::Ignored));
        return effects;
    }

    effects.accepted = true;
    effects.failures.push_back(makeFailure(
        TeamPageKeyRequestFailureKind::SendFailedDetail,
        team::TeamService::SendError::SecurityUnavailable));
    return effects;
}

} // namespace ui
} // namespace team
