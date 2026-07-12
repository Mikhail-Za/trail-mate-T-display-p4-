#include "tm_ble.h"

#include "tm_services.h"

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "C6_BLE";

void ble_store_config_init(void);

static const ble_uuid128_t UUID_MT_SERVICE =
    BLE_UUID128_INIT(0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f,
                     0x1f, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b);
static const ble_uuid128_t UUID_MT_TO_RADIO =
    BLE_UUID128_INIT(0xe7, 0x01, 0x44, 0x12, 0x66, 0x78, 0xdd, 0xa1,
                     0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0x5c, 0xf7);
static const ble_uuid128_t UUID_MT_FROM_NUM =
    BLE_UUID128_INIT(0x53, 0x44, 0xe3, 0x47, 0x75, 0xaa, 0x70, 0xa6,
                     0x66, 0x4f, 0x00, 0xa8, 0x8c, 0xa1, 0x9d, 0xed);
static const ble_uuid128_t UUID_MT_FROM_RADIO =
    BLE_UUID128_INIT(0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8,
                     0xed, 0x11, 0x93, 0x49, 0x9e, 0xe6, 0x55, 0x2c);
static const ble_uuid128_t UUID_MT_LOG_RADIO =
    BLE_UUID128_INIT(0x47, 0x95, 0xdf, 0x8c, 0xde, 0xe9, 0x44, 0x99,
                     0x23, 0x44, 0xe6, 0x06, 0x49, 0x6e, 0x3d, 0x5a);
static const ble_uuid128_t UUID_MT_FROM_RADIO_SYNC =
    BLE_UUID128_INIT(0x5d, 0x16, 0x69, 0x37, 0x92, 0xc7, 0x63, 0x99,
                     0xdb, 0x45, 0x2d, 0x98, 0xc3, 0x50, 0x8a, 0x88);

static const ble_uuid128_t UUID_NUS_SERVICE =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_NUS_RX =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_NUS_TX =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static const ble_uuid128_t UUID_TM_SERVICE =
    BLE_UUID128_INIT(0x36, 0xc6, 0x54, 0x4d, 0x00, 0x00, 0x4c, 0x8f,
                     0x9f, 0x10, 0x54, 0x72, 0x61, 0x69, 0x6c, 0x4d);
static const ble_uuid128_t UUID_TM_RX =
    BLE_UUID128_INIT(0x36, 0xc6, 0x54, 0x4d, 0x01, 0x00, 0x4c, 0x8f,
                     0x9f, 0x10, 0x54, 0x72, 0x61, 0x69, 0x6c, 0x4d);
static const ble_uuid128_t UUID_TM_TX =
    BLE_UUID128_INIT(0x36, 0xc6, 0x54, 0x4d, 0x02, 0x00, 0x4c, 0x8f,
                     0x9f, 0x10, 0x54, 0x72, 0x61, 0x69, 0x6c, 0x4d);

static tm_c6_ble_config_t s_config;
static bool s_initialized;
static bool s_host_started;
static bool s_synced;
static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_connection_id;
static uint16_t s_meshtastic_from_radio_handle;
static uint16_t s_meshtastic_from_num_handle;
static uint16_t s_meshtastic_from_radio_sync_handle;
static uint16_t s_meshtastic_log_handle;
static uint16_t s_meshcore_tx_handle;
static uint16_t s_trailmate_tx_handle;
static uint32_t s_from_num_counter;

// Meshtastic FromRadio FIFO. The phone reads FromRadio (triggered by a FromNum
// notify) until it returns empty. A single slot dropped frames whenever the P4
// pushed the next frame before the phone read the previous one, so the phone got
// an incomplete config and restarted ("too many retries"). This queue holds each
// frame until the phone reads it, sized to absorb a full want_config burst
// (~34 frames) with margin.
// Meshtastic FromRadio ring. push() runs on the HostLink/SDIO task (P4 downlink);
// peek()/drop_head() run on the NimBLE host task (phone read); reset() runs on the
// NimBLE task at connect. Because reset() writes both indices while push() may be
// mid-flight on another task, and because the C memory model does not guarantee the
// slot-write/index-publish ordering by itself, all index+slot operations are done
// under a short portMUX critical section (single-core: brief interrupt-off around a
// bounded <=512B copy). Depth 64 holds up to 63 frames, far above a full
// want_config burst (~34), so a correctly paced phone never fills it.
#define TM_MESHTASTIC_QUEUE_DEPTH 64
#define TM_MESHTASTIC_FRAME_MAX 512
typedef struct
{
    uint16_t len;
    uint8_t data[TM_MESHTASTIC_FRAME_MAX];
} tm_meshtastic_frame_t;
static tm_meshtastic_frame_t s_mt_queue[TM_MESHTASTIC_QUEUE_DEPTH];
static uint16_t s_mt_head; // index of the oldest queued frame (next to read)
static uint16_t s_mt_tail; // index where the next pushed frame goes
static portMUX_TYPE s_mt_mux = portMUX_INITIALIZER_UNLOCKED;

static bool mt_queue_push(const uint8_t* data, size_t len)
{
    if (len > TM_MESHTASTIC_FRAME_MAX)
    {
        return false;
    }
    bool ok = false;
    portENTER_CRITICAL(&s_mt_mux);
    const uint16_t next = (uint16_t)((s_mt_tail + 1) % TM_MESHTASTIC_QUEUE_DEPTH);
    if (next != s_mt_head) // not full
    {
        tm_meshtastic_frame_t* slot = &s_mt_queue[s_mt_tail];
        if (len > 0 && data != NULL)
        {
            memcpy(slot->data, data, len);
        }
        slot->len = (uint16_t)len;
        s_mt_tail = next;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mt_mux);
    return ok;
}

// Copies the oldest frame WITHOUT removing it, so a failed GATT append can be
// retried on the next read. Returns its length (0 if empty). Commit with
// mt_queue_drop_head() only after the append succeeds.
static uint16_t mt_queue_peek(uint8_t* out, size_t out_max)
{
    uint16_t len = 0;
    portENTER_CRITICAL(&s_mt_mux);
    if (s_mt_head != s_mt_tail)
    {
        const tm_meshtastic_frame_t* slot = &s_mt_queue[s_mt_head];
        len = slot->len;
        if (len > out_max)
        {
            len = (uint16_t)out_max;
        }
        if (len > 0 && out != NULL)
        {
            memcpy(out, slot->data, len);
        }
    }
    portEXIT_CRITICAL(&s_mt_mux);
    return len;
}

static void mt_queue_drop_head(void)
{
    portENTER_CRITICAL(&s_mt_mux);
    if (s_mt_head != s_mt_tail)
    {
        s_mt_head = (uint16_t)((s_mt_head + 1) % TM_MESHTASTIC_QUEUE_DEPTH);
    }
    portEXIT_CRITICAL(&s_mt_mux);
}

static void mt_queue_reset(void)
{
    portENTER_CRITICAL(&s_mt_mux);
    s_mt_head = 0;
    s_mt_tail = 0;
    portEXIT_CRITICAL(&s_mt_mux);
}
static uint32_t s_active_passkey;
static uint8_t s_adv_profile_cursor = TM_C6_BLE_PROFILE_MESHTASTIC;

static int gatt_access_cb(uint16_t conn_handle,
                          uint16_t attr_handle,
                          struct ble_gatt_access_ctxt* ctxt,
                          void* arg);
static int gap_event_cb(struct ble_gap_event* event, void* arg);

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_MT_SERVICE.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {.uuid = &UUID_MT_TO_RADIO.u,
                 .access_cb = gatt_access_cb,
                 .arg = (void*)(uintptr_t)TM_C6_BLE_PROFILE_MESHTASTIC,
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
                {.uuid = &UUID_MT_FROM_NUM.u,
                 .access_cb = gatt_access_cb,
                 .val_handle = &s_meshtastic_from_num_handle,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
                {.uuid = &UUID_MT_FROM_RADIO.u,
                 .access_cb = gatt_access_cb,
                 .val_handle = &s_meshtastic_from_radio_handle,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
                {.uuid = &UUID_MT_FROM_RADIO_SYNC.u,
                 .access_cb = gatt_access_cb,
                 .val_handle = &s_meshtastic_from_radio_sync_handle,
                 .flags = BLE_GATT_CHR_F_NOTIFY},
                {.uuid = &UUID_MT_LOG_RADIO.u,
                 .access_cb = gatt_access_cb,
                 .val_handle = &s_meshtastic_log_handle,
                 .flags = BLE_GATT_CHR_F_NOTIFY},
                {0},
            },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_NUS_SERVICE.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {.uuid = &UUID_NUS_RX.u,
                 .access_cb = gatt_access_cb,
                 .arg = (void*)(uintptr_t)TM_C6_BLE_PROFILE_MESHCORE,
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
                {.uuid = &UUID_NUS_TX.u,
                 .access_cb = gatt_access_cb,
                 .val_handle = &s_meshcore_tx_handle,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
                {0},
            },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_TM_SERVICE.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {.uuid = &UUID_TM_RX.u,
                 .access_cb = gatt_access_cb,
                 .arg = (void*)(uintptr_t)TM_C6_BLE_PROFILE_TRAILMATE,
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
                {.uuid = &UUID_TM_TX.u,
                 .access_cb = gatt_access_cb,
                 .val_handle = &s_trailmate_tx_handle,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
                {0},
            },
    },
    {0},
};

static bool profile_enabled(uint8_t profile)
{
    if (!s_config.ble_enabled)
    {
        return false;
    }
    if (profile == TM_C6_BLE_PROFILE_MESHTASTIC)
    {
        return s_config.meshtastic_enabled != 0;
    }
    if (profile == TM_C6_BLE_PROFILE_MESHCORE)
    {
        return s_config.meshcore_enabled != 0;
    }
    if (profile == TM_C6_BLE_PROFILE_TRAILMATE)
    {
        return s_config.trailmate_enabled != 0;
    }
    return false;
}

static uint8_t next_advertised_profile(void)
{
    for (uint8_t attempt = 0; attempt < 3; ++attempt)
    {
        uint8_t profile = s_adv_profile_cursor;
        ++s_adv_profile_cursor;
        if (s_adv_profile_cursor > TM_C6_BLE_PROFILE_TRAILMATE)
        {
            s_adv_profile_cursor = TM_C6_BLE_PROFILE_MESHTASTIC;
        }
        if (profile_enabled(profile))
        {
            return profile;
        }
    }
    return TM_C6_BLE_PROFILE_NONE;
}

static const ble_uuid128_t* service_uuid_for_profile(uint8_t profile)
{
    switch ((tm_c6_ble_profile_t)profile)
    {
    case TM_C6_BLE_PROFILE_MESHTASTIC:
        return &UUID_MT_SERVICE;
    case TM_C6_BLE_PROFILE_MESHCORE:
        return &UUID_NUS_SERVICE;
    case TM_C6_BLE_PROFILE_TRAILMATE:
        return &UUID_TM_SERVICE;
    default:
        return NULL;
    }
}

static void emit_event(uint8_t kind, uint8_t profile, uint16_t mtu, uint16_t error_code)
{
    tm_c6_ble_event_t event = {
        .event_kind = kind,
        .profile = profile,
        .connection_id = s_connection_id,
        .state = s_conn_handle == BLE_HS_CONN_HANDLE_NONE ? 0 : 1,
        .mtu = mtu,
        .error_code = error_code,
    };
    (void)tm_services_send_ble_event(&event);
}

static void copy_device_name(char* out, size_t out_len)
{
    if (out == NULL || out_len == 0)
    {
        return;
    }
    if (s_config.device_name[0] != '\0')
    {
        snprintf(out, out_len, "%s", s_config.device_name);
    }
    else
    {
        snprintf(out, out_len, "%s", "TrailMate-C6");
    }
}

static int append_mbuf(struct os_mbuf* om, const uint8_t* data, size_t len)
{
    if (len == 0)
    {
        return 0;
    }
    return os_mbuf_append(om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static uint32_t parse_fixed_pin(void)
{
    if (!s_config.fixed_pin_enabled)
    {
        return 0;
    }

    uint32_t value = 0;
    size_t digits = 0;
    for (size_t i = 0; i < sizeof(s_config.fixed_pin) && s_config.fixed_pin[i] != '\0'; ++i)
    {
        const char ch = s_config.fixed_pin[i];
        if (ch < '0' || ch > '9')
        {
            return 0;
        }
        value = value * 10u + (uint32_t)(ch - '0');
        ++digits;
    }

    return digits == 6 && value >= 100000u && value <= 999999u ? value : 0;
}

static bool pairing_requires_pin(void)
{
    return s_config.pairing_mode == TM_C6_PAIRING_RANDOM_PIN ||
           s_config.pairing_mode == TM_C6_PAIRING_FIXED_PIN;
}

static uint32_t active_passkey(void)
{
    if (s_config.pairing_mode == TM_C6_PAIRING_FIXED_PIN)
    {
        const uint32_t fixed = parse_fixed_pin();
        if (fixed != 0)
        {
            return fixed;
        }
    }

    if (s_active_passkey < 100000u || s_active_passkey > 999999u)
    {
        s_active_passkey = 100000u + (uint32_t)(esp_random() % 900000u);
    }
    return s_active_passkey;
}

static void apply_security_config(void)
{
    if (!pairing_requires_pin())
    {
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
        ble_hs_cfg.sm_bonding = 0;
        ble_hs_cfg.sm_mitm = 0;
        ble_hs_cfg.sm_sc = 0;
        ble_hs_cfg.sm_our_key_dist = 0;
        ble_hs_cfg.sm_their_key_dist = 0;
        s_active_passkey = 0;
#if MYNEWT_VAL(STATIC_PASSKEY)
        (void)ble_sm_configure_static_passkey(0, false);
#endif
        return;
    }

    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

#if MYNEWT_VAL(STATIC_PASSKEY)
    if (s_config.pairing_mode == TM_C6_PAIRING_FIXED_PIN)
    {
        const uint32_t passkey = active_passkey();
        const int rc = ble_sm_configure_static_passkey(passkey, true);
        if (rc != 0)
        {
            ESP_LOGW(TAG, "static passkey config failed rc=%d", rc);
            tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_static_passkey_failed");
        }
    }
    else
    {
        s_active_passkey = 0;
        (void)ble_sm_configure_static_passkey(0, false);
    }
#endif
}

static bool connection_is_authenticated(uint16_t conn_handle)
{
    if (!pairing_requires_pin())
    {
        return true;
    }

    struct ble_gap_conn_desc desc;
    memset(&desc, 0, sizeof(desc));
    const int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0)
    {
        return false;
    }
    return desc.sec_state.encrypted && desc.sec_state.authenticated;
}

static int gatt_access_cb(uint16_t conn_handle,
                          uint16_t attr_handle,
                          struct ble_gatt_access_ctxt* ctxt,
                          void* arg)
{
    const uint8_t profile = (uint8_t)(uintptr_t)arg;

    if (ctxt == NULL)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        if (!profile_enabled(profile))
        {
            tm_services_record_error(TM_C6_ERROR_PROFILE_NOT_CONFIGURED, "ble_profile_disabled");
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (!connection_is_authenticated(conn_handle))
        {
            (void)ble_gap_security_initiate(conn_handle);
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
        const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > TM_C6_MAX_PAYLOAD - TM_C6_BLE_PACKET_HEADER_LEN)
        {
            tm_services_record_error(TM_C6_ERROR_PAYLOAD_TOO_LARGE, "ble_write_too_large");
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t data[TM_C6_MAX_PAYLOAD] = {};
        if (ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL) != 0)
        {
            tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_mbuf_flat_failed");
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (!tm_services_send_ble_uplink(profile, s_connection_id, data, len))
        {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        if (!connection_is_authenticated(conn_handle))
        {
            (void)ble_gap_security_initiate(conn_handle);
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
        if (attr_handle == s_meshtastic_from_radio_handle)
        {
            uint8_t buf[TM_MESHTASTIC_FRAME_MAX];
            const uint16_t len = mt_queue_peek(buf, sizeof(buf));
            // len == 0 -> empty read, which tells the phone the queue is drained.
            const int rc = append_mbuf(ctxt->om, buf, len);
            if (rc == 0 && len > 0)
            {
                // Remove the frame only once it is safely in the response mbuf; on
                // mbuf exhaustion (rc != 0) it stays queued and is retried next read.
                mt_queue_drop_head();
            }
            return rc;
        }
        if (attr_handle == s_meshtastic_from_num_handle)
        {
            uint8_t counter[4] = {
                (uint8_t)(s_from_num_counter & 0xffu),
                (uint8_t)((s_from_num_counter >> 8) & 0xffu),
                (uint8_t)((s_from_num_counter >> 16) & 0xffu),
                (uint8_t)((s_from_num_counter >> 24) & 0xffu),
            };
            return append_mbuf(ctxt->om, counter, sizeof(counter));
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void advertise(void)
{
    if (!s_synced || !s_config.ble_enabled)
    {
        return;
    }
    if (ble_gap_adv_active())
    {
        return;
    }

    char name[TM_C6_BLE_DEVICE_NAME_LEN] = {};
    copy_device_name(name, sizeof(name));
    ble_svc_gap_device_name_set(name);

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const uint8_t advertised_profile = next_advertised_profile();
    const ble_uuid128_t* advertised_uuid = service_uuid_for_profile(advertised_profile);
    if (advertised_uuid == NULL)
    {
        return;
    }
    fields.uuids128 = advertised_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "adv_set_fields failed rc=%d", rc);
        tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_adv_fields_failed");
        return;
    }

    struct ble_hs_adv_fields response_fields;
    memset(&response_fields, 0, sizeof(response_fields));
    response_fields.name = (uint8_t*)name;
    response_fields.name_len = strlen(name);
    response_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response_fields);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "adv_rsp_set_fields failed rc=%d", rc);
        tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_adv_rsp_fields_failed");
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, 2000, &params, gap_event_cb, NULL);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "adv_start failed rc=%d", rc);
        tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_adv_start_failed");
    }
}

static int gap_event_cb(struct ble_gap_event* event, void* arg)
{
    (void)arg;
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            s_conn_handle = event->connect.conn_handle;
            ++s_connection_id;
            mt_queue_reset(); // fresh FromRadio queue for the new session
            emit_event(TM_C6_BLE_EVENT_CONNECTED, TM_C6_BLE_PROFILE_NONE, ble_att_mtu(s_conn_handle), TM_C6_OK);
        }
        else
        {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s_config.pairing_mode == TM_C6_PAIRING_RANDOM_PIN)
        {
            s_active_passkey = 0;
        }
        emit_event(TM_C6_BLE_EVENT_DISCONNECTED, TM_C6_BLE_PROFILE_NONE, 0, TM_C6_OK);
        advertise();
        return 0;
    case BLE_GAP_EVENT_MTU:
        emit_event(TM_C6_BLE_EVENT_MTU_CHANGED, TM_C6_BLE_PROFILE_NONE, event->mtu.value, TM_C6_OK);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (!connection_is_authenticated(event->subscribe.conn_handle))
        {
            return ble_gap_security_initiate(event->subscribe.conn_handle);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0)
        {
            emit_event(TM_C6_BLE_EVENT_NOTIFY_DROPPED,
                       TM_C6_BLE_PROFILE_NONE,
                       0,
                       (uint16_t)event->enc_change.status);
        }
        else if (s_config.pairing_mode == TM_C6_PAIRING_RANDOM_PIN)
        {
            s_active_passkey = 0;
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
    {
        struct ble_gap_conn_desc desc;
        memset(&desc, 0, sizeof(desc));
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
        {
            (void)ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP ||
            event->passkey.params.action == BLE_SM_IOACT_STATIC)
        {
            struct ble_sm_io pkey;
            memset(&pkey, 0, sizeof(pkey));
            pkey.action = event->passkey.params.action;
            pkey.passkey = active_passkey();
            const int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            if (rc != 0)
            {
                ESP_LOGW(TAG, "passkey inject failed rc=%d", rc);
                tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_passkey_failed");
                return rc;
            }
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "ensure_addr failed rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "id_infer failed rc=%d", rc);
        return;
    }
    s_synced = true;
    advertise();
}

static void host_task(void* param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t tm_ble_init(void)
{
    memset(&s_config, 0, sizeof(s_config));
    return ESP_OK;
}

esp_err_t tm_ble_apply_config(const tm_c6_ble_config_t* config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    if (!s_config.ble_enabled)
    {
        if (s_synced)
        {
            ble_gap_adv_stop();
        }
        return ESP_OK;
    }
    if (s_config.pairing_mode == TM_C6_PAIRING_FIXED_PIN && parse_fixed_pin() == 0)
    {
        tm_services_record_error(TM_C6_ERROR_INTERNAL, "ble_fixed_pin_invalid");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized)
    {
        esp_err_t err = nimble_port_init();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
            return err;
        }

        ble_svc_gap_init();
        ble_svc_gatt_init();
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_store_config_init();
        int rc = ble_gatts_count_cfg(s_gatt_svcs);
        if (rc != 0)
        {
            return ESP_FAIL;
        }
        rc = ble_gatts_add_svcs(s_gatt_svcs);
        if (rc != 0)
        {
            return ESP_FAIL;
        }
        ble_hs_cfg.sync_cb = on_sync;
        s_initialized = true;
    }

    apply_security_config();

    if (!s_host_started)
    {
        nimble_port_freertos_init(host_task);
        s_host_started = true;
    }

    advertise();
    emit_event(TM_C6_BLE_EVENT_STARTED, TM_C6_BLE_PROFILE_NONE, tm_services_selected_mtu(), TM_C6_OK);
    return ESP_OK;
}

static esp_err_t notify_handle(uint16_t value_handle, const uint8_t* payload, size_t payload_len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!connection_is_authenticated(s_conn_handle))
    {
        (void)ble_gap_security_initiate(s_conn_handle);
        return ESP_ERR_INVALID_STATE;
    }
    if (payload_len > TM_C6_MAX_PAYLOAD)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    struct os_mbuf* om = ble_hs_mbuf_from_flat(payload, payload_len);
    if (om == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    const int rc = ble_gatts_notify_custom(s_conn_handle, value_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t tm_ble_send_downlink(uint8_t profile, const uint8_t* payload, size_t payload_len)
{
    if (!profile_enabled(profile))
    {
        tm_services_record_error(TM_C6_ERROR_PROFILE_NOT_CONFIGURED, "ble_downlink_profile_disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (payload == NULL && payload_len != 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len > TM_C6_MAX_PAYLOAD)
    {
        tm_services_record_error(TM_C6_ERROR_PAYLOAD_TOO_LARGE, "ble_downlink_too_large");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    switch ((tm_c6_ble_profile_t)profile)
    {
    case TM_C6_BLE_PROFILE_MESHTASTIC:
        // Queue the frame so it survives until the phone reads FromRadio. Only bump
        // FromNum + notify AFTER a successful enqueue, so the counter the phone sees
        // always matches what is actually readable. A push only fails if the frame
        // is oversized (never happens for Meshtastic) or the 63-slot queue is full
        // (a correctly paced phone never fills it); drop rather than wedge -- the
        // phone re-requests config if it ends up short.
        if (!mt_queue_push(payload, payload_len))
        {
            tm_services_record_error(TM_C6_ERROR_PAYLOAD_TOO_LARGE, "mt_queue_full_or_oversized");
            err = ESP_OK;
            break;
        }
        ++s_from_num_counter;
        {
            const uint8_t counter[4] = {
                (uint8_t)(s_from_num_counter & 0xffu),
                (uint8_t)((s_from_num_counter >> 8) & 0xffu),
                (uint8_t)((s_from_num_counter >> 16) & 0xffu),
                (uint8_t)((s_from_num_counter >> 24) & 0xffu),
            };
            // Best-effort mirror to the custom sync characteristic, then always
            // raise FromNum -- that is what the standard Meshtastic phone waits on
            // before it reads FromRadio.
            (void)notify_handle(s_meshtastic_from_radio_sync_handle, payload, payload_len);
            (void)notify_handle(s_meshtastic_from_num_handle, counter, sizeof(counter));
        }
        err = ESP_OK;
        break;
    case TM_C6_BLE_PROFILE_MESHCORE:
        err = notify_handle(s_meshcore_tx_handle, payload, payload_len);
        break;
    case TM_C6_BLE_PROFILE_TRAILMATE:
        err = notify_handle(s_trailmate_tx_handle, payload, payload_len);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    if (err == ESP_OK)
    {
        tm_services_note_ble_downlink();
    }
    return err;
}
