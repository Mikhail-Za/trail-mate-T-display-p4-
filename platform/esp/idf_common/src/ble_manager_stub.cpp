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
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "ui/widgets/ble_pairing_popup.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <memory>
#include <string>

namespace ble
{
namespace
{
namespace wc = platform::esp::idf_common::wireless_companion;

constexpr const char* kBleTag = "C6_BLE";

// Minimum spacing between downlink frames. The C6's Meshtastic FromRadio path is a
// SINGLE-slot buffer (tm_ble.c s_last_meshtastic_payload): it holds only the most
// recent frame and clears it when the phone reads it. The standard Meshtastic phone
// flow is FromNum-notify -> read FromRadio until empty, and each read is a BLE
// round-trip (~1 connection interval, tens of ms). If we push the next frame before
// the phone has read the previous one, we overwrite (drop) it -> the phone never gets
// a complete config -> "too many retries" and the connect/drop/reconnect cycle.
// Until the C6 grows a real FromRadio queue, pace conservatively so a read completes
// between sends. ~110ms drains a ~40-frame config flow in ~4.5s, a one-time cost.
constexpr int64_t kMinDownlinkIntervalUs = 110000;
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
        mt_pending_valid_ = false;
        mc_pending_valid_ = false;
    }

    void update() override
    {
        wc::c6_companion().poll(); // drains C6 uplink frames -> onBleUplink()/onBleEvent()
        // Pairing prompt: while a device is connected but not yet talking (still
        // pairing), show the PIN so the user can enter it. Hide once data flows
        // (paired) or on disconnect. The grace window avoids flashing the PIN for an
        // already-bonded phone that reconnects and starts talking immediately.
        const bool awaiting = connected_ && !data_seen_ && connect_us_ != 0 &&
                              (esp_timer_get_time() - connect_us_) > kPairingGraceUs;
        pairing_prompt_visible_ = awaiting;
        // The popup touches LVGL, but this runs on the app/runtime task, not the
        // esp_lvgl_port task. LVGL is not thread-safe -- take the display lock (as
        // the chat UI feed does) or the two tasks racing LVGL freeze the device.
        const uint32_t pin = awaiting ? wc::c6_companion().status().ble_passkey : 0;
        if (::platform::esp::boards::lockDisplay(50))
        {
            ::ui::BlePairingPopup::update(pin, true, device_name_.c_str());
            ::platform::esp::boards::unlockDisplay();
        }
        if (mt_session_)
        {
            mt_session_->pumpIncomingAppData();
        }
        if (mc_core_)
        {
            mc_core_->pumpIncomingAppData();
        }
        // Paced downlink: at most one frame per connection interval so notifications
        // do not overrun the BLE link (dropping config frames). Meshtastic first.
        const int64_t now = esp_timer_get_time();
        if (now - last_downlink_us_ < kMinDownlinkIntervalUs)
        {
            return;
        }
        if (sendOneMeshtastic() || sendOneMeshCore())
        {
            last_downlink_us_ = now;
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
        out->requires_passkey = true;
        out->is_fixed_pin = true;
        out->passkey = status.ble_passkey;
        out->is_connected = connected_;
        out->is_pairing_active = pairing_prompt_visible_;
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
        data_seen_ = true; // any uplink means the peer is paired and talking
        if (data == nullptr)
        {
            return;
        }
        ESP_LOGI(kBleTag, "uplink profile=%u conn=%u len=%u",
                 static_cast<unsigned>(profile), connection_id, static_cast<unsigned>(len));
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
        ESP_LOGI(kBleTag, "event kind=%u conn=%u mtu=%u err=%u",
                 event_kind, connection_id, mtu, error_code);
        // Mirrors tm_c6_ble_event_kind: 3 = CONNECTED, 4 = DISCONNECTED.
        if (event_kind == 3u)
        {
            connected_ = true;
            last_connection_ = connection_id;
            connect_us_ = esp_timer_get_time();
            data_seen_ = false;
        }
        else if (event_kind == 4u)
        {
            connected_ = false;
            data_seen_ = false;
            connect_us_ = 0;
            // Drop any held (destructively popped) downlink so it is not delivered
            // to the next phone/session.
            mt_pending_valid_ = false;
            mc_pending_valid_ = false;
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

    void onWifiEvent(const wc::WifiEventInfo& ev) override
    {
        // The Wi-Fi cache is fed directly from the companion (see
        // deliver_wifi_event); here we only surface events for logging.
        switch (ev.kind)
        {
        case wc::WifiEventKind::ScanDone:
            ESP_LOGI(kBleTag, "wifi scan done: %u networks", ev.result_count);
            for (uint8_t i = 0; i < ev.result_count; ++i)
            {
                ESP_LOGI(kBleTag, "  wifi[%u] ssid='%s' rssi=%d ch=%u auth=%u", i,
                         ev.results[i].ssid, ev.results[i].rssi,
                         ev.results[i].channel, ev.results[i].authmode);
            }
            break;
        case wc::WifiEventKind::StaGotIp:
            ESP_LOGI(kBleTag, "wifi got ip %lu.%lu.%lu.%lu ssid='%s'",
                     (unsigned long)(ev.ipv4 & 0xff),
                     (unsigned long)((ev.ipv4 >> 8) & 0xff),
                     (unsigned long)((ev.ipv4 >> 16) & 0xff),
                     (unsigned long)((ev.ipv4 >> 24) & 0xff), ev.ssid);
            break;
        default:
            ESP_LOGI(kBleTag, "wifi event kind=%u err=%u ssid='%s'",
                     static_cast<unsigned>(ev.kind), ev.error_code, ev.ssid);
            break;
        }
    }

  private:
    // sendBleDownlink() returning false means the SDIO write failed -- the C6 did NOT
    // get the frame. popToPhone() is destructive, so on failure we HOLD the frame and
    // retry it next tick rather than dropping it; losing a config frame (e.g.
    // config_complete_id) would restart the phone's whole sync ("too many retries").
    // Sends at most one Meshtastic frame (retrying a held frame first). Returns true
    // only if the C6 accepted the frame; on SDIO-send failure the frame is kept for a
    // retry next tick and the rate-limit clock is left un-advanced.
    bool sendOneMeshtastic()
    {
        if (!mt_session_)
        {
            return false;
        }
        if (!mt_pending_valid_)
        {
            if (!mt_session_->popToPhone(&mt_pending_))
            {
                return false;
            }
            mt_pending_valid_ = true;
        }
        if (wc::c6_companion().sendBleDownlink(wc::BleProfile::Meshtastic, last_connection_,
                                               mt_pending_.buf, mt_pending_.len))
        {
            mt_pending_valid_ = false;
            return true;
        }
        ESP_LOGW(kBleTag, "mt downlink send failed len=%u (held)",
                 static_cast<unsigned>(mt_pending_.len));
        return false;
    }

    bool sendOneMeshCore()
    {
        if (!mc_core_)
        {
            return false;
        }
        if (!mc_pending_valid_)
        {
            mc_pending_len_ = 0;
            if (!mc_core_->popTxFrame(mc_pending_, &mc_pending_len_))
            {
                return false;
            }
            mc_pending_valid_ = true;
        }
        if (wc::c6_companion().sendBleDownlink(wc::BleProfile::MeshCore, last_connection_,
                                               mc_pending_, mc_pending_len_))
        {
            mc_pending_valid_ = false;
            return true;
        }
        return false;
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
    int64_t last_downlink_us_ = 0;
    // Pairing-prompt state: show the PIN while connected-but-not-yet-talking.
    static constexpr int64_t kPairingGraceUs = 1200000; // 1.2s before showing PIN
    int64_t connect_us_ = 0;
    bool data_seen_ = false;
    bool pairing_prompt_visible_ = false;
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
