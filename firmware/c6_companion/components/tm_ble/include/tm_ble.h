#pragma once

#include "hostlink/c6/c6_protocol.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t tm_ble_init(void);
    esp_err_t tm_ble_apply_config(const tm_c6_ble_config_t* config);
    esp_err_t tm_ble_handle_control(const tm_c6_ble_control_t* control);
    esp_err_t tm_ble_send_downlink(uint8_t profile, const uint8_t* payload, size_t payload_len);

#ifdef __cplusplus
}
#endif
