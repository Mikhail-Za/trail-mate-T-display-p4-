// IDF BleManager implementation, C6-backed, with the Meshtastic phone-session pump.
//
// On T-Display P4 / Tab5 the BLE radio lives on the ESP32-C6 companion, reached over
// HostLink/SDIO via WirelessCompanion. The C6 runs the GATT server; this P4-side service
// bridges the reusable Meshtastic phone-session (ToRadio/FromRadio protocol pump) onto the
// HostLink BLE channel:
//   - phone writes ToRadio  -> C6 -> BLE_UPLINK -> onBleUplink() -> session.handleToRadio()
//   - session emits FromRadio -> popToPhone() -> sendBleDownlink() -> C6 notifies the phone
// The C6 owns the from_num / FromRadio-notify semantics (tm_ble.c), so the P4 just pushes
// each FromRadio frame. MeshCore + Trail-Mate profiles and Wi-Fi are follow-on work.
#include "ble/ble_manager.h"

#include "app/app_config.h"
#include "ble/app_phone_facade.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/module_config.pb.h"
#include "phone/meshtastic/meshtastic_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_session.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"

#include <memory>

namespace ble
{
namespace
{
namespace wc = platform::esp::idf_common::wireless_companion;

// Bound on frames drained per update() so a burst can't monopolize the loop.
constexpr int kMaxDrainPerUpdate = 16;

class C6BleService final : public BleService,
                          public wc::WirelessUplinkSink,
                          public phone::meshtastic::MeshtasticPhoneTransport
{
  public:
    explicit C6BleService(app::IAppBleFacade& ctx)
        : ctx_(ctx), phone_facade_(ctx, ble_config_, module_config_, nullptr)
    {
    }

    ~C6BleService() override
    {
        C6BleService::stop();
    }

    bool start() override
    {
        wc::WirelessCompanion& companion = wc::c6_companion();
        (void)companion.begin(); // idempotent; SDIO handshake normally already ran at boot
        companion.setUplinkSink(this);
        mt_session_.reset(new phone::meshtastic::MeshtasticPhoneSession(
            phone_facade_, *this, &phone_facade_, &phone_facade_, &phone_facade_,
            &phone_facade_, &phone_facade_, &phone_facade_));
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
        mt_session_.reset();
        connected_ = false;
    }

    void update() override
    {
        wc::c6_companion().poll(); // drains C6 uplink frames -> onBleUplink()/onBleEvent()
        if (mt_session_)
        {
            mt_session_->pumpIncomingAppData();
            drainToPhone();
        }
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

    // ---- MeshtasticPhoneTransport ----
    bool isBleConnected() const override
    {
        return connected_;
    }
    void notifyFromNum(uint32_t from_num) override
    {
        // The C6 raises the FromNum / FromRadio notify itself when we push a downlink,
        // so there is nothing to do here; we drain the session queue in update().
        (void)from_num;
    }

    // ---- WirelessUplinkSink ----
    void onBleUplink(wc::BleProfile profile, uint8_t connection_id,
                     const uint8_t* data, size_t len) override
    {
        last_connection_ = connection_id;
        if (profile == wc::BleProfile::Meshtastic && mt_session_ && data != nullptr)
        {
            mt_session_->handleToRadio(data, len);
        }
        // MeshCore / Trail-Mate uplink routing is follow-on work.
    }

    void onBleEvent(wc::BleProfile profile, uint8_t event_kind,
                    uint8_t connection_id, uint16_t mtu, uint16_t error_code) override
    {
        (void)profile;
        (void)mtu;
        (void)error_code;
        // Mirrors tm_c6_ble_event_kind: 3 = CONNECTED, 4 = DISCONNECTED.
        if (event_kind == 3u)
        {
            connected_ = true;
            last_connection_ = connection_id;
        }
        else if (event_kind == 4u)
        {
            connected_ = false;
            if (mt_session_)
            {
                mt_session_->close();
            }
        }
    }

  private:
    void drainToPhone()
    {
        phone::meshtastic::MeshtasticBleFrame frame;
        for (int i = 0; i < kMaxDrainPerUpdate && mt_session_->popToPhone(&frame); ++i)
        {
            wc::c6_companion().sendBleDownlink(wc::BleProfile::Meshtastic, last_connection_,
                                               frame.buf, frame.len);
        }
    }

    app::IAppBleFacade& ctx_;
    // Declared before phone_facade_: it holds references to these two configs.
    meshtastic_Config_BluetoothConfig ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    AppPhoneFacade phone_facade_;
    std::unique_ptr<phone::meshtastic::MeshtasticPhoneSession> mt_session_;
    bool started_ = false;
    bool connected_ = false;
    uint8_t last_connection_ = 0;
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
