#pragma once

#include <stdint.h>

#define TM_C6_MAGIC 0x36434D54u
// v2: adds runtime BLE_CONTROL commands, Wi-Fi STA enable/disable, and an echoed
// op_id on the Wi-Fi control/event structs (a wire-size change). Bumped as a hard
// break so a mismatched P4/C6 pair fails at HELLO rather than misreading payloads.
#define TM_C6_PROTO_VERSION 2u
#define TM_C6_PROTO_MIN 2u
#define TM_C6_PROTO_MAX 2u
#define TM_C6_FRAME_HEADER_LEN 20u
#define TM_C6_MAX_PAYLOAD 1024u

#define TM_C6_FLAG_ACK_REQUIRED (1u << 0)
#define TM_C6_FLAG_IS_ACK (1u << 1)
#define TM_C6_FLAG_IS_FRAGMENT (1u << 2)
#define TM_C6_FLAG_FRAGMENT_START (1u << 3)
#define TM_C6_FLAG_FRAGMENT_END (1u << 4)
#define TM_C6_FLAG_ERROR (1u << 5)

#define TM_C6_FEATURE_BLE_MESHTASTIC (1u << 0)
#define TM_C6_FEATURE_BLE_MESHCORE (1u << 1)
#define TM_C6_FEATURE_BLE_TRAILMATE (1u << 2)
#define TM_C6_FEATURE_ESPNOW_TEAM (1u << 3)
#define TM_C6_FEATURE_WIFI_STA (1u << 4)
#define TM_C6_FEATURE_WIFI_AP (1u << 5)
#define TM_C6_FEATURE_DIAG_LOG (1u << 6)
#define TM_C6_FEATURE_HOSTLINK_PING (1u << 7)
#define TM_C6_FEATURE_SLAVE_OTA (1u << 8)

#define TM_C6_P4_FIRMWARE_VERSION_UNKNOWN 0u

#define TM_C6_BLE_DEVICE_NAME_LEN 32u
#define TM_C6_BLE_FIXED_PIN_LEN 8u
#define TM_C6_WIFI_SSID_LEN 32u
#define TM_C6_WIFI_PASSWORD_LEN 64u
#define TM_C6_WIFI_SCAN_RESULT_COUNT 6u
#define TM_C6_ESPNOW_PAYLOAD_MAX 240u
#define TM_C6_BLE_PACKET_HEADER_LEN 4u

#if defined(_MSC_VER)
#define TM_C6_PACKED_BEGIN __pragma(pack(push, 1))
#define TM_C6_PACKED_END __pragma(pack(pop))
#define TM_C6_PACKED
#else
#define TM_C6_PACKED_BEGIN
#define TM_C6_PACKED_END
#define TM_C6_PACKED __attribute__((packed))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum tm_c6_frame_type
    {
        TM_C6_FRAME_HELLO = 0x01,
        TM_C6_FRAME_HELLO_ACK = 0x02,
        TM_C6_FRAME_PING = 0x03,
        TM_C6_FRAME_PONG = 0x04,
        TM_C6_FRAME_ACK = 0x05,
        TM_C6_FRAME_ERROR = 0x06,

        TM_C6_FRAME_CONFIG_SET = 0x10,
        TM_C6_FRAME_CONFIG_GET = 0x11,
        TM_C6_FRAME_CONFIG_REPORT = 0x12,

        TM_C6_FRAME_BLE_UPLINK = 0x20,
        TM_C6_FRAME_BLE_DOWNLINK = 0x21,
        TM_C6_FRAME_BLE_EVENT = 0x22,
        TM_C6_FRAME_BLE_CONTROL = 0x23,

        TM_C6_FRAME_ESPNOW_UPLINK = 0x30,
        TM_C6_FRAME_ESPNOW_DOWNLINK = 0x31,
        TM_C6_FRAME_ESPNOW_EVENT = 0x32,
        TM_C6_FRAME_ESPNOW_CONTROL = 0x33,

        TM_C6_FRAME_WIFI_CONTROL = 0x40,
        TM_C6_FRAME_WIFI_EVENT = 0x41,
        TM_C6_FRAME_WIFI_DATA = 0x42,

        TM_C6_FRAME_DIAG_REQUEST = 0x50,
        TM_C6_FRAME_DIAG_REPORT = 0x51,
        TM_C6_FRAME_LOG = 0x52,
    } tm_c6_frame_type_t;

    typedef enum tm_c6_channel
    {
        TM_C6_CH_CONTROL = 0x00,
        TM_C6_CH_BLE_MESHTASTIC = 0x01,
        TM_C6_CH_BLE_MESHCORE = 0x02,
        TM_C6_CH_BLE_TRAILMATE = 0x03,
        TM_C6_CH_ESPNOW_TEAM = 0x04,
        TM_C6_CH_WIFI_MGMT = 0x05,
        TM_C6_CH_WIFI_DATA = 0x06,
        TM_C6_CH_DIAG = 0x07,
    } tm_c6_channel_t;

    typedef enum tm_c6_error_code
    {
        TM_C6_OK = 0,
        TM_C6_ERROR_BAD_MAGIC = 1,
        TM_C6_ERROR_BAD_VERSION = 2,
        TM_C6_ERROR_BAD_CRC = 3,
        TM_C6_ERROR_PAYLOAD_TOO_LARGE = 4,
        TM_C6_ERROR_UNSUPPORTED_FRAME = 5,
        TM_C6_ERROR_UNSUPPORTED_CHANNEL = 6,
        TM_C6_ERROR_UNSUPPORTED_FEATURE = 7,
        TM_C6_ERROR_PROFILE_NOT_CONFIGURED = 8,
        TM_C6_ERROR_NOT_CONNECTED = 9,
        TM_C6_ERROR_QUEUE_FULL = 10,
        TM_C6_ERROR_LOW_MEMORY = 11,
        TM_C6_ERROR_TIMEOUT = 12,
        TM_C6_ERROR_INTERNAL = 13,
    } tm_c6_error_code_t;

    typedef enum tm_c6_pairing_mode
    {
        TM_C6_PAIRING_DISABLED = 0,
        TM_C6_PAIRING_RANDOM_PIN = 1,
        TM_C6_PAIRING_FIXED_PIN = 2,
        TM_C6_PAIRING_NO_PIN_DEBUG_ONLY = 3,
    } tm_c6_pairing_mode_t;

    typedef enum tm_c6_ble_profile
    {
        TM_C6_BLE_PROFILE_NONE = 0,
        TM_C6_BLE_PROFILE_MESHTASTIC = 1,
        TM_C6_BLE_PROFILE_MESHCORE = 2,
        TM_C6_BLE_PROFILE_TRAILMATE = 3,
    } tm_c6_ble_profile_t;

    typedef enum tm_c6_service_state
    {
        TM_C6_SERVICE_DISABLED = 0,
        TM_C6_SERVICE_STARTING = 1,
        TM_C6_SERVICE_READY = 2,
        TM_C6_SERVICE_ERROR = 3,
        TM_C6_SERVICE_NOT_CONFIGURED = 4,
    } tm_c6_service_state_t;

    typedef enum tm_c6_ble_event_kind
    {
        TM_C6_BLE_EVENT_STARTED = 1,
        TM_C6_BLE_EVENT_STOPPED = 2,
        TM_C6_BLE_EVENT_CONNECTED = 3,
        TM_C6_BLE_EVENT_DISCONNECTED = 4,
        TM_C6_BLE_EVENT_MTU_CHANGED = 5,
        TM_C6_BLE_EVENT_NOTIFY_DROPPED = 6,
    } tm_c6_ble_event_kind_t;

    // Runtime BLE control commands, carried in a TM_C6_FRAME_BLE_CONTROL (0x23)
    // frame. These let the P4 manage the peripheral at runtime without replaying
    // the whole CONFIG_SET (which resets unrelated radios and is skipped for
    // disabled services by the dispatcher).
    typedef enum tm_c6_ble_command
    {
        TM_C6_BLE_CMD_ENABLE = 1,           // start advertising / accept connections
        TM_C6_BLE_CMD_DISABLE = 2,          // terminate connection + stop advertising
        TM_C6_BLE_CMD_SET_PROFILE_MASK = 3, // enable subset of meshtastic/meshcore/trailmate
        TM_C6_BLE_CMD_SET_PIN = 4,          // set the fixed pairing PIN
        TM_C6_BLE_CMD_BOND_RESET = 5,       // clear bonds + terminate -> forces re-pair
        TM_C6_BLE_CMD_DISCONNECT = 6,       // drop the current connection, keep advertising
    } tm_c6_ble_command_t;

    // profile_mask bits for TM_C6_BLE_CMD_SET_PROFILE_MASK.
    #define TM_C6_BLE_PROFILE_MASK_MESHTASTIC (1u << 0)
    #define TM_C6_BLE_PROFILE_MASK_MESHCORE (1u << 1)
    #define TM_C6_BLE_PROFILE_MASK_TRAILMATE (1u << 2)

    typedef enum tm_c6_espnow_event_kind
    {
        TM_C6_ESPNOW_EVENT_STARTED = 1,
        TM_C6_ESPNOW_EVENT_STOPPED = 2,
        TM_C6_ESPNOW_EVENT_SEND_DONE = 3,
        TM_C6_ESPNOW_EVENT_RECV_DROPPED = 4,
    } tm_c6_espnow_event_kind_t;

    typedef enum tm_c6_wifi_command
    {
        TM_C6_WIFI_CMD_SCAN = 1,
        TM_C6_WIFI_CMD_CONNECT = 2,
        TM_C6_WIFI_CMD_DISCONNECT = 3,
        TM_C6_WIFI_CMD_GET_IP = 4,
        TM_C6_WIFI_CMD_AP_START = 5,
        TM_C6_WIFI_CMD_AP_STOP = 6,
        TM_C6_WIFI_CMD_STA_ENABLE = 7,  // bring the STA interface up (idle, no assoc)
        TM_C6_WIFI_CMD_STA_DISABLE = 8, // disconnect + stop STA (radio off)
    } tm_c6_wifi_command_t;

    typedef enum tm_c6_wifi_event_kind
    {
        TM_C6_WIFI_EVENT_STARTED = 1,
        TM_C6_WIFI_EVENT_STOPPED = 2,
        TM_C6_WIFI_EVENT_SCAN_DONE = 3,
        TM_C6_WIFI_EVENT_STA_CONNECTED = 4,
        TM_C6_WIFI_EVENT_STA_DISCONNECTED = 5,
        TM_C6_WIFI_EVENT_STA_GOT_IP = 6,
        TM_C6_WIFI_EVENT_AP_STARTED = 7,
        TM_C6_WIFI_EVENT_AP_STOPPED = 8,
        TM_C6_WIFI_EVENT_ERROR = 9,
    } tm_c6_wifi_event_kind_t;

    TM_C6_PACKED_BEGIN
    typedef struct TM_C6_PACKED tm_c6_frame_header
    {
        uint32_t magic;
        uint8_t version;
        uint8_t header_len;
        uint8_t frame_type;
        uint8_t channel;
        uint16_t flags;
        uint16_t seq;
        uint16_t ack;
        uint16_t payload_len;
        uint32_t crc32;
    } tm_c6_frame_header_t;

    typedef struct TM_C6_PACKED tm_c6_hello
    {
        uint16_t proto_version_min;
        uint16_t proto_version_max;
        uint32_t p4_firmware_version;
        uint32_t requested_features;
        uint16_t preferred_mtu;
        uint16_t max_payload;
    } tm_c6_hello_t;

    typedef struct TM_C6_PACKED tm_c6_hello_ack
    {
        uint16_t selected_proto_version;
        uint32_t c6_firmware_version;
        uint32_t supported_features;
        uint16_t selected_mtu;
        uint16_t max_payload;
        uint32_t c6_free_heap;
    } tm_c6_hello_ack_t;

    typedef struct TM_C6_PACKED tm_c6_error
    {
        uint16_t error_code;
        uint16_t related_seq;
        uint8_t related_frame_type;
        uint8_t related_channel;
        char message[64];
    } tm_c6_error_t;

    typedef struct TM_C6_PACKED tm_c6_ping
    {
        uint32_t nonce;
        uint32_t uptime_ms;
    } tm_c6_ping_t;

    typedef struct TM_C6_PACKED tm_c6_pong
    {
        uint32_t nonce;
        uint32_t uptime_ms;
        uint32_t free_heap;
    } tm_c6_pong_t;

    typedef struct TM_C6_PACKED tm_c6_ble_config
    {
        uint8_t ble_enabled;
        uint8_t meshtastic_enabled;
        uint8_t meshcore_enabled;
        uint8_t trailmate_enabled;
        uint8_t pairing_mode;
        uint8_t fixed_pin_enabled;
        char fixed_pin[TM_C6_BLE_FIXED_PIN_LEN];
        char device_name[TM_C6_BLE_DEVICE_NAME_LEN];
        uint16_t preferred_mtu;
    } tm_c6_ble_config_t;

    typedef struct TM_C6_PACKED tm_c6_espnow_config
    {
        uint8_t espnow_enabled;
        uint8_t team_discovery_enabled;
        uint8_t channel;
        uint8_t reserved;
        uint16_t beacon_interval_ms;
        uint8_t broadcast_mac[6];
    } tm_c6_espnow_config_t;

    typedef struct TM_C6_PACKED tm_c6_wifi_config
    {
        uint8_t wifi_enabled;
        uint8_t sta_enabled;
        uint8_t ap_enabled;
        uint8_t persist_credentials;
        char sta_ssid[TM_C6_WIFI_SSID_LEN];
        char sta_password[TM_C6_WIFI_PASSWORD_LEN];
        char ap_ssid[TM_C6_WIFI_SSID_LEN];
        char ap_password[TM_C6_WIFI_PASSWORD_LEN];
        uint8_t sta_channel;
        uint8_t ap_channel;
    } tm_c6_wifi_config_t;

    typedef struct TM_C6_PACKED tm_c6_companion_config
    {
        uint32_t config_seq;
        uint32_t requested_features;
        tm_c6_ble_config_t ble;
        tm_c6_espnow_config_t espnow;
        tm_c6_wifi_config_t wifi;
    } tm_c6_companion_config_t;

    typedef struct TM_C6_PACKED tm_c6_config_report
    {
        uint32_t config_seq;
        uint32_t supported_features;
        uint32_t enabled_features;
        uint32_t c6_free_heap;
        uint16_t error_code;
        uint8_t ble_state;
        uint8_t espnow_state;
        uint8_t wifi_state;
        uint8_t hostlink_state;
        uint16_t selected_mtu;
        char detail[64];
    } tm_c6_config_report_t;

    typedef struct TM_C6_PACKED tm_c6_ble_packet_header
    {
        uint8_t profile;
        uint8_t connection_id;
        uint16_t payload_len;
    } tm_c6_ble_packet_header_t;

    typedef struct TM_C6_PACKED tm_c6_ble_event
    {
        uint8_t event_kind;
        uint8_t profile;
        uint8_t connection_id;
        uint8_t state;
        uint16_t mtu;
        uint16_t error_code;
    } tm_c6_ble_event_t;

    typedef struct TM_C6_PACKED tm_c6_ble_control
    {
        uint8_t command;      // tm_c6_ble_command_t
        uint8_t profile_mask; // SET_PROFILE_MASK: TM_C6_BLE_PROFILE_MASK_* bits
        uint16_t config_seq;  // echoed in CONFIG_REPORT so the P4 can correlate
        char pin[TM_C6_BLE_FIXED_PIN_LEN]; // SET_PIN: 6-digit ASCII PIN
    } tm_c6_ble_control_t;

    typedef struct TM_C6_PACKED tm_c6_espnow_packet
    {
        uint8_t peer_mac[6];
        uint8_t rssi_valid;
        int8_t rssi;
        uint8_t channel;
        uint8_t payload_len;
        uint8_t payload[TM_C6_ESPNOW_PAYLOAD_MAX];
    } tm_c6_espnow_packet_t;

    typedef struct TM_C6_PACKED tm_c6_espnow_event
    {
        uint8_t event_kind;
        uint8_t status;
        uint8_t peer_mac[6];
        uint16_t error_code;
    } tm_c6_espnow_event_t;

    typedef struct TM_C6_PACKED tm_c6_wifi_control
    {
        uint8_t command;
        uint8_t flags;
        uint16_t op_id; // P4-assigned operation id, echoed on the resulting events
        char ssid[TM_C6_WIFI_SSID_LEN];
        char password[TM_C6_WIFI_PASSWORD_LEN];
        uint8_t channel;
    } tm_c6_wifi_control_t;

    typedef struct TM_C6_PACKED tm_c6_wifi_scan_result
    {
        char ssid[TM_C6_WIFI_SSID_LEN];
        int8_t rssi;
        uint8_t channel;
        uint8_t authmode;
        uint8_t reserved;
    } tm_c6_wifi_scan_result_t;

    typedef struct TM_C6_PACKED tm_c6_wifi_event
    {
        uint8_t event_kind;
        uint8_t result_count;
        uint16_t error_code;
        uint16_t op_id; // echoes the tm_c6_wifi_control op_id that caused this event
        uint16_t reserved;
        uint32_t ipv4_addr;
        char ssid[TM_C6_WIFI_SSID_LEN];
        tm_c6_wifi_scan_result_t results[TM_C6_WIFI_SCAN_RESULT_COUNT];
    } tm_c6_wifi_event_t;

    typedef struct TM_C6_PACKED tm_c6_diag_report
    {
        uint32_t uptime_ms;
        uint32_t free_heap;
        uint32_t minimum_free_heap;
        uint32_t supported_features;
        uint32_t enabled_features;
        uint32_t ble_uplink_count;
        uint32_t ble_downlink_count;
        uint32_t espnow_rx_count;
        uint32_t espnow_tx_count;
        uint32_t wifi_event_count;
        uint16_t last_error_code;
        uint8_t hostlink_state;
        uint8_t reserved;
        char last_error[64];
    } tm_c6_diag_report_t;
    TM_C6_PACKED_END

#ifdef __cplusplus
}

static_assert(sizeof(tm_c6_frame_header_t) == TM_C6_FRAME_HEADER_LEN,
              "tm_c6_frame_header_t must stay wire-compatible");
static_assert(sizeof(tm_c6_ble_packet_header_t) == TM_C6_BLE_PACKET_HEADER_LEN,
              "tm_c6_ble_packet_header_t must stay wire-compatible");
#endif
