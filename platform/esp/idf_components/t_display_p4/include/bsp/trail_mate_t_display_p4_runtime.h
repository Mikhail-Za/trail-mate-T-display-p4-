#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool trail_mate_t_display_p4_display_runtime_init(void);
    bool trail_mate_t_display_p4_display_runtime_is_ready(void);
    bool trail_mate_t_display_p4_display_lock(uint32_t timeout_ms);
    void trail_mate_t_display_p4_display_unlock(void);
    esp_err_t trail_mate_t_display_p4_display_set_brightness_percent(int brightness_percent);
    /* Multi-touch snapshot captured by the most recent touch-controller read (the same
     * I2C transactions that feed the LVGL pointer -- calling this adds no bus traffic).
     * Fills up to two points and returns the finger count (0..2). LVGL-task use only. */
    uint8_t trail_mate_t_display_p4_touch_points(int32_t out_x[2], int32_t out_y[2]);

#ifdef __cplusplus
}
#endif
