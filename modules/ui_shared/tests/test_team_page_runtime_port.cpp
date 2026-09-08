#include "ui/screens/team/team_page_runtime_port.h"

#include <cassert>

namespace
{

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0x42;
    return id;
}

class FakeController final : public team::ui::ITeamPageControllerPort
{
  public:
    void clearKeys() override { clear_keys_count += 1; }
    void resetUiState() override { reset_ui_count += 1; }

    bool setKeysFromPsk(const team::TeamId& team_id,
                        uint32_t key_id,
                        const uint8_t*,
                        size_t psk_len) override
    {
        last_team_id = team_id;
        last_key_id = key_id;
        last_psk_len = psk_len;
        return set_keys_ok;
    }

    bool sendKick(const team::proto::TeamKick& kick,
                  chat::ChannelId,
                  chat::NodeId dest) override
    {
        last_kick_target = kick.target;
        last_dest = dest;
        return send_ok;
    }

    bool sendTransferLeader(const team::proto::TeamTransferLeader& transfer,
                            chat::ChannelId,
                            chat::NodeId dest) override
    {
        last_transfer_target = transfer.target;
        last_dest = dest;
        return send_ok;
    }

    bool sendKeyDist(const team::proto::TeamKeyDist& key_dist,
                     chat::ChannelId,
                     chat::NodeId dest) override
    {
        last_key_id = key_dist.key_id;
        last_dest = dest;
        return send_ok;
    }

    bool sendKeyDistPlain(const team::proto::TeamKeyDist& key_dist,
                          chat::ChannelId,
                          chat::NodeId dest) override
    {
        last_key_id = key_dist.key_id;
        last_dest = dest;
        return send_ok;
    }

    bool sendKeyRequest(const team::proto::TeamKeyRequest& request,
                        chat::ChannelId,
                        chat::NodeId dest) override
    {
        last_key_request_current_key_id = request.current_key_id;
        last_key_request_requester_id = request.requester_id;
        last_dest = dest;
        return send_ok;
    }

    bool sendStatus(const team::proto::TeamStatus& status,
                    chat::ChannelId,
                    chat::NodeId dest) override
    {
        last_status_key_id = status.key_id;
        last_dest = dest;
        return send_ok;
    }

    bool sendStatusPlain(const team::proto::TeamStatus& status,
                         chat::ChannelId,
                         chat::NodeId dest) override
    {
        last_status_key_id = status.key_id;
        last_dest = dest;
        return send_plain_ok;
    }

    team::TeamService::SendError lastSendError() const override
    {
        return error;
    }

    int clear_keys_count = 0;
    int reset_ui_count = 0;
    bool set_keys_ok = true;
    bool send_ok = true;
    bool send_plain_ok = true;
    team::TeamService::SendError error =
        team::TeamService::SendError::None;
    team::TeamId last_team_id{};
    uint32_t last_key_id = 0;
    uint32_t last_psk_len = 0;
    uint32_t last_kick_target = 0;
    uint32_t last_transfer_target = 0;
    uint32_t last_status_key_id = 0;
    uint32_t last_key_request_current_key_id = 0;
    uint32_t last_key_request_requester_id = 0;
    chat::NodeId last_dest = 0;
};

class FakePairing final : public team::ui::ITeamPagePairingPort
{
  public:
    bool startLeader(const team::TeamId& team_id,
                     uint32_t key_id,
                     const uint8_t*,
                     size_t psk_len,
                     uint32_t leader_id,
                     const char* team_name) override
    {
        last_team_id = team_id;
        last_key_id = key_id;
        last_psk_len = psk_len;
        last_leader_id = leader_id;
        last_team_name = team_name ? team_name : "";
        return start_ok;
    }

    bool startMember(uint32_t self_id) override
    {
        last_self_id = self_id;
        return start_ok;
    }

    void stop() override { stop_count += 1; }

    team::TeamPairingStatus status() const override
    {
        return pairing_status;
    }

    bool start_ok = true;
    int stop_count = 0;
    team::TeamPairingStatus pairing_status;
    team::TeamId last_team_id{};
    uint32_t last_key_id = 0;
    size_t last_psk_len = 0;
    uint32_t last_leader_id = 0;
    uint32_t last_self_id = 0;
    std::string last_team_name;
};

class FakeKeyStore final : public team::ui::ITeamPageKeyStorePort
{
  public:
    bool saveKeysNow(
        const team::TeamId& team_id,
        uint32_t key_id,
        const std::array<uint8_t, team::proto::kTeamChannelPskSize>& psk) override
    {
        last_team_id = team_id;
        last_key_id = key_id;
        last_psk0 = psk[0];
        return save_ok;
    }

    bool save_ok = true;
    team::TeamId last_team_id{};
    uint32_t last_key_id = 0;
    uint8_t last_psk0 = 0;
};

void testPortDelegatesControllerAndPairing()
{
    FakeController controller;
    FakePairing pairing;
    FakeKeyStore key_store;
    team::ui::TeamPageRuntimePort port(&controller, &pairing, &key_store);

    const auto id = testTeamId();
    std::array<uint8_t, team::proto::kTeamChannelPskSize> psk{};
    psk[0] = 0xA5;

    assert(port.hasController());
    assert(port.hasPairing());
    port.clearKeys();
    port.resetControllerUi();
    port.stopPairing();
    assert(controller.clear_keys_count == 1);
    assert(controller.reset_ui_count == 1);
    assert(pairing.stop_count == 1);

    assert(port.setKeysFromPsk(id, 7, psk.data(), psk.size()));
    assert(controller.last_key_id == 7);
    assert(controller.last_psk_len == psk.size());
    assert(port.saveKeysNow(id, 8, psk));
    assert(key_store.last_key_id == 8);
    assert(key_store.last_psk0 == 0xA5);

    assert(port.startLeader(id, 9, psk.data(), psk.size(), 0x11111111, "Trail"));
    assert(pairing.last_leader_id == 0x11111111);
    assert(pairing.last_team_name == "Trail");
    assert(port.startMember(0x22222222));
    assert(pairing.last_self_id == 0x22222222);
}

void testPortDelegatesSendsAndErrors()
{
    // Fake success here verifies port wiring only; action/service tests enforce refusal.
    FakeController controller;
    team::ui::TeamPageRuntimePort port(&controller, nullptr, nullptr);

    team::proto::TeamKick kick;
    kick.target = 0x11111111;
    assert(port.sendKick(kick, chat::ChannelId::PRIMARY, 0x22222222));
    assert(controller.last_kick_target == 0x11111111);
    assert(controller.last_dest == 0x22222222);

    team::proto::TeamTransferLeader transfer;
    transfer.target = 0x33333333;
    assert(port.sendTransferLeader(transfer, chat::ChannelId::PRIMARY));
    assert(controller.last_transfer_target == 0x33333333);

    team::proto::TeamKeyDist key_dist;
    key_dist.key_id = 12;
    assert(port.sendKeyDist(key_dist, chat::ChannelId::PRIMARY, 0x44444444));
    assert(controller.last_key_id == 12);
    assert(port.sendKeyDistPlain(key_dist, chat::ChannelId::PRIMARY, 0x44444444));

    team::proto::TeamKeyRequest key_request;
    key_request.current_key_id = 11;
    key_request.requester_id = 0x55555555;
    assert(port.sendKeyRequest(key_request, chat::ChannelId::PRIMARY, 0));
    assert(controller.last_key_request_current_key_id == 11);
    assert(controller.last_key_request_requester_id == 0x55555555);

    team::proto::TeamStatus status;
    status.key_id = 13;
    assert(port.sendStatus(status, chat::ChannelId::PRIMARY));
    assert(port.sendStatusPlain(status, chat::ChannelId::PRIMARY));
    assert(controller.last_status_key_id == 13);

    controller.error = team::TeamService::SendError::MeshSendFail;
    assert(port.lastSendError() == team::TeamService::SendError::MeshSendFail);
}

void testMissingPortsFailSafely()
{
    team::ui::TeamPageRuntimePort port(nullptr, nullptr, nullptr);
    team::TeamId id{};
    std::array<uint8_t, team::proto::kTeamChannelPskSize> psk{};

    assert(!port.hasController());
    assert(!port.hasPairing());
    port.clearKeys();
    port.resetControllerUi();
    port.stopPairing();
    assert(!port.setKeysFromPsk(id, 1, psk.data(), psk.size()));
    assert(!port.saveKeysNow(id, 1, psk));
    assert(!port.startMember(1));
    team::proto::TeamKeyRequest key_request;
    assert(!port.sendKeyRequest(key_request, chat::ChannelId::PRIMARY, 0));
    assert(port.pairingStatus().state == team::TeamPairingState::Idle);
    assert(port.lastSendError() == team::TeamService::SendError::None);
}

} // namespace

int main()
{
    testPortDelegatesControllerAndPairing();
    testPortDelegatesSendsAndErrors();
    testMissingPortsFailSafely();
    return 0;
}
