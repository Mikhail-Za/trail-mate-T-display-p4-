#include "ui/screens/team/team_page_request_keys_action.h"

#include "chat/domain/chat_types.h"

namespace team
{
namespace ui
{

TeamPageRequestKeysEffects TeamPageRequestKeysAction::requestKeys(
    const TeamPageCommandState& state,
    const TeamPageRuntimePort& runtime,
    uint32_t self_node_id) const
{
    (void)runtime;

    TeamPageRequestKeysEffects effects;
    if (!state.in_team || state.self_is_leader || !state.has_team_id ||
        self_node_id == 0)
    {
        effects.ignored = true;
        return effects;
    }

    effects.accepted = true;
    effects.send_failed = true;
    effects.error = team::TeamService::SendError::SecurityUnavailable;
    return effects;
}

} // namespace ui
} // namespace team
