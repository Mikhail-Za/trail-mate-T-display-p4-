// IDF BleManager implementation, C6-backed.
//
// On the T-Display P4 / Tab5 the BLE radio is not local: it lives on the ESP32-C6
// companion and is reached over HostLink/SDIO through WirelessCompanion. This file
// replaces the former no-op stub. Enabling BLE now creates a C6-backed BleService
// that registers for uplink frames, tracks connection state, and drives the
// companion poll from BleManager::update(). The C6 is already told to advertise the
// enabled BLE profiles during the HostLink CONFIG_SET handshake, so "start" here is
// lifecycle + routing, not a radio bring-up.
//
// Still to do (Stage 2b): pump the per-profile phone sessions -- route ToRadio/
// FromRadio (Meshtastic), MeshCore, and Trail-Mate GATT payloads between the mesh
// adapter and the C6 BLE channels via onBleUplink() / sendBleDownlink(). That is the
// large, best-tested-on-hardware piece; the seam is in place below (onBleUplink).
#include "ble/ble_manager.h"

#include "app/app_config.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"

#include <memory>

namespace ble
{
namespace
{
namespace wc = platform::esp::idf_common::wireless_companion;

class C6BleService final : public BleService, public wc::WirelessUplinkSink
{
  public:
    explicit C6BleService(app::IAppBleFacade& ctx) : ctx_(ctx) {}

    ~C6BleService() override
    {
        C6BleService::stop();
    }

    bool start() override
    {
        wc::WirelessCompanion& companion = wc::c6_companion();
        // Idempotent: the SDIO handshake normally already ran at boot; this just
        // ensures it and (re)registers us as the uplink sink.
        (void)companion.begin();
        companion.setUplinkSink(this);
        started_ = true;
        return companion.isPresent();
    }

    void stop() override
    {
        if (started_)
        {
            wc::c6_companion().setUplinkSink(nullptr);
            started_ = false;
        }
        connected_ = false;
    }

    void update() override
    {
        // Drains any pending BLE/ESP-NOW uplink frames from the C6 and dispatches
        // them to this sink.
        wc::c6_companion().poll();
    }

    bool getPairingStatus(BlePairingStatus* out) const override
    {
        if (out == nullptr)
        {
            return false;
        }
        const wc::C6CompanionStatus status = wc::c6_companion().status();
        *out = BlePairingStatus{};
        out->available = status.present;
        out->is_connected = connected_;
        return status.present;
    }

    // ---- WirelessUplinkSink: data + events travelling up from the C6 ----

    void onBleUplink(wc::BleProfile profile, uint8_t connection_id,
                     const uint8_t* data, size_t len) override
    {
        // Stage 2b seam: hand this to the per-profile phone session and on into
        // ctx_.getMeshAdapter(). Today we account for it so the uplink path is
        // exercised and observable without pretending the pump exists yet.
        (void)profile;
        (void)connection_id;
        (void)data;
        (void)len;
        ++uplink_frames_;
    }

    void onBleEvent(wc::BleProfile profile, uint8_t event_kind,
                    uint8_t connection_id, uint16_t mtu, uint16_t error_code) override
    {
        (void)profile;
        (void)connection_id;
        (void)mtu;
        (void)error_code;
        // Mirrors tm_c6_ble_event_kind: 3 = CONNECTED, 4 = DISCONNECTED.
        if (event_kind == 3u)
        {
            connected_ = true;
        }
        else if (event_kind == 4u)
        {
            connected_ = false;
        }
    }

  private:
    app::IAppBleFacade& ctx_;
    bool started_ = false;
    bool connected_ = false;
    uint32_t uplink_frames_ = 0;
};

} // namespace

BleManager::BleManager(app::IAppBleFacade& ctx)
    : ctx_(ctx), active_protocol_(ctx.getConfig().mesh_protocol)
{
}

BleManager::~BleManager() = default;

void BleManager::begin() {}

void BleManager::setEnabled(bool enabled)
{
    if (enabled)
    {
        if (!service_)
        {
            restartService(active_protocol_);
        }
        return;
    }
    service_.reset();
    nimble_initialized_ = false;
}

void BleManager::update()
{
    if (service_)
    {
        service_->update();
    }
}

void BleManager::applyProtocol(chat::MeshProtocol protocol)
{
    // The C6 advertises every enabled profile simultaneously, so a protocol switch
    // needs no BLE teardown; just record the active protocol.
    active_protocol_ = protocol;
}

bool BleManager::getPairingStatus(BlePairingStatus* out) const
{
    if (!out)
    {
        return false;
    }
    if (service_)
    {
        return service_->getPairingStatus(out);
    }
    *out = BlePairingStatus{};
    return false;
}

void BleManager::restartService(chat::MeshProtocol protocol)
{
    active_protocol_ = protocol;
    service_.reset();
    auto service = std::make_unique<C6BleService>(ctx_);
    service->start();
    service_ = std::move(service);
}

void BleManager::shutdownNimble()
{
    nimble_initialized_ = false;
    service_.reset();
}

std::string BleManager::buildDeviceName(chat::MeshProtocol protocol) const
{
    (void)protocol;
    return {};
}

} // namespace ble
