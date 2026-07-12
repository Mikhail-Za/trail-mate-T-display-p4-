#include "tm_wifi.h"

#include "tm_services.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "C6_WIFI";

static bool s_netif_ready;
static bool s_wifi_initialized;
static bool s_wifi_started;
static esp_netif_t* s_sta_netif;
static esp_netif_t* s_ap_netif;
static tm_c6_wifi_config_t s_config;
// op_id of the most recent control command, stamped onto every event we emit so
// the P4 can correlate an async reply to the operation that caused it.
static uint16_t s_current_op_id;

static void copy_text(char* out, size_t out_len, const char* text)
{
    if (out == NULL || out_len == 0)
    {
        return;
    }
    snprintf(out, out_len, "%s", text ? text : "");
}

static void emit_simple_event(uint8_t kind, uint16_t error_code, const char* ssid, uint32_t ipv4_addr)
{
    tm_c6_wifi_event_t event = {
        .event_kind = kind,
        .result_count = 0,
        .error_code = error_code,
        .op_id = s_current_op_id,
        .ipv4_addr = ipv4_addr,
    };
    copy_text(event.ssid, sizeof(event.ssid), ssid);
    (void)tm_services_send_wifi_event(&event);
}

static wifi_mode_t configured_mode(void)
{
    const bool sta = s_config.wifi_enabled && s_config.sta_enabled;
    const bool ap = s_config.wifi_enabled && s_config.ap_enabled;
    if (sta && ap)
    {
        return WIFI_MODE_APSTA;
    }
    if (ap)
    {
        return WIFI_MODE_AP;
    }
    return WIFI_MODE_STA;
}

static esp_err_t ensure_netif(void)
{
    if (s_netif_ready)
    {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (s_sta_netif == NULL)
    {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (s_ap_netif == NULL)
    {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    s_netif_ready = true;
    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_CONNECTED:
        {
            const wifi_event_sta_connected_t* event = (const wifi_event_sta_connected_t*)event_data;
            char ssid[TM_C6_WIFI_SSID_LEN] = {};
            const size_t copy_len = event && event->ssid_len < sizeof(ssid) ? event->ssid_len : sizeof(ssid) - 1;
            if (event != NULL)
            {
                memcpy(ssid, event->ssid, copy_len);
            }
            emit_simple_event(TM_C6_WIFI_EVENT_STA_CONNECTED, TM_C6_OK, ssid, 0);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            const wifi_event_sta_disconnected_t* event =
                (const wifi_event_sta_disconnected_t*)event_data;
            char ssid[TM_C6_WIFI_SSID_LEN] = {};
            uint16_t reason = TM_C6_OK;
            if (event != NULL)
            {
                const size_t copy_len =
                    event->ssid_len < sizeof(ssid) ? event->ssid_len : sizeof(ssid) - 1;
                memcpy(ssid, event->ssid, copy_len);
                reason = event->reason; // 802.11 reason, for the P4 to correlate/map
            }
            // The SSID lets the P4 discard a stale disconnect for a superseded AP.
            emit_simple_event(TM_C6_WIFI_EVENT_STA_DISCONNECTED, reason, ssid, 0);
            break;
        }
        case WIFI_EVENT_AP_START:
            emit_simple_event(TM_C6_WIFI_EVENT_AP_STARTED, TM_C6_OK, s_config.ap_ssid, 0);
            break;
        case WIFI_EVENT_AP_STOP:
            emit_simple_event(TM_C6_WIFI_EVENT_AP_STOPPED, TM_C6_OK, s_config.ap_ssid, 0);
            break;
        default:
            break;
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t* event = (const ip_event_got_ip_t*)event_data;
        uint32_t ipv4 = 0;
        if (event != NULL)
        {
            ipv4 = event->ip_info.ip.addr;
        }
        emit_simple_event(TM_C6_WIFI_EVENT_STA_GOT_IP, TM_C6_OK, s_config.sta_ssid, ipv4);
    }
}

static esp_err_t ensure_wifi_initialized(void)
{
    if (s_wifi_initialized)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_netif(), TAG, "netif");
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi_storage_ram");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));
    s_wifi_initialized = true;
    return ESP_OK;
}

static esp_err_t ensure_wifi_started_with_mode(wifi_mode_t mode)
{
    ESP_RETURN_ON_ERROR(ensure_wifi_initialized(), TAG, "wifi_init");

    if (s_wifi_started)
    {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(mode), TAG, "wifi_set_mode_started");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(mode), TAG, "wifi_set_mode");
    esp_err_t err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_CONN)
    {
        return err;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "wifi_start");
    s_wifi_started = true;
    emit_simple_event(TM_C6_WIFI_EVENT_STARTED, TM_C6_OK, NULL, 0);
    return ESP_OK;
}

esp_err_t tm_wifi_init(void)
{
    memset(&s_config, 0, sizeof(s_config));
    return ESP_OK;
}

esp_err_t tm_wifi_ensure_radio_started(void)
{
    return ensure_wifi_started_with_mode(configured_mode());
}

esp_err_t tm_wifi_apply_config(const tm_c6_wifi_config_t* config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    if (!config->wifi_enabled)
    {
        // Real "Wi-Fi off": drop the association and clear stored STA creds so it
        // will not auto-reconnect. Keep the Wi-Fi driver running so ESP-NOW (which
        // shares the radio) is unaffected; a full radio stop is coordinated with
        // ESP-NOW ownership separately.
        if (s_wifi_started)
        {
            (void)esp_wifi_disconnect();
            wifi_config_t empty_sta = {};
            (void)esp_wifi_set_config(WIFI_IF_STA, &empty_sta);
            emit_simple_event(TM_C6_WIFI_EVENT_STA_DISCONNECTED, TM_C6_OK, NULL, 0);
        }
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_wifi_started_with_mode(configured_mode()), TAG, "wifi_start_config");

    if (config->ap_enabled && config->ap_ssid[0] != '\0')
    {
        wifi_config_t ap_config = {};
        copy_text((char*)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), config->ap_ssid);
        copy_text((char*)ap_config.ap.password, sizeof(ap_config.ap.password), config->ap_password);
        ap_config.ap.ssid_len = strlen((const char*)ap_config.ap.ssid);
        ap_config.ap.channel = config->ap_channel ? config->ap_channel : 1;
        ap_config.ap.max_connection = 4;
        ap_config.ap.authmode = config->ap_password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "ap_config");
    }

    if (config->sta_enabled && config->sta_ssid[0] != '\0')
    {
        wifi_config_t sta_config = {};
        copy_text((char*)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), config->sta_ssid);
        copy_text((char*)sta_config.sta.password, sizeof(sta_config.sta.password), config->sta_password);
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "sta_config");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    }

    return ESP_OK;
}

static esp_err_t emit_scan_results(void)
{
    ESP_RETURN_ON_ERROR(ensure_wifi_started_with_mode(WIFI_MODE_STA), TAG, "wifi_scan_start_mode");
    wifi_scan_config_t scan_config = {};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK)
    {
        tm_services_record_error(TM_C6_ERROR_INTERNAL, "wifi_scan_failed");
        emit_simple_event(TM_C6_WIFI_EVENT_ERROR, TM_C6_ERROR_INTERNAL, NULL, 0);
        return err;
    }

    uint16_t ap_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "scan_get_ap_num");
    wifi_ap_record_t records[TM_C6_WIFI_SCAN_RESULT_COUNT] = {};
    uint16_t result_count = TM_C6_WIFI_SCAN_RESULT_COUNT;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&result_count, records), TAG, "scan_get_records");

    tm_c6_wifi_event_t event = {
        .event_kind = TM_C6_WIFI_EVENT_SCAN_DONE,
        .result_count = (uint8_t)result_count,
        .error_code = TM_C6_OK,
        .op_id = s_current_op_id,
    };
    for (uint16_t i = 0; i < result_count && i < TM_C6_WIFI_SCAN_RESULT_COUNT; ++i)
    {
        copy_text(event.results[i].ssid, sizeof(event.results[i].ssid), (const char*)records[i].ssid);
        event.results[i].rssi = records[i].rssi;
        event.results[i].channel = records[i].primary;
        event.results[i].authmode = records[i].authmode;
    }
    (void)ap_count;
    (void)tm_services_send_wifi_event(&event);
    return ESP_OK;
}

esp_err_t tm_wifi_handle_control(const tm_c6_wifi_control_t* control)
{
    if (control == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_current_op_id = control->op_id;

    switch ((tm_c6_wifi_command_t)control->command)
    {
    case TM_C6_WIFI_CMD_SCAN:
        return emit_scan_results();
    case TM_C6_WIFI_CMD_STA_ENABLE:
        s_config.wifi_enabled = 1;
        s_config.sta_enabled = 1;
        return tm_wifi_apply_config(&s_config); // STA up, idle until a CONNECT
    case TM_C6_WIFI_CMD_STA_DISABLE:
        s_config.wifi_enabled = 0;
        return tm_wifi_apply_config(&s_config); // disconnect + clear creds
    case TM_C6_WIFI_CMD_CONNECT:
    {
        s_config.wifi_enabled = 1;
        s_config.sta_enabled = 1;
        copy_text(s_config.sta_ssid, sizeof(s_config.sta_ssid), control->ssid);
        copy_text(s_config.sta_password, sizeof(s_config.sta_password), control->password);
        s_config.sta_channel = control->channel;
        return tm_wifi_apply_config(&s_config);
    }
    case TM_C6_WIFI_CMD_DISCONNECT:
        return esp_wifi_disconnect();
    case TM_C6_WIFI_CMD_GET_IP:
    {
        esp_netif_ip_info_t ip_info = {};
        if (s_sta_netif != NULL && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK)
        {
            emit_simple_event(TM_C6_WIFI_EVENT_STA_GOT_IP, TM_C6_OK, s_config.sta_ssid, ip_info.ip.addr);
        }
        return ESP_OK;
    }
    case TM_C6_WIFI_CMD_AP_START:
        s_config.wifi_enabled = 1;
        s_config.ap_enabled = 1;
        copy_text(s_config.ap_ssid, sizeof(s_config.ap_ssid), control->ssid);
        copy_text(s_config.ap_password, sizeof(s_config.ap_password), control->password);
        s_config.ap_channel = control->channel;
        return tm_wifi_apply_config(&s_config);
    case TM_C6_WIFI_CMD_AP_STOP:
        s_config.ap_enabled = 0;
        return ensure_wifi_started_with_mode(configured_mode());
    default:
        tm_services_record_error(TM_C6_ERROR_UNSUPPORTED_FRAME, "unsupported_wifi_command");
        emit_simple_event(TM_C6_WIFI_EVENT_ERROR, TM_C6_ERROR_UNSUPPORTED_FRAME, NULL, 0);
        return ESP_ERR_NOT_SUPPORTED;
    }
}
