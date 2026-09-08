#include "ui/screens/team/team_page_kick_confirm_action.h"

#include <cassert>

namespace
{

team::ui::TeamMemberUi makeMember(uint32_t node_id, bool leader = false)
{
    team::ui::TeamMemberUi member;
    member.node_id = node_id;
    member.leader = leader;
    return member;
}

team::ui::TeamPageCommandState makeKickState()
{
    team::ui::TeamPageCommandState state;
    state.in_team = true;
    state.self_is_leader = true;
    state.has_team_id = true;
    state.team_id[0] = 0xCA;
    state.security_round = 7;
    state.has_team_psk = true;
    state.team_psk[0] = 0xA5;
    state.waiting_new_keys = true;
    state.selected_member_index = 1;
    state.members.push_back(makeMember(0, true));
    state.members.push_back(makeMember(0x22222222));
    state.members.push_back(makeMember(0x33333333));
    return state;
}

team::ui::TeamPageCommandReducer makeReducer()
{
    team::ui::TeamPageCommandContext context;
    context.self_node_id = 0x11111111;
    return team::ui::TeamPageCommandReducer(context);
}

class FakeRandom final : public team::ui::ITeamPageKickConfirmRandom
{
  public:
    uint8_t nextByte() override
    {
        calls += 1;
        return 0x80;
    }
    int calls = 0;
};

class FakeDeferred final : public team::ui::ITeamPageKickConfirmDeferred
{
  public:
    void enqueueKeyDist(uint32_t, uint32_t) override { calls += 1; }
    int calls = 0;
};

class FakeController final : public team::ui::ITeamPageControllerPort
{
  public:
    void clearKeys() override { calls += 1; }
    void resetUiState() override { calls += 1; }
    bool setKeysFromPsk(const team::TeamId&, uint32_t, const uint8_t*, size_t) override
    {
        calls += 1;
        return true;
    }
    bool sendKick(const team::proto::TeamKick&, chat::ChannelId, chat::NodeId) override
    {
        calls += 1;
        return true;
    }
    bool sendTransferLeader(const team::proto::TeamTransferLeader&, chat::ChannelId, chat::NodeId) override
    {
        calls += 1;
        return true;
    }
    bool sendKeyDist(const team::proto::TeamKeyDist&, chat::ChannelId, chat::NodeId) override
    {
        calls += 1;
        return true;
    }
    bool sendKeyDistPlain(const team::proto::TeamKeyDist&, chat::ChannelId, chat::NodeId) override
    {
        calls += 1;
        return true;
    }
    bool sendKeyRequest(const team::proto::TeamKeyRequest&, chat::ChannelId, chat::NodeId) override
    {
        calls += 1;
        return true;
    }
    bool sendStatus(const team::proto::TeamStatus&, chat::ChannelId, chat::NodeId) override
    {
        calls += 1;
        return true;
    }
    bool sendStatusPlain(const team::proto::TeamStatus&, chat::ChannelId, chat::NodeId) override
    {
        calls += 1;
        return true;
    }
    team::TeamService::SendError lastSendError() const override
    {
        return team::TeamService::SendError::None;
    }
    int calls = 0;
};

void assertStateUnchanged(const team::ui::TeamPageCommandState& state,
                          int selected_index)
{
    assert(state.security_round == 7);
    assert(state.has_team_psk && state.team_psk[0] == 0xA5);
    assert(state.waiting_new_keys);
    assert(state.selected_member_index == selected_index);
    assert(state.members.size() == 3);
    assert(state.members[1].node_id == 0x22222222);
    assert(state.members[2].node_id == 0x33333333);
}

void testValidKickIsRefusedWithoutSideEffects(bool runtime_present)
{
    auto state = makeKickState();
    auto reducer = makeReducer();
    FakeController controller;
    team::ui::TeamPageRuntimePort runtime(
        runtime_present ? &controller : nullptr, nullptr, nullptr);
    FakeRandom random;
    FakeDeferred deferred;

    const auto effects = team::ui::TeamPageKickConfirmAction().confirmKick(
        state, reducer, runtime, random, deferred, 0x11111111);

    assert(!effects.accepted && !effects.command.accepted);
    assert(!effects.sent_kick && !effects.sent_keydist);
    assert(!effects.enqueued_keydist && !effects.applied_keys);
    assert(!effects.sent_status);
    assert(effects.failures.size() == 1);
    assert(effects.failures[0].action ==
           team::ui::TeamPageKickConfirmFailureAction::Kick);
    assert(effects.failures[0].kind ==
           team::ui::TeamPageKickConfirmFailureKind::SendFailedDetail);
    assert(effects.failures[0].error ==
           team::TeamService::SendError::SecurityUnavailable);
    assert(controller.calls == 0 && random.calls == 0 && deferred.calls == 0);
    assertStateUnchanged(state, 1);
}

void testInvalidKickIsSilentWithoutSideEffects(bool runtime_present)
{
    auto state = makeKickState();
    state.selected_member_index = 9;
    auto reducer = makeReducer();
    FakeController controller;
    team::ui::TeamPageRuntimePort runtime(
        runtime_present ? &controller : nullptr, nullptr, nullptr);
    FakeRandom random;
    FakeDeferred deferred;

    const auto effects = team::ui::TeamPageKickConfirmAction().confirmKick(
        state, reducer, runtime, random, deferred, 0x11111111);
    assert(!effects.accepted && effects.failures.empty());
    assert(controller.calls == 0 && random.calls == 0 && deferred.calls == 0);
    assertStateUnchanged(state, 9);

    state = makeKickState();
    state.self_is_leader = false;
    const auto non_leader = team::ui::TeamPageKickConfirmAction().confirmKick(
        state, reducer, runtime, random, deferred, 0x11111111);
    assert(!non_leader.accepted && non_leader.failures.empty());
    assertStateUnchanged(state, 1);
}

} // namespace

int main()
{
    testValidKickIsRefusedWithoutSideEffects(true);
    testValidKickIsRefusedWithoutSideEffects(false);
    testInvalidKickIsSilentWithoutSideEffects(true);
    testInvalidKickIsSilentWithoutSideEffects(false);
    return 0;
}
