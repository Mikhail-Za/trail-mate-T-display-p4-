#include "ui/screens/team/team_page_kick_confirm_action.h"

namespace team
{
namespace ui
{
namespace
{

TeamPageKickConfirmFailure makeFailureDetail(
    TeamPageKickConfirmFailureAction action,
    team::TeamService::SendError error)
{
    TeamPageKickConfirmFailure failure;
    failure.action = action;
    failure.kind = TeamPageKickConfirmFailureKind::SendFailedDetail;
    failure.error = error;
    return failure;
}

} // namespace

TeamPageKickConfirmEffects TeamPageKickConfirmAction::confirmKick(
    TeamPageCommandState& state,
    const TeamPageCommandReducer& reducer,
    const TeamPageRuntimePort& runtime,
    ITeamPageKickConfirmRandom& random,
    ITeamPageKickConfirmDeferred& deferred,
    uint32_t self_node_id) const
{
    (void)reducer;
    (void)runtime;
    (void)random;
    (void)deferred;
    (void)self_node_id;

    TeamPageKickConfirmEffects effects;
    if (!state.self_is_leader)
    {
        return effects;
    }
    const int idx = state.selected_member_index;
    if (idx < 0 || idx >= static_cast<int>(state.members.size()))
    {
        return effects;
    }

    effects.failures.push_back(makeFailureDetail(
        TeamPageKickConfirmFailureAction::Kick,
        team::TeamService::SendError::SecurityUnavailable));
    return effects;
}

} // namespace ui
} // namespace team
