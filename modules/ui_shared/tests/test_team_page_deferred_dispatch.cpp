#include "ui/screens/team/team_page_deferred_dispatch.h"

#include <cassert>

namespace
{

class FakeDispatchPort final
    : public team::ui::ITeamPageDeferredDispatchPort
{
  public:
    bool hasController() const override { return has_controller; }

    bool sendKeyDistPlain(const team::proto::TeamKeyDist& key_dist,
                          chat::ChannelId,
                          chat::NodeId dest) override
    {
        keydist_send_count += 1;
        last_keydist = key_dist;
        last_keydist_dest = dest;
        return keydist_ok;
    }

    bool sendStatus(const team::proto::TeamStatus& status,
                    chat::ChannelId,
                    chat::NodeId dest) override
    {
        status_send_count += 1;
        last_status = status;
        last_status_dest = dest;
        return status_ok;
    }

    bool sendStatusPlain(const team::proto::TeamStatus& status,
                         chat::ChannelId,
                         chat::NodeId dest) override
    {
        status_plain_send_count += 1;
        last_status = status;
        last_status_plain_dest = dest;
        return status_plain_ok;
    }

    team::TeamService::SendError lastSendError() const override
    {
        return error;
    }

    bool has_controller = true;
    bool keydist_ok = true;
    bool status_ok = true;
    bool status_plain_ok = true;
    team::TeamService::SendError error =
        team::TeamService::SendError::None;
    int keydist_send_count = 0;
    int status_send_count = 0;
    int status_plain_send_count = 0;
    chat::NodeId last_keydist_dest = 0;
    chat::NodeId last_status_dest = 1;
    chat::NodeId last_status_plain_dest = 1;
    team::proto::TeamKeyDist last_keydist;
    team::proto::TeamStatus last_status;
};

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0xCA;
    id[1] = 0xFE;
    return id;
}

team::ui::TeamPageDeferredDispatchState readyState()
{
    team::ui::TeamPageDeferredDispatchState state;
    state.in_team = true;
    state.has_team_id = true;
    state.self_is_leader = true;
    state.team_id = testTeamId();
    state.security_round = 12;
    state.has_team_psk = true;
    state.team_psk[0] = 0x42;
    return state;
}

team::ui::TeamPageDeferredDispatchQueue makeQueue()
{
    team::ui::TeamPageDeferredDispatchConfig config;
    config.keydist_max_retries = 2;
    config.keydist_retry_interval_s = 5;
    config.status_rebroadcast_interval_s = 2;
    return team::ui::TeamPageDeferredDispatchQueue(config);
}

void testKeyDistRetriesAreClearedAsUnavailable(bool runtime_present)
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    port.has_controller = runtime_present;

    queue.enqueueKeyDist(0x22222222, 13, 100);
    queue.enqueueKeyDist(0x22222222, 13, 100);
    assert(queue.keyDistPendingCount() == 1);

    const auto psk_before = state.team_psk;
    const auto effects = queue.processKeyDistRetries(state, port, 0);
    assert(!effects.sent_keydist);
    assert(port.keydist_send_count == 0);
    assert(queue.keyDistPendingCount() == 0);
    assert(effects.failures.size() == 1);
    assert(effects.failures[0].action ==
           team::ui::TeamPageDeferredDispatchAction::KeyDist);
    assert(effects.failures[0].kind ==
           team::ui::TeamPageDeferredDispatchFailureKind::SendFailedDetail);
    assert(effects.failures[0].error ==
           team::TeamService::SendError::SecurityUnavailable);
    assert(state.team_psk == psk_before);
}

void testStatusBroadcastSendsAndReschedules()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    team::proto::TeamStatus status;
    status.key_id = 12;
    status.members = {0x11111111, 0x22222222};
    status.has_members = true;
    status.leader_id = 0x11111111;

    queue.scheduleStatusBroadcast(2, 100, 3);
    assert(queue.statusBroadcastPendingCount() == 1);

    auto effects = queue.processStatusBroadcasts(state, status, port, 102);
    assert(!effects.sent_status);
    assert(port.status_send_count == 0);

    effects = queue.processStatusBroadcasts(state, status, port, 103);
    assert(effects.sent_status);
    assert(port.status_send_count == 1);
    assert(port.status_plain_send_count == 1);
    assert(port.last_status.key_id == 12);
    assert(queue.statusBroadcastPendingCount() == 1);

    effects = queue.processStatusBroadcasts(state, status, port, 105);
    assert(effects.sent_status);
    assert(port.status_send_count == 2);
    assert(port.status_plain_send_count == 2);
    assert(queue.statusBroadcastPendingCount() == 0);
}

void testStatusBroadcastFailureAndInvalidLeaderState()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    team::proto::TeamStatus status;
    status.key_id = 3;
    port.status_ok = false;
    port.status_plain_ok = false;

    queue.scheduleStatusBroadcast(1, 0, 0);
    auto effects = queue.processStatusBroadcasts(state, status, port, 0);

    assert(effects.sent_status);
    assert(effects.failures.size() == 2);
    assert(effects.failures[0].action ==
           team::ui::TeamPageDeferredDispatchAction::Status);
    assert(effects.failures[0].needs_keys);
    assert(!effects.failures[1].needs_keys);

    queue.scheduleStatusBroadcast(1, 10, 0);
    state.self_is_leader = false;
    effects = queue.processStatusBroadcasts(state, status, port, 10);
    assert(!effects.sent_status);
    assert(queue.statusBroadcastPendingCount() == 0);
}

} // namespace

int main()
{
    testKeyDistRetriesAreClearedAsUnavailable(true);
    testKeyDistRetriesAreClearedAsUnavailable(false);
    testStatusBroadcastSendsAndReschedules();
    testStatusBroadcastFailureAndInvalidLeaderState();
    return 0;
}
