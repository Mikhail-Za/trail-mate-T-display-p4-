#include "ui/screens/team/team_page_transfer_leader_action.h"

namespace team
{
namespace ui
{
namespace
{

TeamPageTransferLeaderFailure makeFailure(
    TeamPageTransferLeaderFailureAction action,
    bool needs_keys)
{
    TeamPageTransferLeaderFailure failure;
    failure.action = action;
    failure.kind = TeamPageTransferLeaderFailureKind::SendFailed;
    failure.needs_keys = needs_keys;
    return failure;
}

} // namespace

TeamPageTransferLeaderEffects
TeamPageTransferLeaderAction::transferLeader(
    TeamPageCommandState& state,
    TeamPageKeyEventState& key_event_state,
    const TeamPageCommandReducer& reducer,
    const TeamPageRuntimePort& runtime,
    const TeamPageKeyEventLog& key_log) const
{
    TeamPageTransferLeaderEffects effects;
    if (!state.self_is_leader)
    {
        return effects;
    }
    const int idx = state.selected_member_index;
    if (idx < 0 || idx >= static_cast<int>(state.members.size()))
    {
        return effects;
    }

    const uint32_t target = state.members[static_cast<size_t>(idx)].node_id;
    if (runtime.hasController())
    {
        team::proto::TeamTransferLeader transfer{};
        transfer.target = target;
        effects.sent_transfer = true;
        if (!runtime.sendTransferLeader(
                transfer,
                chat::ChannelId::PRIMARY,
                0))
        {
            effects.failures.push_back(makeFailure(
                TeamPageTransferLeaderFailureAction::Transfer,
                true));
        }
    }

    effects.command = reducer.reduceTransferLeader(state, target);
    effects.accepted = effects.command.accepted;
    if (!effects.command.accepted)
    {
        return effects;
    }

    if (effects.command.leader_transferred_key_event)
    {
        effects.appended_key_event =
            key_log.appendLeaderTransferred(key_event_state,
                                            effects.command.leader_target);
    }

    return effects;
}

} // namespace ui
} // namespace team
