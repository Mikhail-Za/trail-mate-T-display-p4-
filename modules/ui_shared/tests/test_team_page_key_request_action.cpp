#include "ui/screens/team/team_page_key_request_action.h"

#include <cassert>

namespace
{

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0x42;
    id[1] = 0x24;
    return id;
}

team::ui::TeamPageCommandState makeLeaderState()
{
    team::ui::TeamPageCommandState state;
    state.in_team = true;
    state.self_is_leader = true;
    state.has_team_id = true;
    state.team_id = testTeamId();
    state.security_round = 9;
    state.has_team_psk = true;
    state.team_psk[0] = 0xA5;
    return state;
}

team::TeamKeyRequestEvent makeRequest()
{
    team::TeamKeyRequestEvent event;
    event.ctx.team_id = testTeamId();
    event.ctx.key_id = 7;
    event.ctx.from = 0x22222222;
    event.msg.team_id = testTeamId();
    event.msg.current_key_id = 7;
    event.msg.requester_id = 0x22222222;
    return event;
}

class FakeController final : public team::ui::ITeamPageControllerPort
{
  public:
    void clearKeys() override {}
    void resetUiState() override {}

    bool setKeysFromPsk(const team::TeamId&,
                        uint32_t,
                        const uint8_t*,
                        size_t) override
    {
        return true;
    }

    bool sendKick(const team::proto::TeamKick&,
                  chat::ChannelId,
                  chat::NodeId) override
    {
        return true;
    }

    bool sendTransferLeader(const team::proto::TeamTransferLeader&,
                            chat::ChannelId,
                            chat::NodeId) override
    {
        return true;
    }

    bool sendKeyDist(const team::proto::TeamKeyDist& key_dist,
                     chat::ChannelId,
                     chat::NodeId dest) override
    {
        sent_keydist_count += 1;
        last_keydist = key_dist;
        last_dest = dest;
        return send_ok;
    }

    bool sendKeyDistPlain(const team::proto::TeamKeyDist&,
                          chat::ChannelId,
                          chat::NodeId) override
    {
        return true;
    }

    bool sendKeyRequest(const team::proto::TeamKeyRequest&,
                        chat::ChannelId,
                        chat::NodeId) override
    {
        return true;
    }

    bool sendStatus(const team::proto::TeamStatus&,
                    chat::ChannelId,
                    chat::NodeId) override
    {
        return true;
    }

    bool sendStatusPlain(const team::proto::TeamStatus&,
                         chat::ChannelId,
                         chat::NodeId) override
    {
        return true;
    }

    team::TeamService::SendError lastSendError() const override
    {
        return error;
    }

    bool send_ok = true;
    team::TeamService::SendError error =
        team::TeamService::SendError::None;
    int sent_keydist_count = 0;
    team::proto::TeamKeyDist last_keydist{};
    chat::NodeId last_dest = 0;
};

void testLeaderRecoveryResponseIsUnavailable()
{
    auto state = makeLeaderState();
    auto request = makeRequest();
    FakeController controller;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, nullptr);

    const auto effects = team::ui::TeamPageKeyRequestAction().handleRequest(
        state,
        request,
        runtime);

    assert(effects.accepted);
    assert(!effects.sent_keydist);
    assert(effects.failures.size() == 1);
    assert(effects.failures[0].kind ==
           team::ui::TeamPageKeyRequestFailureKind::SendFailedDetail);
    assert(effects.failures[0].error ==
           team::TeamService::SendError::SecurityUnavailable);
    assert(controller.sent_keydist_count == 0);
    assert(state.team_psk[0] == 0xA5);
}

void testFallbackRequesterIsUnavailableWithoutRuntime()
{
    auto state = makeLeaderState();
    auto request = makeRequest();
    request.msg.requester_id = 0;
    team::ui::TeamPageRuntimePort runtime(nullptr, nullptr, nullptr);

    const auto effects = team::ui::TeamPageKeyRequestAction().handleRequest(
        state,
        request,
        runtime);

    assert(effects.accepted);
    assert(!effects.sent_keydist);
    assert(effects.failures.size() == 1);
    assert(effects.failures[0].error ==
           team::TeamService::SendError::SecurityUnavailable);
}

void testInvalidRequestsAreIgnored()
{
    FakeController controller;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, nullptr);

    auto state = makeLeaderState();
    state.self_is_leader = false;
    auto effects = team::ui::TeamPageKeyRequestAction().handleRequest(
        state,
        makeRequest(),
        runtime);
    assert(!effects.accepted);
    assert(!effects.failures.empty());
    assert(controller.sent_keydist_count == 0);

    state = makeLeaderState();
    state.has_team_psk = false;
    effects = team::ui::TeamPageKeyRequestAction().handleRequest(
        state,
        makeRequest(),
        runtime);
    assert(!effects.accepted);
    assert(!effects.failures.empty());

    state = makeLeaderState();
    auto request = makeRequest();
    request.msg.team_id[0] = 0x99;
    effects = team::ui::TeamPageKeyRequestAction().handleRequest(
        state,
        request,
        runtime);
    assert(!effects.accepted);
    assert(!effects.failures.empty());
}

} // namespace

int main()
{
    testLeaderRecoveryResponseIsUnavailable();
    testFallbackRequesterIsUnavailableWithoutRuntime();
    testInvalidRequestsAreIgnored();
    return 0;
}
