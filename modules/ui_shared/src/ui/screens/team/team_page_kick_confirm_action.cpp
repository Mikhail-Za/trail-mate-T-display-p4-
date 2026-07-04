#include "ui/screens/team/team_page_kick_confirm_action.h"

#include <algorithm>

namespace team
{
namespace ui
{
namespace
{

TeamPageKickConfirmFailure makeFailure(
    TeamPageKickConfirmFailureAction action,
    bool needs_keys)
{
    TeamPageKickConfirmFailure failure;
    failure.action = action;
    failure.kind = TeamPageKickConfirmFailureKind::SendFailed;
    failure.needs_keys = needs_keys;
    return failure;
}

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

bool containsMember(const team::proto::TeamStatus& status, uint32_t id)
{
    return std::find(status.members.begin(), status.members.end(), id) !=
           status.members.end();
}

void fillStatusMembers(const TeamPageCommandState& state,
                       team::proto::TeamStatus& status,
                       uint32_t self_node_id)
{
    status.members.clear();
    status.leader_id = 0;

    for (const auto& member : state.members)
    {
        const uint32_t id =
            (member.node_id == 0) ? self_node_id : member.node_id;
        if (id == 0)
        {
            continue;
        }
        if (!containsMember(status, id))
        {
            status.members.push_back(id);
        }
        if (member.leader)
        {
            status.leader_id = id;
        }
    }

    if (status.leader_id == 0 && state.self_is_leader &&
        self_node_id != 0)
    {
        status.leader_id = self_node_id;
    }
    status.has_members = !status.members.empty();
}

uint32_t nextKickKeyId(uint32_t security_round)
{
    uint32_t old_key_id = security_round;
    if (old_key_id == 0)
    {
        old_key_id = 1;
    }
    return old_key_id + 1;
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

    const uint32_t kick_target =
        state.members[static_cast<size_t>(idx)].node_id;
    TeamPageKickRotation rotation;

    if (runtime.hasController())
    {
        team::proto::TeamKick kick{};
        kick.target = kick_target;
        effects.sent_kick = true;
        if (!runtime.sendKick(kick, chat::ChannelId::PRIMARY, 0))
        {
            effects.failures.push_back(
                makeFailure(TeamPageKickConfirmFailureAction::Kick, true));
        }

        const uint32_t new_key_id = nextKickKeyId(state.security_round);
        std::array<uint8_t, team::proto::kTeamChannelPskSize> new_psk{};
        for (size_t i = 0; i < new_psk.size(); ++i)
        {
            new_psk[i] = random.nextByte();
        }

        team::proto::TeamKeyDist key_dist{};
        key_dist.team_id = state.team_id;
        key_dist.key_id = new_key_id;
        key_dist.channel_psk_len = static_cast<uint8_t>(new_psk.size());
        key_dist.channel_psk = new_psk;

        for (const auto& member : state.members)
        {
            if (member.node_id == 0 || member.node_id == kick_target)
            {
                continue;
            }
            effects.sent_keydist = true;
            if (!runtime.sendKeyDist(
                    key_dist,
                    chat::ChannelId::PRIMARY,
                    member.node_id))
            {
                effects.failures.push_back(makeFailureDetail(
                    TeamPageKickConfirmFailureAction::KeyDist,
                    runtime.lastSendError()));
            }
            deferred.enqueueKeyDist(member.node_id, new_key_id);
            effects.enqueued_keydist = true;
        }

        rotation.rotate_keys = true;
        rotation.key_id = new_key_id;
        rotation.team_psk = new_psk;
    }

    effects.command =
        reducer.reduceKickConfirmed(state, kick_target, rotation);
    effects.accepted = effects.command.accepted;
    if (!effects.command.accepted)
    {
        return effects;
    }

    if (effects.command.keys_changed)
    {
        effects.applied_keys = true;
        if (!runtime.setKeysFromPsk(state.team_id,
                                    state.security_round,
                                    state.team_psk.data(),
                                    state.team_psk.size()))
        {
            effects.failures.push_back(
                makeFailure(TeamPageKickConfirmFailureAction::Keys, true));
        }
    }

    if (runtime.hasController() && effects.command.status_should_send)
    {
        team::proto::TeamStatus status{};
        status.key_id = state.security_round;
        fillStatusMembers(state, status, self_node_id);

        effects.sent_status = true;
        if (!runtime.sendStatus(status, chat::ChannelId::PRIMARY, 0))
        {
            effects.failures.push_back(
                makeFailure(TeamPageKickConfirmFailureAction::Status, true));
        }
        if (!runtime.sendStatusPlain(status, chat::ChannelId::PRIMARY, 0))
        {
            effects.failures.push_back(
                makeFailure(TeamPageKickConfirmFailureAction::Status, false));
        }
    }

    return effects;
}

} // namespace ui
} // namespace team
