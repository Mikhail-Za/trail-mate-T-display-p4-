#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/esp/idf_common/wireless_companion/c6_wifi_bridge.h"

#include "hostlink/c6/c6_frame_codec.h"
#include "hostlink/c6/c6_protocol.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_serial_slave_link/essl.h"
#include "esp_serial_slave_link/essl_sdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdmmc_cmd.h"
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "boards/tab5/tab5_board.h"
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/t_display_p4_board.h"
#endif
#endif

namespace platform::esp::idf_common::wireless_companion
{
namespace
{

constexpr const char* kTag = "P4_C6";
constexpr uint16_t kInitialSequence = 1;
constexpr uint32_t kHandshakeTimeoutMs = 500;
constexpr uint32_t kShortIoTimeoutMs = 100;
constexpr uint32_t kSdioCardInitRetryDelayMs = 100;
constexpr uint32_t kSdioCardInitMaxAttempts = 30;
constexpr size_t kSdioBufferSize = 1152;
constexpr uint32_t kRequestedFeatures = TM_C6_FEATURE_BLE_MESHTASTIC |
                                        TM_C6_FEATURE_BLE_MESHCORE |
                                        TM_C6_FEATURE_BLE_TRAILMATE |
                                        TM_C6_FEATURE_ESPNOW_TEAM |
                                        TM_C6_FEATURE_WIFI_STA |
                                        TM_C6_FEATURE_WIFI_AP |
                                        TM_C6_FEATURE_DIAG_LOG |
                                        TM_C6_FEATURE_HOSTLINK_PING;
constexpr uint32_t kDefaultConfigSequence = 1;
// The C6 is power-cycled by the P4 just before the handshake and needs a few
// seconds to boot its firmware (Wi-Fi + BLE + NimBLE init) before it can answer
// on the SDIO slave. Retry HELLO across that window instead of a single shot.
constexpr uint32_t kHelloMaxAttempts = 8;
constexpr uint32_t kHelloRetryDelayMs = 250;

bool board_has_c6_companion()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return true;
#else
    return false;
#endif
}

uint16_t read_le16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read_le32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void write_le16(uint8_t* data, uint16_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFFu);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void write_le32(uint8_t* data, uint32_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFFu);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

void set_detail(C6CompanionStatus& status, CompanionState state, const char* detail)
{
    status.present = state == CompanionState::Present;
    status.state = state;
    status.detail = detail;
}

uint8_t ble_channel_for(BleProfile profile)
{
    switch (profile)
    {
    case BleProfile::Meshtastic:
        return TM_C6_CH_BLE_MESHTASTIC;
    case BleProfile::MeshCore:
        return TM_C6_CH_BLE_MESHCORE;
    case BleProfile::TrailMate:
        return TM_C6_CH_BLE_TRAILMATE;
    }
    return TM_C6_CH_BLE_TRAILMATE;
}

#if defined(ESP_PLATFORM)
class C6Transport
{
  public:
    virtual bool begin() = 0;
    virtual bool send(const uint8_t* data, size_t len, uint32_t timeout_ms) = 0;
    virtual bool recv(uint8_t* data, size_t max_len, size_t& out_len, uint32_t timeout_ms) = 0;
    virtual void reset() = 0;
    virtual const char* last_error() const = 0;
    virtual ~C6Transport() = default;
};

class SdioC6Transport final : public C6Transport
{
  public:
    bool begin() override
    {
        if (ready_)
        {
            return true;
        }

        if (!prepare_board_companion())
        {
            return false;
        }

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_1;
        host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF |
                     SDMMC_HOST_FLAG_DEINIT_ARG;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 4;
        apply_board_pins(slot_config);
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_err_t err = sdmmc_host_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            set_error("sdmmc_host_init_failed");
            ESP_LOGW(kTag, "SDIO host init failed: %s", esp_err_to_name(err));
            return false;
        }
        host_initialized_ = true;

        err = sdmmc_host_init_slot(host.slot, &slot_config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            set_error("sdmmc_slot_init_failed");
            ESP_LOGW(kTag, "SDIO slot init failed: %s", esp_err_to_name(err));
            deinit();
            return false;
        }
        slot_initialized_ = true;

        card_ = static_cast<sdmmc_card_t*>(calloc(1, sizeof(sdmmc_card_t)));
        if (card_ == nullptr)
        {
            set_error("sdio_card_alloc_failed");
            deinit();
            return false;
        }

        for (uint32_t attempt = 1; attempt <= kSdioCardInitMaxAttempts; ++attempt)
        {
            std::memset(card_, 0, sizeof(*card_));
            err = sdmmc_card_init(&host, card_);
            if (err == ESP_OK)
            {
                break;
            }
            if (attempt < kSdioCardInitMaxAttempts)
            {
                ESP_LOGW(kTag,
                         "SDIO card init attempt %lu/%lu failed: %s; retrying",
                         static_cast<unsigned long>(attempt),
                         static_cast<unsigned long>(kSdioCardInitMaxAttempts),
                         esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(kSdioCardInitRetryDelayMs));
            }
        }
        if (err != ESP_OK)
        {
            set_error("sdio_card_init_failed");
            ESP_LOGW(kTag, "SDIO card init failed: %s", esp_err_to_name(err));
            deinit();
            return false;
        }
        ESP_LOGI(kTag,
                 "SDIO card detected is_sdio=%lu is_mem=%lu functions=%lu ocr=0x%08lx rca=0x%04x real_freq_khz=%d",
                 static_cast<unsigned long>(card_->is_sdio),
                 static_cast<unsigned long>(card_->is_mem),
                 static_cast<unsigned long>(card_->num_io_functions),
                 static_cast<unsigned long>(card_->ocr),
                 static_cast<unsigned>(card_->rca),
                 card_->real_freq_khz);
        log_cccr_state("pre_essl");

        essl_sdio_config_t essl_config = {
            .card = card_,
            .recv_buffer_size = static_cast<int>(kSdioBufferSize),
        };
        err = essl_sdio_init_dev(&handle_, &essl_config);
        if (err != ESP_OK)
        {
            set_error("essl_sdio_init_failed");
            ESP_LOGW(kTag, "ESSL SDIO init failed: %s", esp_err_to_name(err));
            deinit();
            return false;
        }

        err = essl_init(handle_, kHandshakeTimeoutMs);
        if (err != ESP_OK)
        {
            set_error(err == ESP_ERR_TIMEOUT ? "essl_init_fn_ready_timeout" : "essl_init_failed");
            ESP_LOGW(kTag,
                     "ESSL init failed stage=function_ready_or_cccr err=%s",
                     esp_err_to_name(err));
            deinit();
            return false;
        }
        log_cccr_state("post_essl");

        ready_ = true;
        error_[0] = '\0';
        ESP_LOGI(kTag,
                 "SDIO transport ready slot=1 pins clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d",
                 pins_.clk,
                 pins_.cmd,
                 pins_.d0,
                 pins_.d1,
                 pins_.d2,
                 pins_.d3);
        return true;
    }

    bool send(const uint8_t* data, size_t len, uint32_t timeout_ms) override
    {
        if (!ready_ || handle_ == nullptr || data == nullptr || len == 0)
        {
            set_error("sdio_send_invalid_state");
            return false;
        }

        const esp_err_t err = essl_send_packet(handle_, data, len, timeout_ms);
        if (err != ESP_OK)
        {
            set_error("sdio_send_failed");
            ESP_LOGW(kTag, "SDIO send failed len=%u err=%s", static_cast<unsigned>(len), esp_err_to_name(err));
            return false;
        }
        return true;
    }

    bool recv(uint8_t* data, size_t max_len, size_t& out_len, uint32_t timeout_ms) override
    {
        out_len = 0;
        if (!ready_ || handle_ == nullptr || data == nullptr || max_len == 0)
        {
            set_error("sdio_recv_invalid_state");
            return false;
        }

        uint32_t rx_size = 0;
        esp_err_t err = essl_get_rx_data_size(handle_, &rx_size, timeout_ms);
        if (err != ESP_OK)
        {
            set_error("sdio_rx_size_failed");
            return false;
        }
        if (rx_size == 0)
        {
            set_error("sdio_rx_empty");
            return false;
        }
        if (rx_size > max_len)
        {
            set_error("sdio_rx_too_large");
            return false;
        }

        size_t size_read = rx_size;
        err = essl_get_packet(handle_, data, max_len, &size_read, timeout_ms);
        if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED)
        {
            set_error("sdio_recv_failed");
            ESP_LOGW(kTag, "SDIO recv failed err=%s", esp_err_to_name(err));
            return false;
        }
        out_len = size_read;
        return true;
    }

    void reset() override
    {
        deinit();
    }

    const char* last_error() const override
    {
        return error_[0] != '\0' ? error_ : "sdio_no_error_detail";
    }

  private:
    struct Pins
    {
        int clk = -1;
        int cmd = -1;
        int d0 = -1;
        int d1 = -1;
        int d2 = -1;
        int d3 = -1;
    };

    void set_error(const char* text)
    {
        std::snprintf(error_, sizeof(error_), "%s", text ? text : "sdio_error");
    }

    static void configure_pullup(int gpio)
    {
        if (gpio < 0)
        {
            return;
        }
        const gpio_num_t pin = static_cast<gpio_num_t>(gpio);
        (void)gpio_pullup_en(pin);
        (void)gpio_pulldown_dis(pin);
    }

    void apply_board_pins(sdmmc_slot_config_t& slot_config)
    {
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
        const auto& pins = ::boards::tab5::Tab5Board::profile().c6_sdio;
        pins_ = {pins.clk, pins.cmd, pins.d0, pins.d1, pins.d2, pins.d3};
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        const auto& pins = ::boards::t_display_p4::TDisplayP4Board::profile().c6_sdio;
        pins_ = {pins.clk, pins.cmd, pins.d0, pins.d1, pins.d2, pins.d3};
#endif
        slot_config.clk = static_cast<gpio_num_t>(pins_.clk);
        slot_config.cmd = static_cast<gpio_num_t>(pins_.cmd);
        slot_config.d0 = static_cast<gpio_num_t>(pins_.d0);
        slot_config.d1 = static_cast<gpio_num_t>(pins_.d1);
        slot_config.d2 = static_cast<gpio_num_t>(pins_.d2);
        slot_config.d3 = static_cast<gpio_num_t>(pins_.d3);
        configure_pullup(pins_.clk);
        configure_pullup(pins_.cmd);
        configure_pullup(pins_.d0);
        configure_pullup(pins_.d1);
        configure_pullup(pins_.d2);
        configure_pullup(pins_.d3);
    }

    void log_cccr_state(const char* stage) const
    {
        if (card_ == nullptr)
        {
            return;
        }

        uint8_t ioe = 0;
        uint8_t ior = 0;
        uint8_t ie = 0;
        const esp_err_t ioe_err = sdmmc_io_read_byte(card_, 0, SD_IO_CCCR_FN_ENABLE, &ioe);
        const esp_err_t ior_err = sdmmc_io_read_byte(card_, 0, SD_IO_CCCR_FN_READY, &ior);
        const esp_err_t ie_err = sdmmc_io_read_byte(card_, 0, SD_IO_CCCR_INT_ENABLE, &ie);

        if (ioe_err == ESP_OK && ior_err == ESP_OK && ie_err == ESP_OK)
        {
            ESP_LOGI(kTag,
                     "SDIO CCCR %s ioe=0x%02x ior=0x%02x int_ena=0x%02x",
                     stage ? stage : "probe",
                     ioe,
                     ior,
                     ie);
            return;
        }

        ESP_LOGW(kTag,
                 "SDIO CCCR %s probe failed ioe=%s ior=%s int_ena=%s",
                 stage ? stage : "probe",
                 esp_err_to_name(ioe_err),
                 esp_err_to_name(ior_err),
                 esp_err_to_name(ie_err));
    }

    bool prepare_board_companion()
    {
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
        const auto& profile = ::boards::t_display_p4::TDisplayP4Board::profile();
        const auto& io = profile.io_expander;
        if (!board.expanderPinMode(io.c6_enable, true))
        {
            set_error("c6_enable_pin_mode_failed");
            return false;
        }
        if (!board.expanderWriteActive(io.c6_enable, true, profile.c6_enable_active_high))
        {
            set_error("c6_enable_failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!board.expanderWriteActive(io.c6_enable, false, profile.c6_enable_active_high))
        {
            set_error("c6_reset_assert_failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!board.expanderWriteActive(io.c6_enable, true, profile.c6_enable_active_high))
        {
            set_error("c6_reset_release_failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(1000));
        return true;
#elif defined(TRAIL_MATE_ESP_BOARD_TAB5)
        const auto& profile = ::boards::tab5::Tab5Board::profile();
        if (profile.c6_reset_gpio_requires_validation)
        {
            ESP_LOGW(kTag,
                     "Tab5 C6 reset GPIO%d requires validation; skipping reset pulse",
                     profile.c6_reset_gpio);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        return true;
#else
        return false;
#endif
    }

    void deinit()
    {
        ready_ = false;
        if (handle_ != nullptr)
        {
            (void)essl_sdio_deinit_dev(handle_);
            handle_ = nullptr;
        }
        if (card_ != nullptr)
        {
            free(card_);
            card_ = nullptr;
        }
        if (slot_initialized_)
        {
            (void)sdmmc_host_deinit_slot(SDMMC_HOST_SLOT_1);
            slot_initialized_ = false;
        }
        if (host_initialized_)
        {
            (void)sdmmc_host_deinit();
            host_initialized_ = false;
        }
    }

    essl_handle_t handle_ = nullptr;
    sdmmc_card_t* card_ = nullptr;
    Pins pins_{};
    bool ready_ = false;
    bool host_initialized_ = false;
    bool slot_initialized_ = false;
    char error_[64] = {};
};
#endif

class C6CompanionRuntime final : public WirelessCompanion
{
  public:
    bool begin() override
    {
        if (status_.started)
        {
            if (status_.present)
            {
                return true;
            }
            // A prior probe did not reach Present (e.g. the C6 was still booting or
            // SDIO transiently failed during the startup probe). Retry the handshake
            // rather than latching "missing" until the next P4 reboot.
            return try_handshake();
        }

        status_.started = true;
        ensure_ble_passkey();
        status_.ble_passkey = ble_passkey_;
        status_.board_capable = board_has_c6_companion();
        status_.protocol_min = TM_C6_PROTO_MIN;
        status_.protocol_max = TM_C6_PROTO_MAX;
        status_.supported_features = 0;
        status_.enabled_features = 0;
        status_.config_seq = 0;
        status_.config_error = 0;
        status_.selected_mtu = 0;
        status_.ble_state = 0;
        status_.espnow_state = 0;
        status_.wifi_state = 0;

        if (!status_.board_capable)
        {
            set_detail(status_, CompanionState::Unsupported, "target_has_no_c6_companion");
            return false;
        }

        status_.present = false;
        status_.state = CompanionState::TransportPending;
        status_.detail = "sdio_transport_probe_pending";
        status_.ping_nonce = 0xC6000001u;
        status_.ping_count = 1;
#if defined(ESP_PLATFORM)
        ESP_LOGW(kTag,
                 "C6 Phase 1 HostLink starting SDIO transport probe; "
                 "continuing product boot if C6 is unavailable");
        ESP_LOGI(kTag,
                 "HELLO request proto=%u..%u p4_fw=%lu requested_features=0x%08lx preferred_mtu=%u max_payload=%u seq=%u",
                 TM_C6_PROTO_MIN,
                 TM_C6_PROTO_MAX,
                 (unsigned long)TM_C6_P4_FIRMWARE_VERSION_UNKNOWN,
                 (unsigned long)kRequestedFeatures,
                 TM_C6_MAX_PAYLOAD,
                 TM_C6_MAX_PAYLOAD,
                 kInitialSequence);
        ESP_LOGI(kTag,
                 "PING template nonce=0x%08lx seq=%u channel=%u",
                 (unsigned long)status_.ping_nonce,
                 static_cast<unsigned>(kInitialSequence + 1),
                 static_cast<unsigned>(TM_C6_CH_CONTROL));
#endif
        return try_handshake();
    }

    bool isPresent() const override
    {
        return status_.present;
    }

    uint32_t capabilities() const override
    {
        return status_.supported_features;
    }

    C6CompanionStatus status() const override
    {
        C6CompanionStatus copy = status_;
        copy.board_capable = board_has_c6_companion();
        if (!copy.started && copy.board_capable)
        {
            copy.state = CompanionState::NotStarted;
            copy.detail = "not_started";
        }
        return copy;
    }

    void poll() override
    {
        if (!status_.started || !status_.present)
        {
            return;
        }
#if defined(ESP_PLATFORM)
        if (!transport_)
        {
            return;
        }
        hostlink::c6::Frame frame{};
        if (receive_frame(frame, 0, false))
        {
            handle_async_frame(frame);
        }
#endif
    }

    bool sendBleDownlink(BleProfile profile,
                         uint8_t connection_id,
                         const uint8_t* data,
                         size_t len) override
    {
        if (!status_.present || data == nullptr)
        {
            return false;
        }
        if (len > TM_C6_MAX_PAYLOAD - sizeof(tm_c6_ble_packet_header_t))
        {
            return false;
        }

        std::vector<uint8_t> payload(sizeof(tm_c6_ble_packet_header_t) + len);
        tm_c6_ble_packet_header_t header{};
        header.profile = static_cast<uint8_t>(profile);
        header.connection_id = connection_id;
        header.payload_len = static_cast<uint16_t>(len);
        std::memcpy(payload.data(), &header, sizeof(header));
        if (len > 0)
        {
            std::memcpy(payload.data() + sizeof(header), data, len);
        }
        return send_frame(TM_C6_FRAME_BLE_DOWNLINK,
                          ble_channel_for(profile),
                          0,
                          0,
                          payload.data(),
                          payload.size());
    }

    bool sendEspNow(const uint8_t mac[6], const uint8_t* data, size_t len) override
    {
        if (!status_.present || data == nullptr || mac == nullptr)
        {
            return false;
        }
        if (len > TM_C6_ESPNOW_PAYLOAD_MAX)
        {
            return false;
        }

        tm_c6_espnow_packet_t packet{};
        std::memcpy(packet.peer_mac, mac, sizeof(packet.peer_mac));
        packet.rssi_valid = 0;
        packet.rssi = 0;
        packet.channel = 0;
        packet.payload_len = static_cast<uint8_t>(len);
        std::memcpy(packet.payload, data, len);
        return send_frame(TM_C6_FRAME_ESPNOW_DOWNLINK,
                          TM_C6_CH_ESPNOW_TEAM,
                          0,
                          0,
                          reinterpret_cast<const uint8_t*>(&packet),
                          sizeof(packet));
    }

    bool sendWifiControl(WifiCommand command, const char* ssid,
                         const char* password, uint8_t channel, uint16_t op_id) override
    {
        if (!status_.present)
        {
            return false;
        }
        tm_c6_wifi_control_t control{};
        control.command = static_cast<uint8_t>(command);
        control.flags = 0;
        control.op_id = op_id;
        control.channel = channel;
        // control is zero-initialised, so copying at most sizeof-1 leaves the final
        // byte NUL: the C6 reads these fixed arrays as C strings.
        if (ssid != nullptr)
        {
            std::strncpy(control.ssid, ssid, sizeof(control.ssid) - 1);
        }
        if (password != nullptr)
        {
            std::strncpy(control.password, password, sizeof(control.password) - 1);
        }
        return send_frame(TM_C6_FRAME_WIFI_CONTROL,
                          TM_C6_CH_WIFI_MGMT,
                          0,
                          0,
                          reinterpret_cast<const uint8_t*>(&control),
                          sizeof(control));
    }

    void setUplinkSink(WirelessUplinkSink* sink) override
    {
        uplink_sink_ = sink;
    }

  private:
    uint16_t next_seq()
    {
        const uint16_t current = next_seq_++;
        if (next_seq_ == 0)
        {
            next_seq_ = 1;
        }
        return current;
    }

    bool encode(uint8_t frame_type,
                uint8_t channel,
                uint16_t flags,
                uint16_t ack,
                const uint8_t* payload,
                size_t payload_len,
                std::vector<uint8_t>& out)
    {
        hostlink::c6::EncodeRequest request{};
        request.frame_type = frame_type;
        request.channel = channel;
        request.flags = flags;
        request.seq = next_seq();
        request.ack = ack;
        request.payload = payload;
        request.payload_len = payload_len;
        return hostlink::c6::encode_frame(request, out, TM_C6_MAX_PAYLOAD);
    }

    bool receive_frame(hostlink::c6::Frame& frame, uint32_t timeout_ms, bool update_status_on_empty = true)
    {
#if defined(ESP_PLATFORM)
        if (!transport_)
        {
            return false;
        }
        std::array<uint8_t, TM_C6_FRAME_HEADER_LEN + TM_C6_MAX_PAYLOAD> rx{};
        size_t rx_len = 0;
        if (!transport_->recv(rx.data(), rx.size(), rx_len, timeout_ms))
        {
            if (update_status_on_empty)
            {
                set_detail(status_, CompanionState::Missing, transport_->last_error());
            }
            return false;
        }

        const auto decoded = hostlink::c6::decode_frame(rx.data(), rx_len, TM_C6_MAX_PAYLOAD);
        if (decoded.status != hostlink::c6::DecodeStatus::Ok)
        {
            set_detail(status_, CompanionState::Error, hostlink::c6::decode_status_name(decoded.status));
            ESP_LOGW(kTag, "C6 decode failed status=%s len=%u", status_.detail, static_cast<unsigned>(rx_len));
            return false;
        }
        frame = decoded.frame;
        return true;
#else
        (void)frame;
        (void)timeout_ms;
        return false;
#endif
    }

    // Receive the next frame of a specific type within total_timeout_ms, skipping
    // any stray/out-of-order frames (e.g. a duplicate HELLO_ACK queued by our retry
    // HELLOs). Keeps the handshake in sync without assuming strict frame ordering.
    bool receive_expected(uint8_t expected_type,
                          hostlink::c6::Frame& frame,
                          uint32_t total_timeout_ms)
    {
#if defined(ESP_PLATFORM)
        const uint32_t start = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        for (;;)
        {
            if (receive_frame(frame, kShortIoTimeoutMs, false))
            {
                if (frame.frame_type == expected_type)
                {
                    return true;
                }
                ESP_LOGW(kTag,
                         "C6 skip stray frame type=0x%02x while awaiting 0x%02x",
                         static_cast<unsigned>(frame.frame_type),
                         static_cast<unsigned>(expected_type));
            }
            const uint32_t now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (now - start >= total_timeout_ms)
            {
                return false;
            }
        }
#else
        (void)expected_type;
        (void)frame;
        (void)total_timeout_ms;
        return false;
#endif
    }

    bool send_frame(uint8_t frame_type,
                    uint8_t channel,
                    uint16_t flags,
                    uint16_t ack,
                    const uint8_t* payload,
                    size_t payload_len)
    {
#if defined(ESP_PLATFORM)
        std::vector<uint8_t> out;
        if (!encode(frame_type, channel, flags, ack, payload, payload_len, out))
        {
            set_detail(status_, CompanionState::Error, "c6_encode_failed");
            return false;
        }
        if (!transport_ || !transport_->send(out.data(), out.size(), kShortIoTimeoutMs))
        {
            set_detail(status_, CompanionState::Missing, transport_ ? transport_->last_error() : "transport_missing");
            return false;
        }
        return true;
#else
        (void)frame_type;
        (void)channel;
        (void)flags;
        (void)ack;
        (void)payload;
        (void)payload_len;
        return false;
#endif
    }

    bool send_hello()
    {
        uint8_t payload[sizeof(tm_c6_hello_t)] = {};
        write_le16(payload, TM_C6_PROTO_MIN);
        write_le16(payload + 2, TM_C6_PROTO_MAX);
        write_le32(payload + 4, TM_C6_P4_FIRMWARE_VERSION_UNKNOWN);
        write_le32(payload + 8, kRequestedFeatures);
        write_le16(payload + 12, TM_C6_MAX_PAYLOAD);
        write_le16(payload + 14, TM_C6_MAX_PAYLOAD);
        return send_frame(TM_C6_FRAME_HELLO,
                          TM_C6_CH_CONTROL,
                          TM_C6_FLAG_ACK_REQUIRED,
                          0,
                          payload,
                          sizeof(payload));
    }

    bool handle_hello_ack(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type != TM_C6_FRAME_HELLO_ACK ||
            frame.channel != TM_C6_CH_CONTROL ||
            frame.payload.size() != sizeof(tm_c6_hello_ack_t))
        {
            set_detail(status_, CompanionState::Error, "unexpected_hello_ack");
            return false;
        }

        const uint8_t* payload = frame.payload.data();
        const uint16_t selected_proto = read_le16(payload);
        if (selected_proto < TM_C6_PROTO_MIN || selected_proto > TM_C6_PROTO_MAX)
        {
            set_detail(status_, CompanionState::Error, "protocol_mismatch");
            return false;
        }

        status_.selected_protocol = selected_proto;
        status_.firmware_version = read_le32(payload + 2);
        status_.supported_features = read_le32(payload + 6);
        status_.selected_mtu = read_le16(payload + 10);
        status_.free_heap = read_le32(payload + 14);
        return true;
    }

    bool send_ping()
    {
        status_.ping_nonce = 0xC6000001u + status_.ping_count;
        uint8_t payload[sizeof(tm_c6_ping_t)] = {};
        write_le32(payload, status_.ping_nonce);
#if defined(ESP_PLATFORM)
        write_le32(payload + 4, static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS));
#else
        write_le32(payload + 4, 0);
#endif
        ++status_.ping_count;
        return send_frame(TM_C6_FRAME_PING,
                          TM_C6_CH_CONTROL,
                          TM_C6_FLAG_ACK_REQUIRED,
                          0,
                          payload,
                          sizeof(payload));
    }

    bool handle_pong(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type != TM_C6_FRAME_PONG ||
            frame.channel != TM_C6_CH_CONTROL ||
            frame.payload.size() != sizeof(tm_c6_pong_t))
        {
            set_detail(status_, CompanionState::Error, "unexpected_pong");
            return false;
        }

        const uint8_t* payload = frame.payload.data();
        const uint32_t nonce = read_le32(payload);
        if (nonce != status_.ping_nonce)
        {
            set_detail(status_, CompanionState::Error, "pong_nonce_mismatch");
            return false;
        }

        ++status_.pong_count;
        status_.free_heap = read_le32(payload + 8);
        return true;
    }

    // Load (or first-time generate + persist) the random 6-digit BLE pairing PIN.
    // Persisting in NVS keeps the code stable across reboots so a paired phone need
    // not re-pair every boot, while still being unique per device (not the well-known
    // Meshtastic 123456 default). The P4 displays this PIN during pairing.
    void ensure_ble_passkey()
    {
        if (ble_passkey_ >= 100000u && ble_passkey_ <= 999999u)
        {
            return;
        }
#if defined(ESP_PLATFORM)
        uint32_t pin = 0;
        nvs_handle_t handle = 0;
        if (nvs_open("tm_c6", NVS_READWRITE, &handle) == ESP_OK)
        {
            const esp_err_t rc = nvs_get_u32(handle, "ble_pin", &pin);
            if (rc != ESP_OK || pin < 100000u || pin > 999999u)
            {
                pin = 100000u + (esp_random() % 900000u);
                (void)nvs_set_u32(handle, "ble_pin", pin);
                (void)nvs_commit(handle);
            }
            nvs_close(handle);
        }
        else
        {
            pin = 100000u + (esp_random() % 900000u);
        }
        ble_passkey_ = pin;
#else
        ble_passkey_ = 123456u;
#endif
    }

    tm_c6_companion_config_t default_config() const
    {
        tm_c6_companion_config_t config{};
        config.config_seq = kDefaultConfigSequence;
        config.requested_features = kRequestedFeatures;

        config.ble.ble_enabled = 1;
        config.ble.meshtastic_enabled = 1;
        config.ble.meshcore_enabled = 1;
        config.ble.trailmate_enabled = 1;
        // Fixed-PIN pairing using the per-device random 6-digit code (shown on the
        // P4 screen during pairing). Stable across reboots so a bonded phone need not
        // re-pair, and not the well-known Meshtastic 123456 default.
        config.ble.pairing_mode = TM_C6_PAIRING_FIXED_PIN;
        config.ble.fixed_pin_enabled = 1;
        char pin_buf[16];
        const unsigned long pin =
            static_cast<unsigned long>((ble_passkey_ >= 100000u ? ble_passkey_ : 123456u) %
                                       1000000u);
        std::snprintf(pin_buf, sizeof(pin_buf), "%06lu", pin);
        std::memcpy(config.ble.fixed_pin, pin_buf, 6); // exactly 6 digits, no NUL
        std::memcpy(config.ble.device_name, "TrailMate-C6", 11);
        config.ble.preferred_mtu = 247;

        config.espnow.espnow_enabled = 1;
        config.espnow.team_discovery_enabled = 1;
        config.espnow.channel = 0;
        config.espnow.beacon_interval_ms = 1000;
        std::fill(std::begin(config.espnow.broadcast_mac),
                  std::end(config.espnow.broadcast_mac),
                  static_cast<uint8_t>(0xFF));

        // Wi-Fi STA enabled so the C6 can scan and connect on demand; it stays idle
        // (no association) until the P4 issues a WifiCommand::Connect. AP stays off.
        // BLE + ESP-NOW keep running via the C6's radio coexistence.
        config.wifi.wifi_enabled = 1;
        config.wifi.sta_enabled = 1;
        config.wifi.ap_enabled = 0;
        config.wifi.persist_credentials = 1;
        std::memcpy(config.wifi.ap_ssid, "TrailMate-C6", 11);
        config.wifi.ap_channel = 1;
        return config;
    }

    bool send_config_set()
    {
        const tm_c6_companion_config_t config = default_config();
        status_.config_seq = config.config_seq;
        return send_frame(TM_C6_FRAME_CONFIG_SET,
                          TM_C6_CH_CONTROL,
                          TM_C6_FLAG_ACK_REQUIRED,
                          0,
                          reinterpret_cast<const uint8_t*>(&config),
                          sizeof(config));
    }

    bool handle_config_report(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type != TM_C6_FRAME_CONFIG_REPORT ||
            frame.channel != TM_C6_CH_CONTROL ||
            frame.payload.size() != sizeof(tm_c6_config_report_t))
        {
            set_detail(status_, CompanionState::Error, "unexpected_config_report");
            return false;
        }

        tm_c6_config_report_t report{};
        std::memcpy(&report, frame.payload.data(), sizeof(report));
        status_.config_seq = report.config_seq;
        status_.supported_features = report.supported_features;
        status_.enabled_features = report.enabled_features;
        status_.free_heap = report.c6_free_heap;
        status_.config_error = report.error_code;
        status_.selected_mtu = report.selected_mtu;
        status_.ble_state = report.ble_state;
        status_.espnow_state = report.espnow_state;
        status_.wifi_state = report.wifi_state;

        if (report.error_code != TM_C6_OK)
        {
            set_detail(status_, CompanionState::Error, "config_report_error");
            return false;
        }
        return true;
    }

    void deliver_ble_uplink(const hostlink::c6::Frame& frame)
    {
        if (uplink_sink_ == nullptr ||
            frame.payload.size() < sizeof(tm_c6_ble_packet_header_t))
        {
            return;
        }
        tm_c6_ble_packet_header_t header{};
        std::memcpy(&header, frame.payload.data(), sizeof(header));
        const size_t available = frame.payload.size() - sizeof(header);
        const size_t data_len = std::min<size_t>(header.payload_len, available);
        uplink_sink_->onBleUplink(static_cast<BleProfile>(header.profile),
                                  header.connection_id,
                                  frame.payload.data() + sizeof(header),
                                  data_len);
    }

    void deliver_ble_event(const hostlink::c6::Frame& frame)
    {
        if (uplink_sink_ == nullptr || frame.payload.size() != sizeof(tm_c6_ble_event_t))
        {
            return;
        }
        tm_c6_ble_event_t event{};
        std::memcpy(&event, frame.payload.data(), sizeof(event));
        uplink_sink_->onBleEvent(static_cast<BleProfile>(event.profile),
                                 event.event_kind,
                                 event.connection_id,
                                 event.mtu,
                                 event.error_code);
    }

    void deliver_espnow_uplink(const hostlink::c6::Frame& frame)
    {
        if (uplink_sink_ == nullptr ||
            frame.payload.size() != sizeof(tm_c6_espnow_packet_t))
        {
            return;
        }
        tm_c6_espnow_packet_t packet{};
        std::memcpy(&packet, frame.payload.data(), sizeof(packet));
        const size_t data_len =
            std::min<size_t>(packet.payload_len, static_cast<size_t>(TM_C6_ESPNOW_PAYLOAD_MAX));
        uplink_sink_->onEspNowReceive(packet.peer_mac, packet.rssi, packet.payload, data_len);
    }

    void deliver_wifi_event(const hostlink::c6::Frame& frame)
    {
        if (frame.payload.size() != sizeof(tm_c6_wifi_event_t))
        {
            return;
        }
        tm_c6_wifi_event_t raw{};
        std::memcpy(&raw, frame.payload.data(), sizeof(raw));
        WifiEventInfo ev{};
        ev.kind = static_cast<WifiEventKind>(raw.event_kind);
        ev.error_code = raw.error_code;
        ev.op_id = raw.op_id;
        ev.ipv4 = raw.ipv4_addr;
        std::memcpy(ev.ssid, raw.ssid, sizeof(raw.ssid));
        ev.ssid[sizeof(ev.ssid) - 1] = '\0';
        const uint8_t count =
            std::min<uint8_t>(raw.result_count, TM_C6_WIFI_SCAN_RESULT_COUNT);
        ev.result_count = count;
        for (uint8_t i = 0; i < count; ++i)
        {
            std::memcpy(ev.results[i].ssid, raw.results[i].ssid, sizeof(raw.results[i].ssid));
            ev.results[i].ssid[sizeof(ev.results[i].ssid) - 1] = '\0';
            ev.results[i].rssi = raw.results[i].rssi;
            ev.results[i].channel = raw.results[i].channel;
            ev.results[i].authmode = raw.results[i].authmode;
        }
        // Feed the UI Wi-Fi cache directly so Wi-Fi status/scan work regardless of
        // whether a BLE uplink sink is registered (e.g. BLE disabled). Still notify
        // the sink for any BLE-side consumer/logging.
        c6_wifi_ingest_event(ev);
        if (uplink_sink_ != nullptr)
        {
            uplink_sink_->onWifiEvent(ev);
        }
    }

    void handle_async_frame(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type == TM_C6_FRAME_BLE_UPLINK)
        {
            deliver_ble_uplink(frame);
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_BLE_EVENT)
        {
            deliver_ble_event(frame);
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_ESPNOW_UPLINK)
        {
            deliver_espnow_uplink(frame);
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_WIFI_EVENT)
        {
            deliver_wifi_event(frame);
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_CONFIG_REPORT)
        {
            (void)handle_config_report(frame);
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_DIAG_REPORT &&
            frame.payload.size() == sizeof(tm_c6_diag_report_t))
        {
            tm_c6_diag_report_t report{};
            std::memcpy(&report, frame.payload.data(), sizeof(report));
            status_.free_heap = report.free_heap;
            status_.enabled_features = report.enabled_features;
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_ERROR &&
            frame.payload.size() == sizeof(tm_c6_error_t))
        {
            const auto* error = reinterpret_cast<const tm_c6_error_t*>(frame.payload.data());
            status_.config_error = error->error_code;
            set_detail(status_, CompanionState::Error, "c6_error_frame");
        }
    }

    bool try_handshake()
    {
#if defined(ESP_PLATFORM)
        transport_ = std::make_unique<SdioC6Transport>();
        if (!transport_->begin())
        {
            set_detail(status_, CompanionState::Missing, transport_->last_error());
            ESP_LOGW(kTag, "C6 SDIO transport unavailable detail=%s", status_.detail);
            transport_.reset();
            return false;
        }

        hostlink::c6::Frame frame{};
        bool hello_ok = false;
        for (uint32_t attempt = 1; attempt <= kHelloMaxAttempts; ++attempt)
        {
            if (!send_hello())
            {
                ESP_LOGW(kTag, "C6 HELLO send failed detail=%s", status_.detail);
                transport_->reset();
                transport_.reset();
                return false;
            }
            if (receive_frame(frame, kHandshakeTimeoutMs, false) && handle_hello_ack(frame))
            {
                hello_ok = true;
                if (attempt > 1)
                {
                    ESP_LOGI(kTag, "C6 HELLO_ACK ok on attempt %lu", (unsigned long)attempt);
                }
                break;
            }
            ESP_LOGW(kTag,
                     "C6 HELLO_ACK attempt %lu/%lu no response (%s); C6 may still be booting",
                     (unsigned long)attempt,
                     (unsigned long)kHelloMaxAttempts,
                     status_.detail);
            vTaskDelay(pdMS_TO_TICKS(kHelloRetryDelayMs));
        }
        if (!hello_ok)
        {
            ESP_LOGW(kTag,
                     "C6 HELLO_ACK failed after %lu attempts detail=%s",
                     (unsigned long)kHelloMaxAttempts,
                     status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!send_ping())
        {
            ESP_LOGW(kTag, "C6 PING send failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!receive_expected(TM_C6_FRAME_PONG, frame, kHandshakeTimeoutMs) || !handle_pong(frame))
        {
            ESP_LOGW(kTag, "C6 PONG failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!send_config_set())
        {
            ESP_LOGW(kTag, "C6 CONFIG_SET send failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!receive_expected(TM_C6_FRAME_CONFIG_REPORT, frame, kHandshakeTimeoutMs) ||
            !handle_config_report(frame))
        {
            ESP_LOGW(kTag, "C6 CONFIG_REPORT failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        set_detail(status_, CompanionState::Present, "hello_ack_ping_pong_config_ok");
        ESP_LOGI(kTag,
                 "C6 present proto=%u fw=0x%08lx features=0x%08lx enabled=0x%08lx heap=%lu pongs=%lu",
                 status_.selected_protocol,
                 static_cast<unsigned long>(status_.firmware_version),
                 static_cast<unsigned long>(status_.supported_features),
                 static_cast<unsigned long>(status_.enabled_features),
                 static_cast<unsigned long>(status_.free_heap),
                 static_cast<unsigned long>(status_.pong_count));
        return true;
#else
        return false;
#endif
    }

    C6CompanionStatus status_{};
    uint16_t next_seq_ = kInitialSequence;
    uint32_t ble_passkey_ = 0; // random 6-digit BLE pairing PIN, persisted in NVS
    WirelessUplinkSink* uplink_sink_ = nullptr;
#if defined(ESP_PLATFORM)
    std::unique_ptr<C6Transport> transport_;
#endif
};

C6CompanionRuntime s_c6_companion{};

} // namespace

WirelessCompanion& c6_companion()
{
    return s_c6_companion;
}

bool ensure_c6_companion_started()
{
    return c6_companion().begin();
}

C6CompanionStatus get_c6_companion_status()
{
    return c6_companion().status();
}

const char* companion_state_name(CompanionState state)
{
    switch (state)
    {
    case CompanionState::Unsupported:
        return "unsupported";
    case CompanionState::NotStarted:
        return "not_started";
    case CompanionState::Missing:
        return "missing";
    case CompanionState::TransportPending:
        return "transport_pending";
    case CompanionState::Present:
        return "present";
    case CompanionState::Error:
        return "error";
    }
    return "unknown";
}

} // namespace platform::esp::idf_common::wireless_companion
