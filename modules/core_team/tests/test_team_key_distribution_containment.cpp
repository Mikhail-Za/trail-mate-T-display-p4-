#include "chat/ports/i_mesh_adapter.h"
#include "team/ports/i_team_crypto.h"
#include "team/ports/i_team_event_sink.h"
#include "team/ports/i_team_runtime.h"
#include "team/protocol/team_mgmt.h"
#include "team/protocol/team_portnum.h"
#include "team/protocol/team_wire.h"
#include "team/usecase/team_service.h"

#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace
{

class FakeMesh final : public chat::IMeshAdapter
{
  public:
    chat::MeshCapabilities getCapabilities() const override
    {
        chat::MeshCapabilities caps{};
        caps.supports_unicast_appdata = true;
        caps.supports_broadcast_appdata = false;
        return caps;
    }

    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override
    {
        return true;
    }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId, uint32_t portnum, const uint8_t*, size_t,
                     chat::NodeId dest, bool, chat::MessageId, bool) override
    {
        ++send_count;
        sent_ports.push_back(portnum);
        sent_destinations.push_back(dest);
        return true;
    }
    bool pollIncomingData(chat::MeshIncomingData* out) override
    {
        if (incoming.empty()) return false;
        *out = incoming.front();
        incoming.pop_front();
        return true;
    }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t& out_len, size_t) override
    {
        out_len = 0;
        return false;
    }
    chat::NodeId getNodeId() const override { return 0x11111111; }

    std::deque<chat::MeshIncomingData> incoming;
    int send_count = 0;
    std::vector<uint32_t> sent_ports;
    std::vector<chat::NodeId> sent_destinations;
};

class FakeCrypto final : public team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key, size_t key_len, const char* info,
                   uint8_t* out, size_t out_len) override
    {
        ++derive_count;
        derive_inputs.emplace_back(key, key + key_len);
        derive_infos.emplace_back(info);
        for (size_t i = 0; i < out_len; ++i)
        {
            out[i] = static_cast<uint8_t>(key[i % key_len] ^ info[5]);
        }
        return true;
    }

    bool aeadEncrypt(const uint8_t* key, size_t key_len,
                     const uint8_t*, size_t, const uint8_t*, size_t,
                     const uint8_t* plain, size_t plain_len,
                     std::vector<uint8_t>& out_cipher) override
    {
        ++encrypt_count;
        last_encrypt_key.assign(key, key + key_len);
        out_cipher.assign(plain, plain + plain_len);
        return true;
    }

    bool aeadDecrypt(const uint8_t* key, size_t key_len,
                     const uint8_t*, size_t, const uint8_t*, size_t,
                     const uint8_t* cipher, size_t cipher_len,
                     std::vector<uint8_t>& out_plain) override
    {
        ++decrypt_count;
        last_decrypt_key.assign(key, key + key_len);
        out_plain.assign(cipher, cipher + cipher_len);
        return true;
    }

    int derive_count = 0;
    int encrypt_count = 0;
    int decrypt_count = 0;
    std::vector<std::vector<uint8_t>> derive_inputs;
    std::vector<std::string> derive_infos;
    std::vector<uint8_t> last_encrypt_key;
    std::vector<uint8_t> last_decrypt_key;
};

class FakeRuntime final : public team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override { return 100; }
    uint32_t nowUnixSeconds() override { return 200; }
    void fillRandomBytes(uint8_t* out, size_t len) override
    {
        ++random_count;
        std::memset(out, 0xA5, len);
    }
    int random_count = 0;
};

class FakeSink final : public team::ITeamEventSink
{
  public:
    void onTeamKick(const team::TeamKickEvent&) override { ++kick_count; }
    void onTeamTransferLeader(const team::TeamTransferLeaderEvent&) override { ++transfer_count; }
    void onTeamKeyDist(const team::TeamKeyDistEvent&) override { ++key_dist_count; }
    void onTeamKeyRequest(const team::TeamKeyRequestEvent&) override { ++key_request_count; }
    void onTeamStatus(const team::TeamStatusEvent&) override { ++status_count; }
    void onTeamPosition(const team::TeamPositionEvent&) override {}
    void onTeamWaypoint(const team::TeamWaypointEvent&) override {}
    void onTeamTrack(const team::TeamTrackEvent&) override {}
    void onTeamChat(const team::TeamChatEvent&) override { ++chat_count; }
    void onTeamError(const team::TeamErrorEvent&) override { ++error_count; }

    int kick_count = 0;
    int transfer_count = 0;
    int key_dist_count = 0;
    int key_request_count = 0;
    int status_count = 0;
    int chat_count = 0;
    int error_count = 0;
};

class CountingObserver final : public team::TeamService::IncomingDataObserver
{
  public:
    void onIncomingData(const chat::MeshIncomingData&) override { ++count; }
    int count = 0;
};

team::TeamId teamId(uint8_t marker)
{
    team::TeamId id{};
    id[0] = marker;
    return id;
}

team::proto::TeamKeyDist keyDist(const team::TeamId& id)
{
    team::proto::TeamKeyDist msg{};
    msg.team_id = id;
    msg.key_id = 99;
    msg.channel_psk_len = 4;
    msg.channel_psk[0] = 0xDE;
    msg.channel_psk[1] = 0xAD;
    msg.channel_psk[2] = 0xBE;
    msg.channel_psk[3] = 0xEF;
    return msg;
}

team::proto::TeamKeyRequest keyRequest(const team::TeamId& id)
{
    team::proto::TeamKeyRequest msg{};
    msg.team_id = id;
    msg.current_key_id = 7;
    msg.requester_id = 0x22222222;
    return msg;
}

std::vector<uint8_t> managementWire(team::proto::TeamMgmtType type,
                                    const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> wire;
    assert(team::proto::encodeTeamMgmtMessage(type, payload, wire));
    return wire;
}

std::vector<uint8_t> keyDistWire(const team::TeamId& id)
{
    std::vector<uint8_t> payload;
    assert(team::proto::encodeTeamKeyDist(keyDist(id), payload));
    return managementWire(team::proto::TeamMgmtType::KeyDist, payload);
}

std::vector<uint8_t> keyRequestWire(const team::TeamId& id)
{
    std::vector<uint8_t> payload;
    assert(team::proto::encodeTeamKeyRequest(keyRequest(id), payload));
    return managementWire(team::proto::TeamMgmtType::KeyRequest, payload);
}

std::vector<uint8_t> encryptedWire(const team::TeamId& envelope_team,
                                   uint32_t key_id,
                                   std::vector<uint8_t> plain)
{
    team::proto::TeamEncrypted envelope{};
    envelope.team_id = envelope_team;
    envelope.key_id = key_id;
    envelope.sender_id = 0x22222222;
    envelope.ciphertext = std::move(plain);
    std::vector<uint8_t> wire;
    assert(team::proto::encodeTeamEncrypted(envelope, wire));
    return wire;
}

chat::MeshIncomingData incoming(std::vector<uint8_t> payload)
{
    chat::MeshIncomingData data{};
    data.portnum = team::proto::TEAM_MGMT_APP;
    data.from = 0x22222222;
    data.payload = std::move(payload);
    return data;
}

std::vector<uint8_t> expectedDerived(const std::array<uint8_t, 4>& psk, char discriminator)
{
    std::vector<uint8_t> key(team::kTeamKeySize);
    for (size_t i = 0; i < key.size(); ++i)
    {
        key[i] = static_cast<uint8_t>(psk[i % psk.size()] ^ discriminator);
    }
    return key;
}

void assertOutboundRefused(team::TeamService& service,
                           FakeCrypto& crypto, FakeMesh& mesh, FakeRuntime& runtime)
{
    const auto dist = keyDist(teamId(0x10));
    const auto request = keyRequest(teamId(0x10));
    const int derives = crypto.derive_count;
    const int encrypts = crypto.encrypt_count;
    const int decrypts = crypto.decrypt_count;
    const int sends = mesh.send_count;
    const int randoms = runtime.random_count;

    assert(!service.sendKeyDist(dist, chat::ChannelId::PRIMARY, 0x22222222, true, true));
    assert(service.getLastSendError() == team::TeamService::SendError::SecurityUnavailable);
    assert(!service.sendKeyDistPlain(dist, chat::ChannelId::PRIMARY, 0x22222222, true, true));
    assert(service.getLastSendError() == team::TeamService::SendError::SecurityUnavailable);
    assert(!service.sendKeyRequest(request, chat::ChannelId::PRIMARY, 0x22222222, true, true));
    assert(service.getLastSendError() == team::TeamService::SendError::SecurityUnavailable);

    assert(crypto.derive_count == derives);
    assert(crypto.encrypt_count == encrypts);
    assert(crypto.decrypt_count == decrypts);
    assert(mesh.send_count == sends);
    assert(runtime.random_count == randoms);
}

void testIngressContainmentAndOrdinaryTraffic()
{
    FakeCrypto crypto;
    FakeMesh mesh;
    FakeSink sink;
    FakeRuntime runtime;
    CountingObserver observer;
    team::TeamService service(crypto, mesh, sink, runtime);
    service.addIncomingDataObserver(&observer);

    const auto local_team = teamId(0x10);
    const auto foreign_team = teamId(0x20);

    mesh.incoming.push_back(incoming(keyDistWire(local_team)));
    mesh.incoming.push_back(incoming(keyRequestWire(local_team)));
    service.processIncoming();
    assert(observer.count == 2);
    assert(crypto.derive_count == 0);
    assert(sink.key_dist_count == 0);
    assert(sink.key_request_count == 0);
    assert(!service.hasKeys());

    const std::array<uint8_t, 4> psk{{0x11, 0x22, 0x33, 0x44}};
    assert(service.setKeysFromPsk(local_team, 7, psk.data(), psk.size()));
    assert(crypto.derive_count == 4);

    mesh.incoming.push_back(incoming(keyDistWire(local_team)));
    mesh.incoming.push_back(incoming(keyDistWire(foreign_team)));
    mesh.incoming.push_back(incoming(keyRequestWire(local_team)));
    mesh.incoming.push_back(incoming(managementWire(team::proto::TeamMgmtType::KeyDist,
                                                   {0xDE, 0xAD})));
    mesh.incoming.push_back(incoming(encryptedWire(local_team, 7, keyDistWire(local_team))));
    mesh.incoming.push_back(incoming(encryptedWire(local_team, 7, keyDistWire(foreign_team))));
    mesh.incoming.push_back(incoming(encryptedWire(foreign_team, 7, keyDistWire(local_team))));
    mesh.incoming.push_back(incoming(encryptedWire(local_team, 7, keyRequestWire(local_team))));
    mesh.incoming.push_back(incoming(encryptedWire(local_team, 7,
                                                  managementWire(team::proto::TeamMgmtType::KeyRequest,
                                                                 {0x01}))));
    mesh.incoming.push_back(incoming(keyDistWire(local_team)));
    service.processIncoming();

    assert(observer.count == 12);
    assert(crypto.derive_count == 4);
    assert(sink.key_dist_count == 0);
    assert(sink.key_request_count == 0);

    team::proto::TeamChatMessage chat{};
    chat.header.version = team::proto::kTeamChatVersion;
    chat.header.type = team::proto::TeamChatType::Text;
    chat.payload = {'o', 'k'};
    assert(service.sendChat(chat, chat::ChannelId::PRIMARY));
    assert(mesh.sent_ports.back() == team::proto::TEAM_CHAT_APP);
    assert(mesh.sent_destinations.back() == 0);
    assert(crypto.last_encrypt_key == expectedDerived(psk, 'c'));

    team::proto::TeamStatus status{};
    status.key_id = 7;
    assert(service.sendStatus(status, chat::ChannelId::PRIMARY));
    assert(mesh.sent_ports.back() == team::proto::TEAM_MGMT_APP);
    assert(crypto.last_encrypt_key == expectedDerived(psk, 'm'));

    assertOutboundRefused(service, crypto, mesh, runtime);
}

void testOutboundRefusalWithoutKeys()
{
    FakeCrypto crypto;
    FakeMesh mesh;
    FakeSink sink;
    FakeRuntime runtime;
    team::TeamService service(crypto, mesh, sink, runtime);
    assertOutboundRefused(service, crypto, mesh, runtime);
}

} // namespace

int main()
{
    testIngressContainmentAndOrdinaryTraffic();
    testOutboundRefusalWithoutKeys();
    return 0;
}
