#include "ui/screens/team/team_page_read_model.h"

#include "platform/ui/team_ui_snapshot_store.h"
#include "ui/team_presence/team_presence_model.h"

#include <cstdio>

namespace team
{
namespace ui
{

TeamPageReadModel::TeamPageReadModel(uint32_t now_s)
    : now_s_(now_s)
{
}

TeamPageReadModelInput TeamPageReadModel::inputFromSnapshot(
    const TeamUiSnapshot& snapshot)
{
    TeamPageReadModelInput input;
    input.in_team = snapshot.in_team;
    input.pending_join = snapshot.pending_join;
    input.kicked_out = snapshot.kicked_out;
    input.self_is_leader = snapshot.self_is_leader;
    input.team_id = snapshot.team_id;
    input.has_team_id = snapshot.has_team_id;
    input.team_name = snapshot.team_name;
    input.security_round = snapshot.security_round;
    input.last_update_s = snapshot.last_update_s;
    input.has_team_psk = snapshot.has_team_psk;
    input.members = snapshot.members;
    return input;
}

std::string TeamPageReadModel::formatTeamNameFromId(const TeamId& id)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "TEAM-%02X%02X", id[0], id[1]);
    return std::string(buf);
}

TeamPageSummaryView TeamPageReadModel::buildSummary(
    const TeamPageReadModelInput& input) const
{
    TeamPageSummaryView summary;
    summary.in_team = input.in_team;
    summary.pending_join = input.pending_join;
    summary.kicked_out = input.kicked_out;
    summary.self_is_leader = input.self_is_leader;
    summary.waiting_new_keys = input.waiting_new_keys;
    summary.member_count = input.members.size();
    summary.security_round = input.security_round;
    summary.has_security_round = input.security_round != 0;
    summary.last_update = buildLastUpdate(input.last_update_s);

    if (!input.team_name.empty())
    {
        summary.team_name = input.team_name;
    }
    else if (input.has_team_id)
    {
        summary.team_name = formatTeamNameFromId(input.team_id);
    }
    else
    {
        summary.team_name = "Unknown";
    }

    for (const auto& member : input.members)
    {
        if (::ui::team_presence::isTeamMemberOnline(now_s_,
                                                    member.last_seen_s))
        {
            ++summary.online_count;
        }
    }

    return summary;
}

TeamJoinPendingView TeamPageReadModel::buildJoinPending(
    const TeamPageReadModelInput& input) const
{
    TeamJoinPendingView view;
    view.target_team_name = input.pairing_team_name;

    if (input.pairing_role == TeamPairingRole::Leader)
    {
        view.title = "Pairing (Leader)";
        view.show_leader_members = true;
    }
    else if (input.pairing_role == TeamPairingRole::Member)
    {
        view.title = "Pairing (Member)";
    }

    switch (input.pairing_state)
    {
    case TeamPairingState::LeaderBeacon:
        view.state_line = "Waiting for member...";
        break;
    case TeamPairingState::MemberScanning:
        view.state_line = "Scanning for team...";
        break;
    case TeamPairingState::JoinSent:
        view.state_line = "Join request sent...";
        break;
    case TeamPairingState::WaitingKey:
        view.state_line = "Waiting for keys...";
        break;
    case TeamPairingState::Completed:
        view.state_line = "Paired successfully";
        break;
    case TeamPairingState::Failed:
        view.state_line = "Pairing failed";
        break;
    default:
        break;
    }
    return view;
}

std::vector<TeamMemberRowView> TeamPageReadModel::buildMemberRows(
    const std::vector<TeamMemberUi>& members) const
{
    std::vector<TeamMemberRowView> rows;
    rows.reserve(members.size());
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        rows.push_back(buildMemberRow(members[i], i));
    }
    return rows;
}

TeamMemberDetailView TeamPageReadModel::buildSelectedMember(
    const TeamPageReadModelInput& input) const
{
    TeamMemberDetailView detail;
    if (input.selected_member_index < 0 ||
        static_cast<std::size_t>(input.selected_member_index) >= input.members.size())
    {
        return detail;
    }

    const auto index = static_cast<std::size_t>(input.selected_member_index);
    detail.valid = true;
    detail.member = buildMemberRow(input.members[index], index);
    detail.last_seen = buildLastSeen(input.members[index].last_seen_s);
    detail.management_actions_enabled =
        input.self_is_leader &&
        input.has_team_psk &&
        input.has_team_id &&
        input.security_round > 0 &&
        !input.waiting_new_keys;
    return detail;
}

TeamRelativeTimeView TeamPageReadModel::buildLastSeen(uint32_t last_seen_s) const
{
    TeamRelativeTimeView view;
    if (last_seen_s == 0)
    {
        return view;
    }
    if (::ui::team_presence::isTeamMemberOnline(now_s_, last_seen_s))
    {
        view.kind = TeamRelativeTimeKind::Online;
        return view;
    }

    const uint32_t age = now_s_ - last_seen_s;
    if (age < 3600)
    {
        view.kind = TeamRelativeTimeKind::MinutesAgo;
        view.value = age / 60;
        return view;
    }

    view.kind = TeamRelativeTimeKind::HoursAgo;
    view.value = age / 3600;
    return view;
}

TeamRelativeTimeView TeamPageReadModel::buildLastUpdate(uint32_t last_update_s) const
{
    TeamRelativeTimeView view;
    if (last_update_s == 0)
    {
        return view;
    }
    view.kind = TeamRelativeTimeKind::SecondsAgo;
    if (now_s_ > last_update_s)
    {
        view.value = now_s_ - last_update_s;
    }
    return view;
}

TeamMemberRowView TeamPageReadModel::buildMemberRow(
    const TeamMemberUi& member,
    std::size_t source_index) const
{
    TeamMemberRowView row;
    row.source_index = source_index;
    row.node_id = member.node_id;
    row.name = member.name;
    row.online = ::ui::team_presence::isTeamMemberOnline(now_s_,
                                                         member.last_seen_s);
    row.leader = member.leader;
    row.color_index = member.color_index;
    return row;
}

} // namespace ui
} // namespace team
