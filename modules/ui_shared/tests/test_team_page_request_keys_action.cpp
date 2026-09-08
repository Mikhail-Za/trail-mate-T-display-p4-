#include "ui/screens/team/team_page_request_keys_action.h"

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

team::ui::TeamPageCommandState makeEligibleMemberState()
{
    team::ui::TeamPageCommandState state;
    state.in_team = true;
    state.self_is_leader = false;
    state.has_team_id = true;
    state.team_id = testTeamId();
    state.security_round = 7;
    return state;
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

    bool sendKeyDist(const team::proto::TeamKeyDist&,
                     chat::ChannelId,
                     chat::NodeId) override
    {
        return true;
    }

    bool sendKeyDistPlain(const team::proto::TeamKeyDist&,
                          chat::ChannelId,
                          chat::NodeId) override
    {
        return true;
    }

    bool sendKeyRequest(const team::proto::TeamKeyRequest& request,
                        chat::ChannelId,
                        chat::NodeId dest) override
    {
        sent_key_request_count += 1;
        last_request = request;
        last_dest = dest;
        return send_ok;
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
    int sent_key_request_count = 0;
    team::proto::TeamKeyRequest last_request{};
    chat::NodeId last_dest = 0;
};

void testEligibleMemberRecoveryIsUnavailable()
{
    auto state = makeEligibleMemberState();
    FakeController controller;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, nullptr);

    const auto effects =
        team::ui::TeamPageRequestKeysAction().requestKeys(
            state,
            runtime,
            0x22222222);

    assert(effects.accepted);
    assert(!effects.ignored);
    assert(!effects.sent_request);
    assert(effects.send_failed);
    assert(effects.error == team::TeamService::SendError::SecurityUnavailable);
    assert(controller.sent_key_request_count == 0);
}

void testUnavailableDoesNotDependOnControllerResult()
{
    auto state = makeEligibleMemberState();
    FakeController controller;
    controller.send_ok = false;
    controller.error = team::TeamService::SendError::MeshSendFail;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, nullptr);

    const auto effects =
        team::ui::TeamPageRequestKeysAction().requestKeys(
            state,
            runtime,
            0x22222222);

    assert(effects.accepted);
    assert(!effects.sent_request);
    assert(effects.send_failed);
    assert(effects.error == team::TeamService::SendError::SecurityUnavailable);
    assert(controller.sent_key_request_count == 0);
}

void testMissingRuntimeIsReportedAsSendFailure()
{
    auto state = makeEligibleMemberState();
    team::ui::TeamPageRuntimePort runtime(nullptr, nullptr, nullptr);

    const auto effects =
        team::ui::TeamPageRequestKeysAction().requestKeys(
            state,
            runtime,
            0x22222222);

    assert(effects.accepted);
    assert(!effects.sent_request);
    assert(effects.send_failed);
    assert(effects.error == team::TeamService::SendError::SecurityUnavailable);
}

void testInvalidStatesAreIgnored()
{
    FakeController controller;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, nullptr);

    auto state = makeEligibleMemberState();
    state.in_team = false;
    auto effects =
        team::ui::TeamPageRequestKeysAction().requestKeys(
            state,
            runtime,
            0x22222222);
    assert(!effects.accepted);
    assert(effects.ignored);
    assert(!effects.sent_request);
    assert(controller.sent_key_request_count == 0);

    state = makeEligibleMemberState();
    state.self_is_leader = true;
    effects = team::ui::TeamPageRequestKeysAction().requestKeys(
        state,
        runtime,
        0x22222222);
    assert(!effects.accepted);
    assert(effects.ignored);
    assert(!effects.sent_request);

    state = makeEligibleMemberState();
    state.has_team_id = false;
    effects = team::ui::TeamPageRequestKeysAction().requestKeys(
        state,
        runtime,
        0x22222222);
    assert(!effects.accepted);
    assert(effects.ignored);
    assert(!effects.sent_request);

    state = makeEligibleMemberState();
    effects = team::ui::TeamPageRequestKeysAction().requestKeys(
        state,
        runtime,
        0);
    assert(!effects.accepted);
    assert(effects.ignored);
    assert(!effects.sent_request);
}

} // namespace

int main()
{
    testEligibleMemberRecoveryIsUnavailable();
    testUnavailableDoesNotDependOnControllerResult();
    testMissingRuntimeIsReportedAsSendFailure();
    testInvalidStatesAreIgnored();
    return 0;
}
