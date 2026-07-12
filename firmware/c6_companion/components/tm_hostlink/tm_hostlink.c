#include "tm_hostlink.h"
#include "tm_hostlink_sdio.h"

#include "hostlink/c6/c6_frame_codec_c.h"
#include "hostlink/c6/c6_protocol.h"
#include "tm_ble.h"
#include "tm_espnow.h"
#include "tm_services.h"
#include "tm_wifi.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tm_c6_companion_config.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "C6_HOSTLINK";

typedef enum tm_hostlink_state
{
    TM_HOSTLINK_WAIT_P4_HELLO = 0,
    TM_HOSTLINK_READY = 1,
    TM_HOSTLINK_ERROR = 2,
} tm_hostlink_state_t;

static tm_hostlink_state_t s_state = TM_HOSTLINK_WAIT_P4_HELLO;
static uint16_t s_selected_proto = 0;
static uint32_t s_supported_features = 0;
static uint16_t s_next_seq = 1;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint16_t next_seq(void)
{
    const uint16_t current = s_next_seq++;
    if (s_next_seq == 0)
    {
        s_next_seq = 1;
    }
    return current;
}

static uint16_t read_le16(const uint8_t* data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t* data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void write_le16(uint8_t* data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_le32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
    data[2] = (uint8_t)((value >> 16) & 0xFFu);
    data[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void build_hello_ack_payload(uint8_t* out)
{
    s_supported_features = tm_services_supported_features();
    write_le16(out, s_selected_proto);
    write_le32(out + 2, TM_C6_COMPANION_FIRMWARE_VERSION);
    write_le32(out + 6, s_supported_features);
    write_le16(out + 10, TM_C6_MAX_PAYLOAD);
    write_le16(out + 12, TM_C6_MAX_PAYLOAD);
    write_le32(out + 14, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

static bool send_frame(uint8_t frame_type,
                       uint8_t channel,
                       uint16_t flags,
                       uint16_t ack,
                       const uint8_t* payload,
                       size_t payload_len)
{
    uint8_t out[TM_C6_FRAME_HEADER_LEN + TM_C6_MAX_PAYLOAD] = {};
    size_t out_len = 0;
    tm_c6_encode_request_t request = {
        .frame_type = frame_type,
        .channel = channel,
        .flags = flags,
        .seq = next_seq(),
        .ack = ack,
        .payload = payload,
        .payload_len = payload_len,
    };

    if (!tm_c6_encode_frame(&request, out, sizeof(out), &out_len, TM_C6_MAX_PAYLOAD))
    {
        ESP_LOGE(TAG, "failed to encode frame type=%u payload_len=%u", frame_type, (unsigned)payload_len);
        return false;
    }

    const esp_err_t err = tm_hostlink_sdio_send(out, out_len, 100);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "sdio send frame type=%u failed: %s", frame_type, esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool send_error_frame(const tm_c6_frame_view_t* related,
                             tm_c6_error_code_t code,
                             const char* message)
{
    uint8_t payload[sizeof(tm_c6_error_t)] = {};
    const uint16_t related_seq = related ? related->seq : 0;
    const uint8_t related_type = related ? related->frame_type : 0;
    const uint8_t related_channel = related ? related->channel : TM_C6_CH_CONTROL;
    write_le16(payload, (uint16_t)code);
    write_le16(payload + 2, related_seq);
    payload[4] = related_type;
    payload[5] = related_channel;
    if (message != NULL)
    {
        snprintf((char*)payload + 6, sizeof(tm_c6_error_t) - 6, "%s", message);
    }
    return send_frame(TM_C6_FRAME_ERROR,
                      related_channel,
                      TM_C6_FLAG_ERROR,
                      related_seq,
                      payload,
                      sizeof(payload));
}

static bool services_hostlink_send(uint8_t frame_type,
                                   uint8_t channel,
                                   uint16_t flags,
                                   uint16_t ack,
                                   const uint8_t* payload,
                                   size_t payload_len,
                                   void* user)
{
    (void)user;
    return send_frame(frame_type, channel, flags, ack, payload, payload_len);
}

static bool handle_hello(const tm_c6_frame_view_t* frame)
{
    if (frame->channel != TM_C6_CH_CONTROL || frame->payload_len != sizeof(tm_c6_hello_t))
    {
        return send_error_frame(frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "bad_hello");
    }

    const uint16_t proto_min = read_le16(frame->payload);
    const uint16_t proto_max = read_le16(frame->payload + 2);
    const uint32_t requested_features = read_le32(frame->payload + 8);
    const uint16_t preferred_mtu = read_le16(frame->payload + 12);
    const uint16_t max_payload = read_le16(frame->payload + 14);

    if (proto_min > TM_C6_PROTO_VERSION || proto_max < TM_C6_PROTO_VERSION)
    {
        ESP_LOGW(TAG, "HELLO rejected proto range=%u..%u", proto_min, proto_max);
        return send_error_frame(frame, TM_C6_ERROR_BAD_VERSION, "protocol_mismatch");
    }

    s_selected_proto = TM_C6_PROTO_VERSION;
    uint8_t ack_payload[sizeof(tm_c6_hello_ack_t)] = {};
    build_hello_ack_payload(ack_payload);

    ESP_LOGI(TAG,
             "HELLO seq=%u requested=0x%08lx mtu=%u max_payload=%u selected_proto=%u",
             frame->seq,
             (unsigned long)requested_features,
             preferred_mtu,
             max_payload,
             s_selected_proto);
    return send_frame(TM_C6_FRAME_HELLO_ACK,
                      TM_C6_CH_CONTROL,
                      TM_C6_FLAG_IS_ACK,
                      frame->seq,
                      ack_payload,
                      sizeof(ack_payload));
}

static bool handle_ping(const tm_c6_frame_view_t* frame)
{
    if (frame->channel != TM_C6_CH_CONTROL || frame->payload_len != sizeof(tm_c6_ping_t))
    {
        return send_error_frame(frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "bad_ping");
    }

    uint8_t payload[sizeof(tm_c6_pong_t)] = {};
    const uint32_t nonce = read_le32(frame->payload);
    write_le32(payload, nonce);
    write_le32(payload + 4, uptime_ms());
    write_le32(payload + 8, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT));

    ESP_LOGI(TAG, "PING seq=%u nonce=0x%08lx", frame->seq, (unsigned long)nonce);
    return send_frame(TM_C6_FRAME_PONG,
                      TM_C6_CH_CONTROL,
                      TM_C6_FLAG_IS_ACK,
                      frame->seq,
                      payload,
                      sizeof(payload));
}

static bool handle_config_set(const tm_c6_frame_view_t* frame)
{
    if (frame->channel != TM_C6_CH_CONTROL || frame->payload_len != sizeof(tm_c6_companion_config_t))
    {
        return send_error_frame(frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "bad_config_set");
    }

    tm_c6_companion_config_t config = {};
    memcpy(&config, frame->payload, sizeof(config));

    tm_c6_config_report_t report = {};
    esp_err_t err = tm_services_apply_config(&config, &report);
    // Always call each service's apply -- including when its *_enabled flag is 0 --
    // so the disable transition actually runs (stop STA / terminate BLE). Each
    // apply is idempotent and handles the disabled state internally.
    if (err == ESP_OK)
    {
        err = tm_ble_apply_config(&config.ble);
        if (err != ESP_OK)
        {
            tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_config_failed");
            tm_services_fill_config_report(config.config_seq,
                                           TM_C6_ERROR_INTERNAL,
                                           "ble_config_failed",
                                           &report);
        }
    }
    if (err == ESP_OK)
    {
        err = tm_wifi_apply_config(&config.wifi);
        if (err != ESP_OK)
        {
            tm_services_record_error(TM_C6_ERROR_INTERNAL, "wifi_config_failed");
            tm_services_fill_config_report(config.config_seq,
                                           TM_C6_ERROR_INTERNAL,
                                           "wifi_config_failed",
                                           &report);
        }
    }
    if (err == ESP_OK)
    {
        err = tm_espnow_apply_config(&config.espnow);
        if (err != ESP_OK)
        {
            tm_services_record_error(TM_C6_ERROR_INTERNAL, "espnow_config_failed");
            tm_services_fill_config_report(config.config_seq,
                                           TM_C6_ERROR_INTERNAL,
                                           "espnow_config_failed",
                                           &report);
        }
    }

    ESP_LOGI(TAG,
             "CONFIG_SET seq=%u config_seq=%lu enabled=0x%08lx err=%s",
             frame->seq,
             (unsigned long)config.config_seq,
             (unsigned long)tm_services_enabled_features(),
             esp_err_to_name(err));
    return send_frame(TM_C6_FRAME_CONFIG_REPORT,
                      TM_C6_CH_CONTROL,
                      TM_C6_FLAG_IS_ACK,
                      frame->seq,
                      (const uint8_t*)&report,
                      sizeof(report));
}

static bool handle_config_get(const tm_c6_frame_view_t* frame)
{
    tm_c6_config_report_t report = {};
    tm_services_fill_config_report(0, TM_C6_OK, "config_report", &report);
    return send_frame(TM_C6_FRAME_CONFIG_REPORT,
                      TM_C6_CH_CONTROL,
                      TM_C6_FLAG_IS_ACK,
                      frame->seq,
                      (const uint8_t*)&report,
                      sizeof(report));
}

static bool handle_ble_downlink(const tm_c6_frame_view_t* frame)
{
    if (frame->payload_len < sizeof(tm_c6_ble_packet_header_t))
    {
        return send_error_frame(frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "bad_ble_downlink");
    }
    tm_c6_ble_packet_header_t header = {};
    memcpy(&header, frame->payload, sizeof(header));
    if (header.payload_len != frame->payload_len - sizeof(header))
    {
        return send_error_frame(frame, TM_C6_ERROR_PAYLOAD_TOO_LARGE, "bad_ble_len");
    }
    const esp_err_t err = tm_ble_send_downlink(header.profile,
                                               frame->payload + sizeof(header),
                                               header.payload_len);
    if (err != ESP_OK)
    {
        return send_error_frame(frame, TM_C6_ERROR_NOT_CONNECTED, "ble_downlink_failed");
    }
    return true;
}

static bool handle_espnow_downlink(const tm_c6_frame_view_t* frame)
{
    if (frame->channel != TM_C6_CH_ESPNOW_TEAM || frame->payload_len != sizeof(tm_c6_espnow_packet_t))
    {
        return send_error_frame(frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "bad_espnow_downlink");
    }
    tm_c6_espnow_packet_t packet = {};
    memcpy(&packet, frame->payload, sizeof(packet));
    const esp_err_t err = tm_espnow_send_packet(&packet);
    if (err != ESP_OK)
    {
        return send_error_frame(frame, TM_C6_ERROR_INTERNAL, "espnow_send_failed");
    }
    return true;
}

static bool handle_wifi_control(const tm_c6_frame_view_t* frame)
{
    if (frame->channel != TM_C6_CH_WIFI_MGMT || frame->payload_len != sizeof(tm_c6_wifi_control_t))
    {
        return send_error_frame(frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "bad_wifi_control");
    }
    tm_c6_wifi_control_t control = {};
    memcpy(&control, frame->payload, sizeof(control));
    const esp_err_t err = tm_wifi_handle_control(&control);
    if (err != ESP_OK)
    {
        return send_error_frame(frame, TM_C6_ERROR_INTERNAL, "wifi_control_failed");
    }
    return true;
}

static bool handle_ble_control(const tm_c6_frame_view_t* frame)
{
    if (frame->channel != TM_C6_CH_CONTROL || frame->payload_len != sizeof(tm_c6_ble_control_t))
    {
        return send_error_frame(frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "bad_ble_control");
    }
    tm_c6_ble_control_t control = {};
    memcpy(&control, frame->payload, sizeof(control));
    const esp_err_t err = tm_ble_handle_control(&control);
    if (err != ESP_OK)
    {
        return send_error_frame(frame, TM_C6_ERROR_INTERNAL, "ble_control_failed");
    }
    return true;
}

static bool handle_diag_request(const tm_c6_frame_view_t* frame)
{
    tm_c6_diag_report_t report = {};
    tm_services_fill_diag_report((uint8_t)s_state, &report);
    return send_frame(TM_C6_FRAME_DIAG_REPORT,
                      TM_C6_CH_DIAG,
                      TM_C6_FLAG_IS_ACK,
                      frame->seq,
                      (const uint8_t*)&report,
                      sizeof(report));
}

static void hostlink_task(void* arg)
{
    (void)arg;
    uint8_t rx[TM_C6_FRAME_HEADER_LEN + TM_C6_MAX_PAYLOAD] = {};

    for (;;)
    {
        tm_hostlink_sdio_poll_tx_done();
        size_t rx_len = 0;
        const esp_err_t err = tm_hostlink_sdio_recv(rx, sizeof(rx), &rx_len, 100);
        if (err == ESP_ERR_TIMEOUT)
        {
            continue;
        }
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "sdio recv failed: %s", esp_err_to_name(err));
            continue;
        }

        const tm_c6_decode_result_t decoded = tm_c6_decode_frame(rx, rx_len, TM_C6_MAX_PAYLOAD);
        if (decoded.status != TM_C6_DECODE_OK)
        {
            ESP_LOGW(TAG, "bad frame decode=%s len=%u", tm_c6_decode_status_name(decoded.status), (unsigned)rx_len);
            (void)send_error_frame(NULL, tm_c6_decode_status_error_code(decoded.status), tm_c6_decode_status_name(decoded.status));
            continue;
        }

        switch (decoded.frame.frame_type)
        {
        case TM_C6_FRAME_HELLO:
            if (handle_hello(&decoded.frame))
            {
                s_state = TM_HOSTLINK_READY;
            }
            break;
        case TM_C6_FRAME_PING:
            if (s_state == TM_HOSTLINK_READY)
            {
                (void)handle_ping(&decoded.frame);
            }
            else
            {
                (void)send_error_frame(&decoded.frame, TM_C6_ERROR_NOT_CONNECTED, "hello_required");
            }
            break;
        case TM_C6_FRAME_CONFIG_SET:
            if (s_state == TM_HOSTLINK_READY)
            {
                (void)handle_config_set(&decoded.frame);
            }
            else
            {
                (void)send_error_frame(&decoded.frame, TM_C6_ERROR_NOT_CONNECTED, "hello_required");
            }
            break;
        case TM_C6_FRAME_CONFIG_GET:
            (void)handle_config_get(&decoded.frame);
            break;
        case TM_C6_FRAME_BLE_DOWNLINK:
            (void)handle_ble_downlink(&decoded.frame);
            break;
        case TM_C6_FRAME_ESPNOW_DOWNLINK:
            (void)handle_espnow_downlink(&decoded.frame);
            break;
        case TM_C6_FRAME_WIFI_CONTROL:
            (void)handle_wifi_control(&decoded.frame);
            break;
        case TM_C6_FRAME_BLE_CONTROL:
            (void)handle_ble_control(&decoded.frame);
            break;
        case TM_C6_FRAME_DIAG_REQUEST:
            (void)handle_diag_request(&decoded.frame);
            break;
        default:
            (void)send_error_frame(&decoded.frame, TM_C6_ERROR_UNSUPPORTED_FRAME, "unsupported_frame");
            break;
        }
    }
}

esp_err_t tm_hostlink_init(void)
{
    ESP_LOGI(TAG,
             "HostLink Phase 1 core initialized magic=0x%08lx header=%u max_payload=%u features=0x%08lx",
             (unsigned long)TM_C6_MAGIC,
             TM_C6_FRAME_HEADER_LEN,
             TM_C6_MAX_PAYLOAD,
             (unsigned long)s_supported_features);
    s_state = TM_HOSTLINK_WAIT_P4_HELLO;
    s_selected_proto = 0;
    s_next_seq = 1;
    s_supported_features = tm_services_supported_features();
    const tm_services_hostlink_t hostlink = {
        .send = services_hostlink_send,
        .user = NULL,
    };
    tm_services_bind_hostlink(&hostlink);
    return ESP_OK;
}

esp_err_t tm_hostlink_start(void)
{
    ESP_LOGI(TAG,
             "WAIT_P4_HELLO transport=sdio target=esp_hosted_or_hostlink selected_proto=%u uptime_ms=%lu",
             s_selected_proto,
             (unsigned long)uptime_ms());
    ESP_LOGI(TAG,
             "HELLO_ACK template proto=%u fw=0x%08lx features=0x%08lx mtu=%u max_payload=%u heap=%lu",
             TM_C6_PROTO_VERSION,
             (unsigned long)0x00010000u,
             (unsigned long)s_supported_features,
             TM_C6_MAX_PAYLOAD,
             TM_C6_MAX_PAYLOAD,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG,
             "PONG template mirrors ping nonce and reports uptime_ms/free_heap; wireless services remain disabled");
    esp_err_t err = tm_hostlink_sdio_init();
    if (err != ESP_OK)
    {
        s_state = TM_HOSTLINK_ERROR;
        return err;
    }
    ESP_LOGI(TAG, "SDIO transport active; waiting for real P4 HELLO");
    BaseType_t task_ok = xTaskCreate(hostlink_task, "tm_hostlink", 4096, NULL, 10, NULL);
    if (task_ok != pdPASS)
    {
        s_state = TM_HOSTLINK_ERROR;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "SDIO HostLink task started; waiting for real P4 HELLO");
    return ESP_OK;
}

const char* tm_hostlink_state_name(void)
{
    switch (s_state)
    {
    case TM_HOSTLINK_WAIT_P4_HELLO:
        return "wait_p4_hello";
    case TM_HOSTLINK_READY:
        return "ready";
    case TM_HOSTLINK_ERROR:
        return "error";
    }
    return "unknown";
}
