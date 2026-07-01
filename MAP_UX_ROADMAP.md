# Trail Mate - Map UX Roadmap & Pinch-to-Zoom Plan

Research deliverable, 2026-06-26. Execute next session when device flashing is available
(devices are out for travel; this session was research-only). Branch: `channel-key-passphrase`.

## Context (what shipped this session)
- Offline maps now RENDER on the P4 (3 stacked fixes: LVGL FS driver `A:`->/sdcard, lodepng
  PNG decoder, CLIB malloc routing 256KB tiles to PSRAM). Commits 05fc158 / 8677eee / 675d464.
- Layer & Zoom availability checkmarks (dynamic, SD-probed) + Settings "Offline Coverage" row. 312e5a2.
- Last-known-position marker (hollow grey ring, SD-persisted, survives reboot). c248a24.
- Diagnosed: Unit A GPS module is hardware-dead (electrically silent); Unit B works. Not firmware.

---

## 1. PINCH-TO-ZOOM - VERDICT: FEASIBLE

No hardware or driver-ownership obstacle. The touch ICs are multi-touch, and the firmware
already owns the touch read path via a hand-written LVGL `read_cb` over raw I2C (NOT
esp_lvgl_port / esp_lcd_touch), so reading a 2nd finger is a small, contention-free, localized change.

### The two touch ICs (auto-selected by panel at boot)
| Panel | Touch IC | I2C addr | Multi-touch today |
|---|---|---|---|
| `hi8561` (TFT - the shipping `tdisplayp4_tft` build) | Hi8561 integrated touch | 0x68 | reference driver reads up to 10 fingers; **IDF runtime currently reads only 1 pt (5 bytes)** -> widen read to 13 bytes for 2 pts |
| `rm69a10` (AMOLED) | Goodix GT9895 | 0x5D | **IDF runtime ALREADY reads the full 10-finger buffer + knows finger_count** -> decode finger 1 = zero extra I2C |

### Recommended approach: (a) firmware pinch detection in the existing polled timer
REJECT LVGL's built-in pinch recognizer: LVGL 9.5.0 has it (`lv_indev_gesture.c`,
`lv_indev_gesture_detect_pinch`, `lv_event_get_pinch_scale`) but it is COMPILED OUT - it
depends on `LV_USE_FLOAT` + `LV_USE_GESTURE_RECOGNITION`, both off in this Kconfig build
(`# CONFIG_LV_USE_FLOAT is not set`). Enabling them pulls floating-point into LVGL's hot
paths on the P4 (flash/RAM/CPU) AND still requires rewriting the read_cb into a type-B
multi-contact feeder. Approach (a) is ~50 lines of integer math reusing primitives we already have.

### Mechanism
1. **Read 2 points** - extend the firmware's own touch read in
   `platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp`:
   - GT9895 (`read_gt9895_touch`, ~359-414): full buffer already read; decode finger 1 at
     `response[offset + 1*8 + 2..5]`, guarded by `finger_count (response[2]) >= 2`.
   - Hi8561 (`read_hi8561_touch`, ~306-357): widen the I2C read from 5 to
     `3 + 2*5 = 13` bytes, decode 2 points per the reference `Hi8561Touch::GetMultipleTouchPoint`.
   - Expose via a new `extern "C"` accessor (next to the runtime exports ~717-779), e.g.
     `int trail_mate_t_display_p4_read_touch_points(TouchPt out[2])` returning finger count
     (0/1/2) + coords, inside the SAME `lock_system_i2c` path (no contention, no 2nd IC owner).
2. **Pinch state machine** in `gps_page_input.cpp` `map_touch_poll_timer_cb` (~346, the 16ms
   poll): branch on finger count - 2 -> pinch handler, 1 -> existing pan, 0 -> release.
   - Enter pinch on 2 fingers: record `start_dist` (integer `dx*dx+dy*dy` is enough),
     `start_zoom`, `start_mid`. Suppress single-finger pan while pinching (early-return in
     `handle_map_touch_press/move` when count != 1).
   - While 2 down: distance ratio with HYSTERESIS - e.g. +1 zoom when `cur/start > 1.6`,
     -1 when `< 0.625`; re-baseline `start_dist = cur_dist` after each step; debounce ~120ms.
   - Clamp zoom to 0..18. Re-center on the pinch MIDPOINT (or screen-center for a simpler v1).
   - End pinch when count < 2: clear flag, `follow_position=false`, `remember_gps_view_state()`,
     final `update_map_tiles(false)`; debounce one poll so the lingering finger doesn't yank a pan.
3. **Reuse the zoom-apply tail** (`zoom_popup_apply_selection` ~1315-1371: set `zoom_level`,
   reset `pan_x/pan_y`, `update_map_anchor()` + `update_map_tiles(false)`). Factor a shared
   `apply_zoom_level(int new_level, recenter_point)` helper so the popup and pinch share one path.

### Files to change
1. `platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp` - 2-point read + accessor.
2. `platform/esp/arduino_common/src/ui/screens/gps/gps_page_input.cpp` - pinch state machine in the poll timer.
3. `modules/ui_shared/include/ui/screens/gps/gps_state.h` - `PinchState` fields (under the `USING_INPUT_DEV_TOUCHPAD` block ~172-183).
4. No sdkconfig/Kconfig/LVGL-config change required for approach (a).

### Risks / unknowns
- **Hi8561 (TFT) 2-finger read is unverified on hardware** - the reference `GetMultipleTouchPoint`
  exists, but confirm the buffer layout + a clean 2nd-contact report on a device. GT9895 (AMOLED)
  is lower-risk (runtime already reads the full buffer).
- **I2C lock budget** - keep the 16ms poll; do NOT add a 2nd independent poll (route pinch through
  the existing timer to avoid doubling bus traffic; touch lock timeout 50ms / txn 30ms, bus shared
  with the GPIO expander/RTC/GPS-LoRa prep).
- **Pan vs pinch arbitration** - hard-gate pan while 2 fingers down; debounce the 2->1 transition
  (the existing 6px drag threshold helps but isn't enough alone).
- **No hardware pinch-gesture byte** - do the geometry in firmware; don't depend on a controller register.
- **Midpoint re-center** is the only genuinely new math (screen->world->screen via the same
  `pan_x/pan_y` + anchor model the pan uses). A screen-center v1 (today's zoom-apply behavior) de-risks iteration 1.

---

## 2. OTHER RECOMMENDATIONS (prioritized)

### Touch / map UX
- **Double-tap to zoom in / two-finger-tap to zoom out** - single-touch-friendly; do alongside
  pinch, and a guaranteed fallback if the Hi8561 2-finger read proves flaky.
- **Momentum / inertia panning** - flick-to-glide with deceleration (vs the current step-pan).
- **"Outside coverage" hint** - subtle label when panning beyond loaded tiles instead of blank
  white, so blank always reads as "no tiles here," never "is it broken?" (the exact confusion hit 06-26).
- **Scale bar + zoom-level indicator** on the map.
- **Long-press to drop a waypoint** - reuse the SD-persistence built for the last-known marker.
- **Last-known marker polish** - show "last fix: Xh ago" (the epoch is already stored) + an optional
  breadcrumb trail of recent positions.
- **Snap initial zoom** to a level that has tiles for the current area on map entry.

### Device / system (post-trip)
- **Unit A GPS** - hardware-dead module: multimeter the L76K VCC, reflow/replace the module
  (see memory: tdisplay-p4-gps-unit-a-hardware).
- **Team app** - NVS persistence so it stops re-pairing every reboot; secure passphrase pairing
  (the PSK is currently in the clear).
- **Map tiles** - render z13-14 for the destination/vacation area for future trips
  (finish-conus-render.ps1 incremental top-up; the PostGIS DB + tilecache are preserved).

---

## 3. CODE-REVIEW FOLLOW-UPS (2026-07-01 max-effort review of the map-tiles session)

15 findings fixed on-branch (persistence rework, Inf-guard, i18n key, engine-level layer
gate, title-bar overlap, throttle rework, probe caching, sdkconfig pins, pack-scan
decouple, and cleanups). Items surfaced but deliberately DEFERRED or ACCEPTED:

- Zoom popup does 19 one-shot SD probes at open (~40-150ms behind the modal). Accepted:
  user-initiated, once per open. Revisit only if the open visibly hitches.
- update_last_known_marker_position runs twice in a tick when a follow-refresh fires
  (update_map_tiles + the gps_state_changed tail). Micro cost, accepted.
- Settings "Offline Coverage" row is a static string by design (Z asked for a written
  note; the Layer/Zoom checkmarks are the live truth). UPDATE THE STRING when re-rendering
  tiles for a new region (finish-conus-render.ps1).
- last_known_epoch is stored but unread -- reserved for the "last fix: Xh ago" label
  polish (section 2 above).
- build_zoom_popup_ui carries a pre-existing forked copy of the modal title/content
  geometry (fixed separately in 866ecd3); consolidate onto modal_create_touch_title_bar/
  content_area when next touching that popup.
- Last-fix capture only runs while the GPS page is open. The deeper design is a
  domain-level last-fix service in the GPS runtime (captures regardless of page, serves
  future consumers: dashboard widget, compass, waypoints). The bespoke lv_fs file could
  then move to the chat::infra NVS blob store for SD-independence.
- Settings -> Map Source enum writes config directly, bypassing the engine availability
  gate. Accepted as a deliberate escape hatch (force a source despite marks).
- node_info's layer popup is protected by the engine gate but has no availability
  checkmarks (optional polish).
- Layer/zoom availability probes run unguarded by SharedSpiLockGuard (matches the
  pre-existing missing-tile probe call style; lastfix save/load DO take the guard).
  On the P4 the guard is a no-op either way.

---

## Key source references (for execution)
- Board touch driver: `T-Display-P4/libraries/lilygo_device_driver/src/device/t_display_p4/t_display_p4_driver.cpp` (163-174, 290-299); config `t_display_p4_config.h` (289 Hi8561 0x68, 307 GT9895 0x5D).
- Touch chip multi-point reference: `T-Display-P4/libraries/cpp_bus_driver/src/chip/i2c/gt9895.cpp` (144-204), `hi8561_touch.cpp` (164+).
- Live touch path (IDF): `trail-mate/platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp` (read_gt9895_touch 359-414, read_hi8561_touch 306-357, touch_read_cb 434-459, create_touch_indev 461-480).
- Touch-pan + zoom UI: `trail-mate/platform/esp/arduino_common/src/ui/screens/gps/gps_page_input.cpp` (poll 346-396, zoom apply 1315-1371, adjust_popup_zoom 76).
- LVGL gesture (compiled out): `trail-mate/managed_components/lvgl__lvgl/src/indev/lv_indev_gesture.{c,h}`; gate `Kconfig:878-881` + `sdkconfig.tdisplayp4_tft:2716`.
- State + bounds: `gps_state.h:172-183`, `gps_constants.h:8-14`, `map_viewport.h:17-19` (kMinZoom 0, kMaxZoom 18, kDefaultZoom 12).
