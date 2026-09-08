#include "ui/screens/team/team_page_deferred_dispatch.h"

#include <algorithm>

namespace team
{
namespace ui
{
namespace
{

TeamPageDeferredDispatchFailure makeFailure(
    TeamPageDeferredDispatchAction action,
    TeamPageDeferredDispatchFailureKind kind,
    bool needs_keys,
    team::TeamService::SendError error = team::TeamService::SendError::None)
{
    TeamPageDeferredDispatchFailure failure;
    failure.action = action;
    failure.kind = kind;
    failure.needs_keys = needs_keys;
    failure.error = error;
    return failure;
}

} // namespace

TeamPageDeferredDispatchQueue::TeamPageDeferredDispatchQueue(
    TeamPageDeferredDispatchConfig config)
    : config_(config)
{
}

void TeamPageDeferredDispatchQueue::clearAll()
{
    clearKeyDist();
    clearStatusBroadcasts();
}

void TeamPageDeferredDispatchQueue::clearKeyDist()
{
    keydist_pending_.clear();
}

void TeamPageDeferredDispatchQueue::clearStatusBroadcasts()
{
    status_pending_.clear();
}

void TeamPageDeferredDispatchQueue::enqueueKeyDist(uint32_t node_id,
                                                   uint32_t key_id,
                                                   uint32_t now_s)
{
    for (const auto& item : keydist_pending_)
    {
        if (item.node_id == node_id && item.key_id == key_id)
        {
            return;
        }
    }

    KeyDistPending item;
    item.node_id = node_id;
    item.key_id = key_id;
    item.next_retry_s = now_s + config_.keydist_retry_interval_s;
    keydist_pending_.push_back(item);
}

void TeamPageDeferredDispatchQueue::confirmKeyDist(uint32_t node_id,
                                                   uint32_t key_id)
{
    keydist_pending_.erase(
        std::remove_if(keydist_pending_.begin(),
                       keydist_pending_.end(),
                       [&](const KeyDistPending& item)
                       {
                           return item.node_id == node_id &&
                                  item.key_id == key_id;
                       }),
        keydist_pending_.end());
}

void TeamPageDeferredDispatchQueue::scheduleStatusBroadcast(uint8_t repeats,
                                                            uint32_t now_s,
                                                            uint32_t delay_s)
{
    if (repeats == 0)
    {
        return;
    }

    StatusBroadcastPending item;
    item.next_send_s = now_s + delay_s;
    item.remaining = repeats;
    status_pending_.push_back(item);
}

TeamPageDeferredDispatchEffects
TeamPageDeferredDispatchQueue::processKeyDistRetries(
    const TeamPageDeferredDispatchState& state,
    ITeamPageDeferredDispatchPort& port,
    uint32_t now_s)
{
    (void)state;
    (void)port;
    (void)now_s;

    TeamPageDeferredDispatchEffects effects;
    if (keydist_pending_.empty())
    {
        return effects;
    }
    clearKeyDist();
    effects.failures.push_back(makeFailure(
        TeamPageDeferredDispatchAction::KeyDist,
        TeamPageDeferredDispatchFailureKind::SendFailedDetail,
        false,
        team::TeamService::SendError::SecurityUnavailable));
    return effects;
}

TeamPageDeferredDispatchEffects
TeamPageDeferredDispatchQueue::processStatusBroadcasts(
    const TeamPageDeferredDispatchState& state,
    const team::proto::TeamStatus& status,
    ITeamPageDeferredDispatchPort& port,
    uint32_t now_s)
{
    TeamPageDeferredDispatchEffects effects;
    if (status_pending_.empty())
    {
        return effects;
    }

    if (!state.in_team || !state.has_team_id || !state.self_is_leader)
    {
        clearStatusBroadcasts();
        return effects;
    }

    if (!port.hasController())
    {
        return effects;
    }

    for (auto it = status_pending_.begin(); it != status_pending_.end();)
    {
        if (now_s < it->next_send_s)
        {
            ++it;
            continue;
        }

        if (!port.sendStatus(status, chat::ChannelId::PRIMARY, 0))
        {
            effects.failures.push_back(makeFailure(
                TeamPageDeferredDispatchAction::Status,
                TeamPageDeferredDispatchFailureKind::SendFailed,
                true));
        }
        effects.sent_status = true;

        if (!port.sendStatusPlain(status, chat::ChannelId::PRIMARY, 0))
        {
            effects.failures.push_back(makeFailure(
                TeamPageDeferredDispatchAction::Status,
                TeamPageDeferredDispatchFailureKind::SendFailed,
                false));
        }
        effects.sent_status = true;

        if (it->remaining > 0)
        {
            it->remaining -= 1;
        }
        if (it->remaining == 0)
        {
            it = status_pending_.erase(it);
        }
        else
        {
            it->next_send_s = now_s + config_.status_rebroadcast_interval_s;
            ++it;
        }
    }

    return effects;
}

size_t TeamPageDeferredDispatchQueue::keyDistPendingCount() const
{
    return keydist_pending_.size();
}

size_t TeamPageDeferredDispatchQueue::statusBroadcastPendingCount() const
{
    return status_pending_.size();
}

} // namespace ui
} // namespace team
