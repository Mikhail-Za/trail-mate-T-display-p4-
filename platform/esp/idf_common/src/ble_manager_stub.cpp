// IDF BleManager implementation, C6-backed, with the Meshtastic + MeshCore phone pumps.
//
// On T-Display P4 / Tab5 the BLE radio lives on the ESP32-C6 companion, reached over
// HostLink/SDIO via WirelessCompanion. The C6 runs the GATT servers; this P4-side service
// bridges the reusable phone-session cores onto the HostLink BLE channels:
//   phone write -> C6 -> BLE_UPLINK -> onBleUplink() -> session/core handleToRadio/handleRxFrame
//   core emits  -> popToPhone/popTxFrame -> sendBleDownlink() -> C6 notifies the phone
// The C6 owns the GATT notify/read semantics (tm_ble.c). Meshtastic uses MeshtasticPhoneSession;
// MeshCore uses MeshCorePhoneCore. Trail-Mate is a private profile with no P4-side protocol yet,
// so its uplink is accepted but not routed. Wi-Fi management is handled separately.
#include "ble/ble_manager.h"

#include "app/app_config.h"
#include "ble/app_phone_facade.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/module_config.pb.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_session.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"

#include <memory>
#include <string>

namespace ble
{
namespace
{
namespace wc = platform::esp::idf_common::wireless_companion;

// Frames drained per update(). Kept small to pace downlinks over SDIO and avoid
// flooding the C6's NimBLE notification queue in one burst; update() runs many
// times per second so sustained throughput stays high.
constexpr int kMaxDrainPerUpdate = 4;
// MeshCore BLE frames are capped at 172 bytes (NUS MTU budget), matching the Arduino path.
constexpr size_t kMeshCoreFrameMax = 172;

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
        mc_core_.reset(new phone::meshcore::MeshCorePhoneCore(phone_facade_, device_name_,
                                                             &phone_facade_));
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
        mc_core_.reset();
        connected_ = false;
    }

    void update() override
    {
        wc::c6_companion().poll(); // drains C6 uplink frames -> onBleUplink()/onBleEvent()
        if (mt_session_)
        {
            mt_session_->pumpIncomingAppData();
            drainMeshtastic();
        }
        if (mc_core_)
        {
            mc_core_->pumpIncomingAppData();
            drainMeshCore();
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
        // The C6 raises the FromNum / FromRadio notify itself when we push a downlink.
        (void)from_num;
    }

    // ---- WirelessUplinkSink ----
    void onBleUplink(wc::BleProfile profile, uint8_t connection_id,
                     const uint8_t* data, size_t len) override
    {
        last_connection_ = connection_id;
        if (data == nullptr)
        {
            return;
        }
        switch (profile)
        {
        case wc::BleProfile::Meshtastic:
            if (mt_session_)
            {
                mt_session_->handleToRadio(data, len);
            }
            break;
        case wc::BleProfile::MeshCore:
            if (mc_core_)
            {
                mc_core_->handleRxFrame(data, len);
            }
            break;
        case wc::BleProfile::TrailMate:
            // Private profile: no P4-side protocol yet. Accept and drop.
            break;
        }
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
            if (mc_core_)
            {
                mc_core_->reset();
            }
        }
    }

  private:
    // sendBleDownlink() returning false means the SDIO write failed -- the C6 did NOT
    // get the frame. popToPhone() is destructive, so on failure we HOLD the frame and
    // retry it next tick rather than dropping it; losing a config frame (e.g.
    // config_complete_id) would restart the phone's whole sync ("too many retries").
    void drainMeshtastic()
    {
        if (mt_pending_valid_)
        {
            if (!wc::c6_companion().sendBleDownlink(wc::BleProfile::Meshtastic, last_connection_,
                                                    mt_pending_.buf, mt_pending_.len))
            {
                return; // still failing; keep the held frame, retry next tick
            }
            mt_pending_valid_ = false;
        }
        for (int i = 0; i < kMaxDrainPerUpdate; ++i)
        {
            if (!mt_session_->popToPhone(&mt_pending_))
            {
                return;
            }
            if (!wc::c6_companion().sendBleDownlink(wc::BleProfile::Meshtastic, last_connection_,
                                                    mt_pending_.buf, mt_pending_.len))
            {
                mt_pending_valid_ = true; // hold the just-popped frame for retry
                return;
            }
        }
    }

    void drainMeshCore()
    {
        if (mc_pending_valid_)
        {
            if (!wc::c6_companion().sendBleDownlink(wc::BleProfile::MeshCore, last_connection_,
                                                    mc_pending_, mc_pending_len_))
            {
                return;
            }
            mc_pending_valid_ = false;
        }
        for (int i = 0; i < kMaxDrainPerUpdate; ++i)
        {
            mc_pending_len_ = 0;
            if (!mc_core_->popTxFrame(mc_pending_, &mc_pending_len_))
            {
                return;
            }
            if (!wc::c6_companion().sendBleDownlink(wc::BleProfile::MeshCore, last_connection_,
                                                    mc_pending_, mc_pending_len_))
            {
                mc_pending_valid_ = true;
                return;
            }
        }
    }

    app::IAppBleFacade& ctx_;
    // Declared before phone_facade_: it holds references to these two configs.
    meshtastic_Config_BluetoothConfig ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    AppPhoneFacade phone_facade_;
    std::string device_name_ = "TrailMate-C6";
    std::unique_ptr<phone::meshtastic::MeshtasticPhoneSession> mt_session_;
    std::unique_ptr<phone::meshcore::MeshCorePhoneCore> mc_core_;
    bool started_ = false;
    bool connected_ = false;
    uint8_t last_connection_ = 0;

    // Hold-and-retry buffers for a downlink frame whose SDIO send failed (see drains).
    phone::meshtastic::MeshtasticBleFrame mt_pending_{};
    bool mt_pending_valid_ = false;
    uint8_t mc_pending_[kMeshCoreFrameMax] = {};
    size_t mc_pending_len_ = 0;
    bool mc_pending_valid_ = false;
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
