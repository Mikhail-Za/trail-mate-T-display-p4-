Now I have all evidence needed. Compiling the complete addendum.

---

# Trail-Mate T-Display P4 — Audit Addendum
**Worker:** bounded read-only Sonnet (claude-sonnet-4-6) · Ponytail SKILL active
**Baseline:** `e3c0fbd68663bbeef09056a22cbce950065304bc` (main, descends from `98609d5`)
**Session:** continuation of first-pass report; all Astra corrections applied herein
**Constraint reminder:** read-only; no shell/edit/delegate; no invented CVEs/perf numbers; mesh crypto is security-worker scope; safety-critical photo captions preserved, not rewritten.

---

## Part I — Corrections to Original Report

### C1 — F7 Retracted: `uint32_t` unsigned elapsed subtraction is correct
**Original claim:** `(now_ms - blank_since_ms) >= kBlankHoldMs` unsafe across uint32 wrap.
**Correction (Astra, confirmed):** Unsigned subtraction in C++ wraps modulo 2³² by the standard. For any elapsed interval bounded well below UINT32_MAX (kBlankHoldMs is milliseconds; overflow of a 32-bit ms counter takes ~49 days), `now_ms - blank_since_ms` always yields the correct positive elapsed time regardless of individual wrap. This is the standard FreeRTOS embedded idiom. **F7 is false; retracted.**
**Evidence:** `gps_jitter_filter.cpp:43` uses the same idiom (`uint32_t dt_ms = now_ms - last_ms_`); `gps_page_map.cpp` and `walkie_service.cpp` use it uniformly. No counter-evidence found.

---

### C2 — F1 Test Description Corrected: Title Shows View-Center, Not GPS Fix
**Original error:** The test description stated "[P] re-center → title shows Pakistani coordinates." This is backwards.
**Corrected description (evidence `gps_page_map.cpp:618–702`, `gps_page_input.cpp:1557–1558`):**
- When the user pans the map, `apply_zoom_level_centered` writes `g_gps_state.lat = center_lat; g_gps_state.lng = center_lng` (the panned view center). `update_title_and_status` reads `g_gps_state.lat/lng` at line 660. So while `follow_position == false`, the title bar shows the panned view-center coordinate (Pakistan in the test scenario).
- `[P]` calls `action_position_center`, which sets `follow_position = true` and then on the next `tick_gps_update` the snap-back fix writes `g_gps_state.lat/lng` from the real `gps_data.lat/lng` (Wisconsin fix). After `[P]`, the title correctly reverts to Wisconsin.
- **The F1 finding (title shows wrong coordinate while panned) is real. The test description was inverted. Corrected failable check:** Pan map to location far from actual GPS → title bar shows panned coordinates, not GPS fix coordinates. Press [P] → title immediately returns to GPS fix coordinates.

---

### C3 — Unit A GPS Hardware Status Retracted
**Original report:** Listed Unit A GPS as an open hardware marginal item.
**Correction:** `MAINTAINERS.md §12.1` (2026-07-11, authoritative): *"Unit A's GPS started working — it was hardware-marginal, not firmware."* Do not repeat obsolete status. **Retracted.**

---

### C4 — Team NVS Persistence Open Item Retracted
**Original report:** Listed "Team app NVS key persistence so pairing survives reboot" as open.
**Correction:** `platform/esp/idf_common/src/team/idf_nvs_team_ui_snapshot_store.cpp` confirmed present and reads as a complete implementation. NVS namespace `"team_keys"`, key `"v1"`, blob layout with version byte + team ID (8 bytes) + key ID (4 bytes) + PSK (16 bytes) + flags byte = 30 bytes total. `nvs_set_blob` + `nvs_commit` on write; two-call size-probe + read on load; `ESP_ERR_NVS_NOT_FOUND` treated as absent. **Team persistence is implemented; open item retracted.**
**Evidence:** `idf_nvs_team_ui_snapshot_store.cpp:34–43` (offset layout), lines 65–75 (erase path), lines 79–end (load path — `nvs_open` READONLY, size-probe, then blob read).

---

### C5 — Binfont Crash Reclassified as Unverified Robustness Gap
**Original claim:** `lv_binfont_create` crashes on corrupt binfont file.
**Reclassification:** `lv_binfont_loader.h/c` is a managed component not present in the repo source tree (no files matched `**/lv_binfont*`). The decoder source cannot be read in this audit.

**What the source DOES show (positive controls):**
- `modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp:1104–1109`: Calls `lv_binfont_create(pack.source_path.c_str())` and immediately checks `if (pack.owned_font == nullptr)` → logs failure and returns false without crashing.
- `apps/esp32_lvgl/src/esp32_lvgl_translate_app.cpp:208–212`: Assigns `st->font = lv_binfont_create(font_path.c_str())` and checks `if (st->font == nullptr)` → sets `font_missing` flag, falls back to `lv_font_montserrat_24`.

Both callers handle a null return correctly. **Whether `lv_binfont_create` can crash internally on corrupt file data (rather than returning null) is unknown.** This is a documentation-level gap, not a confirmed crash. MAP_UX_ROADMAP §4 lists "binfont corrupt crash" as a deferred follow-up item.
**Severity:** Unverified robustness gap (medium priority); cannot claim crash without decoder source.

---

## Part II — New Findings (Astra-Mandated Inspection)

---

### **NF-1 [CRITICAL SAFETY] — Photo Species Warning Silently Omitted When CREDITS.tsv Absent, Malformed, or Missing Row**

**Severity:** Critical (safety, not security)
**Confidence:** Confirmed by source
**Category:** Safety / data-loss risk

**Evidence:**

`apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp:846`:
```cpp
append_photo_caption(st, st->photo_stem, idx + 1);  // ← called BEFORE image creation
```

`apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp:857`:
```cpp
lv_obj_t* img = lv_image_create(st->body);  // ← unconditional; no dependency on caption result
```

Inside `append_photo_caption` (lines 628–692):

```cpp
// Lines 628–632: silent return if CREDITS.tsv is missing or read fails
std::string text;
if (!read_text_file("A:/guides/photos/CREDITS.tsv", text))
{
    return;   // ← caption never rendered; photo shown by caller regardless
}
```

```cpp
// Lines 664–667: silent return if no matching row for this stem+photo-number
if (species.empty())
{
    return;   // ← caption never rendered; photo shown by caller regardless
}
```

```cpp
// Lines 668–689: the red-tint "lookalike" / "poison" / "deadly" warning label
// is only rendered when species is non-empty AND present in CREDITS.tsv
```

**Trigger paths:**

| Scenario | Effect |
|---|---|
| CREDITS.tsv absent from SD card | Every photo in every article is displayed with zero species identification |
| CREDITS.tsv on SD but malformed (header-only, binary corruption, wrong delimiters) | `read_text_file` succeeds but `for_each_tsv_row` finds no matching row → `species.empty()` → silent return |
| CREDITS.tsv correct but stem/number not yet added (newly photographed species) | Same: silent return, no warning rendered |
| SD card hot-swapped mid-session and SD remounted before photo is opened | CREDITS.tsv read succeeds, but new card's CREDITS.tsv may lack a row that old card had |

**Impact:** `append_photo_caption` applies a red-tinted species label that says `{deadly, poison, venom, lookalike}` when the matched species field contains those terms. A lookalike photo (e.g., poison hemlock photographed alongside yarrow) that appears under the article title "Yarrow" will display with no species name at all — a user may believe they are looking at a safe plant. The photo IS created and visible regardless (`lv_image_create` at line 857 is unconditional).

**Existing mitigations:** `show_photo_viewer_screen` calls `append_photo_caption` before `lv_image_create` (line 846 vs 857), so ordering is correct for the happy path. Red-tint styling and species-level danger keywords are implemented (lines 668–689). No mitigation for the file-missing / no-matching-row case.

**Minimal fix direction (Astra to validate):** `append_photo_caption` should return a `bool` indicating whether a species caption was successfully rendered. `show_photo_viewer_screen` should, on a false return, render a fallback label (e.g., grey-tinted "Species unknown") rather than leaving the photo header blank. This ensures the user sees a visual indicator that the biological identification metadata is unavailable rather than no warning at all.

**Concrete failable check:** Remove `CREDITS.tsv` from the SD card (or rename it). Open the field guide, navigate to any article with a photo marked as a lookalike species. Verify that the photo viewer shows the species warning. (Expected result before fix: no warning; photo shown without identification.)

---

### NF-2 — `read_text_file` Has No Size Cap: Any SD File Fully Slurped Into PSRAM
**Severity:** Low (controlled SD, not externally writable)
**Confidence:** Confirmed
**Category:** Robustness / defensive programming gap

**Evidence:** `modules/ui_shared/include/ui/support/lvgl_fs_utils.h:143–178`:

```cpp
inline bool read_text_file(const char* path, std::string& out)
{
    // ...
    char buffer[256];
    uint32_t bytes_read = 0;
    while (true)
    {
        if (lv_fs_read(&file, buffer, sizeof(buffer), &bytes_read) != LV_FS_RES_OK)
        {
            lv_fs_close(&file); out.clear(); return false;
        }
        if (bytes_read == 0) { break; }
        out.append(buffer, bytes_read);  // ← no size limit checked here
    }
    lv_fs_close(&file);
    return !out.empty();
}
```

No caller passes a size cap; no cap is possible through this API since there is no `max_bytes` parameter.

**Call sites reading unbounded files:**
- `append_photo_caption` and `append_photo_credits` → `"A:/guides/photos/CREDITS.tsv"` (field guide app)
- `load_index` → `"A:/guides/index.tsv"` (field guide app)
- `load_region_tags` → `"A:/guides/regions.tsv"` (field guide app)
- `load_manifest` → manifest TSV (translate app)
- `load_language` → language TSV (translate app)

**Practical risk:** All these files are produced by the Hive build pipeline and land on a FAT32 128 GB card. In the designed workflow the SD card is controlled content. A maliciously oversized file (e.g., 20 MB `index.tsv`) would cause `std::string::append` to allocate until ESP32-P4 PSRAM exhaustion, resulting in an `lv_malloc` failure and likely an abort or null-dereference downstream. Not reachable in normal operation; reachable if a user physically swaps in a sabotaged SD card.

**Concrete failable check:** Replace `CREDITS.tsv` with a 10 MB file of tab-separated junk. Open field guide → photo viewer. Observe device behavior (heap exhaustion / watchdog reset vs. graceful failure). Expected: crash or watchdog; no size guard in path.

---

### NF-3 — Path Traversal from Unsanitized `entry.path` in `load_index`
**Severity:** Low (offline, physically controlled SD)
**Confidence:** Confirmed by source
**Category:** Defense-in-depth gap

**Evidence:** `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp` (load_index, lines 227–282):

```cpp
// entry.path taken verbatim from third tab-delimited field of index.tsv
const std::string path = std::string(kGuidesDir) + entry.path;
// kGuidesDir = "A:/guides/"
// If index.tsv contains "../../lastfix.dat" in the path column:
// path = "A:/guides/../../lastfix.dat" = "A:/lastfix.dat"
```

The constructed path is passed to subsequent `read_text_file` calls and `file_exists` probes. LVGL's POSIX FS driver maps `A:` → `/sdcard/` and calls standard POSIX `open()`. The Linux VFS resolves `..` components; `/sdcard/guides/../../lastfix.dat` resolves to `/sdcard/lastfix.dat`. There is no canonicalization or prefix check.

**Reachability:** Requires physical SD swap with a crafted `index.tsv`. The ESP32-P4 has no network SD write path; this is purely a local physical-access scenario.

**Concrete failable check:** Craft an `index.tsv` whose path column contains `../../lastfix.dat`. Load the field guide → verify whether `load_index` opens `/sdcard/lastfix.dat` instead of a guides article.

---

### NF-4 — Waypoint Write Not Power-Loss Atomic
**Severity:** Low-Medium (data loss on power-loss mid-write, not corruption of other files)
**Confidence:** Confirmed
**Category:** Persistence robustness

**Evidence:** `apps/esp32_lvgl/src/esp32_lvgl_waypoints_app.cpp:179–228`:

```cpp
// Step 1: read old file size
uint32_t old_size = 0;
lv_fs_file_t rf;
if (lv_fs_open(&rf, kWaypointsPath, LV_FS_MODE_RD) == LV_FS_RES_OK) {
    lv_fs_seek(&rf, 0, LV_FS_SEEK_END);
    lv_fs_tell(&rf, &old_size);
    lv_fs_close(&rf);
}
// Step 2: pad shorter content to old size with '\n'
if (content.size() < old_size) {
    content.append(old_size - content.size(), '\n');
}
// Step 3: open for write and overwrite
lv_fs_file_t wf;
lv_fs_open(&wf, kWaypointsPath, LV_FS_MODE_WR);
lv_fs_write(&wf, content.data(), content.size(), &bw);
lv_fs_close(&wf);
```

**Analysis:**
- The padding defense (Steps 1–2) ensures that on FAT32, which does not guarantee truncation, shorter new content overwrites the old bytes rather than leaving stale data visible as valid TSV lines. The TSV parser skips blank lines (confirmed: `parse_waypoints:112–158` uses `strtod` after non-empty field check), so padding with `'\n'` is safe.
- **Not addressed:** Power loss between `lv_fs_open(WR)` (which truncates or overwrites) and `lv_fs_close`. If power is lost mid-write, `waypoints.tsv` is partially written with a mix of new and old content (or zeros depending on FAT32 cluster allocation). The result is a malformed TSV.
- **On-failure recovery (lines 497–513, 446–495):** `on_delete_clicked` reloads from disk on write failure; `on_save_clicked` pops the in-memory entry on write failure. But these paths fire on `lv_fs_write` error return, not on power-loss mid-write (where the close never happens).
- **kMaxWaypoints = 32** caps the file at 32 entries; maximum file size is bounded (each entry is at most ~100 bytes → ~3.2 KB), limiting blast radius.

**Trigger:** User saves/deletes a waypoint while the device loses power mid-SD-write. Next boot: `parse_waypoints` reads the partial file. `strtod` on truncated coordinate field returns 0.0; `std::isfinite(0.0)` is true but the range check (lines 135–140: lat ±90, lng ±180) would catch 0.0 as a valid coordinate, so a corrupted mid-write waypoint may load as `(0.0, 0.0)` rather than being discarded.

**Concrete failable check:** On device, save a waypoint at known coordinates. Power-cycle during the SD write. On reboot, open waypoints app and verify the saved waypoint is either present with correct coordinates or absent — not present with (0.0, 0.0) or other garbage coordinates.

---

### NF-5 — P4 Runtime Touch Architecture (Positive Controls Confirmed)
**Severity:** N/A (positive finding)
**File:** `platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp`

**Confirmed safe behaviors:**

| Mechanism | Evidence | Assessment |
|---|---|---|
| `s_touch_snapshot` written only from LVGL task (via `touch_read_cb` and 16 ms timer) | `touch_read_cb` is the LVGL indev driver callback, called on LVGL task only | No locking needed; safe by design |
| Hi8561 multi-touch: 13-byte read (offset 3 + 2×5), `response[0]` = finger count | Lines 327–444 (13-byte branch when `s_multitouch_enabled`) | Hardware-unverified per BENCH_TEST_CHECKLIST §3 |
| GT9895 multi-touch: full 80-byte buffer read always; second finger at `off1 = offset + 8` decoded when `s_multitouch_enabled && finger_count >= 2` | Lines 446–522 | Architecture correct |
| `0xFFFF/0xFFFF` no-touch sentinel handled for Hi8561 | Lines 407–415 | Correct |
| `s_multitouch_enabled` gated: `set_multitouch_enabled(true)` only in `bind_map_touch_input`, false in `unbind_map_touch_input` | `gps_page_input.cpp` callers confirmed | Multi-touch active only during map screen; correct isolation |
| Coordinate clamping to panel bounds | Lines 417–432, 490–510 | Prevents out-of-bounds indev reports |
| `create_display` uses SPIRAM=false for DMA buffers (internal RAM) | Lines 710–762 | Correct: MIPI DSI DMA must be in internal RAM |

---

### NF-6 — GPS Nonfinite Gap at NMEA Parse Level (No `isfinite` Check on Parsed Coordinates)
**Severity:** Very low (theoretical; requires malformed NMEA from hardware)
**Confidence:** Confirmed by source
**Category:** Defensive coding gap at module boundary

**Evidence:** `modules/core_gps/src/protocol/nmea/nmea_parser.cpp:44–61`:

```cpp
bool parseNmeaCoordinate(const char* value, const char* hemisphere, double& out)
{
    if (!value || !hemisphere || value[0] == '\0' || hemisphere[0] == '\0')
        return false;

    const double raw = parseDoubleField(value);  // strtod; returns NaN for "NAN"/"nan"
    const int degrees = static_cast<int>(raw / 100.0);  // UB if raw is NaN (C++ §7.10 [conv.fpint])
    const double minutes = raw - static_cast<double>(degrees * 100);
    double decimal = static_cast<double>(degrees) + minutes / 60.0;
    // ...
    out = decimal;
    return true;  // returns true even for NaN input
}
```

There is **no `std::isfinite` check** anywhere in `modules/core_gps` (grep confirmed zero matches for `isfinite`, `isnan`, `isinf`).

**Propagation path if NaN reaches the jitter filter:**
`GpsJitterFilter::update(NaN, NaN, ...)` → `haversine_m(last_lat_, last_lat_, NaN, NaN)` → `std::sin(NaN)` → NaN → `decision.distance_m = NaN` → `v_gps = NaN` → `NaN > v_max` evaluates **false** in IEEE 754 → jitter filter **accepts** the NaN fix → `events_.onLocationUpdated(fix_with_NaN_coords)` → `tick_gps_update` → map center set to NaN → tile-fetch for NaN tile coordinates.

**Contrast with LASTFIX.DAT protection:** `load_last_fix_file` (`gps_page_map.cpp:84–116`) checks `std::isfinite(record.lat) && std::isfinite(record.lng)` before accepting. This guard exists for the stored coordinate path but NOT for the live NMEA parse path.

**Practical reachability:** A real GNSS module (LC86G or similar) will never emit "NAN" in a NMEA sentence. This path requires hardware fault (line noise injecting ASCII letters into coordinate field) or a software GPS simulator that emits a malformed sentence. Risk is theoretical in normal operation.

**Concrete failable check (simulator only):** Inject a GNSS sentence `$GPRMC,120000.00,A,NAN,N,NAN,E,0.0,,010124,,,A*XX` into the GPS UART. Verify that the map center does not become NaN (observed as the map rendering at tile (0,0) or hanging on tile fetch).

---

## Part III — Full Positive Controls Inventory (Both Sessions)

| Control | Location | Evidence |
|---|---|---|
| Map tile UAF fix (eviction scan) | `gps_page_map.cpp:1130–1153`, `map_tiles.cpp:205–268` | Confirmed: scans all tiles for `cached_img == evicted_slot`, deletes `img_obj` before freeing slot |
| LASTFIX.DAT nonfinite guard | `gps_page_map.cpp:84–116` | `std::isfinite(record.lat) && std::isfinite(record.lng)` + range check; magic 'LFX1' guard |
| GPS snap-back fix | `gps_page_map.cpp:1726–1918` | Compares incoming fix against `last_known_lat/lng`; writes `g_gps_state.lat/lng` only when `follow_position || just_got_fix` |
| SD hot-swap debounce | `waypoints_app.cpp:692–750` | 2 consecutive reads at 5-second interval; `dir_exists("A:/")` blocking I/O throttled |
| `startTransmitAsync` non-blocking | `walkie_runtime.cpp:261–265` | `radio().startTransmitAsync()` — non-blocking; known-open blocking call resolved |
| Waypoint parse isfinite + range | `waypoints_app.cpp:112–158` | `strtod` + `std::isfinite` + ±90/±180 range; kMaxWaypoints=32 cap |
| Team NVS persistence | `idf_nvs_team_ui_snapshot_store.cpp` | Full NVS-backed implementation confirmed |
| Photo caption before image create | `field_guide_app.cpp:846 vs 857` | `append_photo_caption` called at 846 before `lv_image_create` at 857 |
| Binfont null-return handled | `resource_pack_registry.cpp:1104–1109`, `translate_app.cpp:208–212` | Both callers check `nullptr` return; font_missing flag set for CJK/RTL |
| `walkie_yield` clamped to ≥1 tick | `walkie_service.cpp:76–90` | Avoids `vTaskDelay(0)` busy-spin |
| `s_touch_snapshot` LVGL-task-only | `trail_mate_t_display_p4_runtime.cpp` | `touch_read_cb` called on LVGL task; no cross-task write; no lock needed |
| CREDITS.tsv red-tint danger warning | `field_guide_app.cpp:668–689` | Species field checked for "deadly/poison/venom/lookalike"; warning rendered with red tint **when CREDITS.tsv present and row matched** |

---

## Part IV — Coverage Gaps (Disclosed)

| Gap | Reason | Risk if Unread |
|---|---|---|
| `lv_binfont_create` decoder internals | Managed component; no source in repo tree | Cannot confirm crash-vs-null for corrupt binfont; MAP_UX_ROADMAP §4 flags this |
| `modules/ui_map_runtime/core_gps` GPS nonfinite at coordinate transform layer (map tile math) | `map_overlay_projector.cpp`, `map_tile_resolver.cpp` not read — these compute pixel coordinates from lat/lng | If NaN lat/lng reaches tile math, tile-fetch indices could become NaN-derived garbage (tile 0x0 or assertion) |
| `build_sd_content.py` CREDITS.tsv generation logic | Hive-side Python; confirms whether all photo stems always have CREDITS rows at build time | Would determine whether NF-1 is latent-at-build vs. only-on-card-damage |
| Hi8561 multi-touch on physical hardware | No P4 TFT unit test confirmed; BENCH §3 explicitly marks unverified | 13-byte read path could misbehave if Hi8561 firmware version differs from expected |

---

## Part V — Priority Recommendations

| Priority | Finding | Recommended Action |
|---|---|---|
| **P0 — Safety** | NF-1: CREDITS.tsv missing → lookalike photo shown without species warning | `append_photo_caption` return bool; caller renders "Species ID unavailable" fallback label. Do not display lookalike photos without a visible caption state. |
| **P1 — Robustness** | NF-4: Waypoint write not power-loss atomic | Add a temp-file rename pattern: write to `waypoints.tsv.tmp`, then rename over original (FAT32 rename is effectively atomic at the directory-entry level on ESP-IDF VFS). |
| **P2 — Defensive** | NF-2: `read_text_file` no size cap | Add a `max_bytes` parameter (e.g., 512 KB); return false if file exceeds cap. Apply 64 KB cap to CREDITS.tsv and index.tsv callers. |
| **P3 — Defensive** | NF-3: Path traversal from `entry.path` | After constructing `path`, verify it starts with `kGuidesDir` prefix (string prefix check). Reject entries that escape. |
| **P4 — Defensive** | NF-6: NMEA parse no isfinite | Add `std::isfinite(out)` check at end of `parseNmeaCoordinate`; return false if not finite. |
| **P5 — Verify** | Binfont decoder robustness | Obtain `lv_binfont_loader` source (LVGL managed component) and verify null-return vs crash on corrupt file. |
| **P5 — Verify** | Hi8561 multi-touch on HW | BENCH_TEST_CHECKLIST §3 open: run two-finger pinch on P4 TFT unit when available. |

---

**Ponytail confirmation:** Active this response. Root-cause traced for each finding to the specific code path, not just the symptom. Smallest correct fix direction provided. No speculative performance claims. No invented CVEs. Astra validates and handles fixes.
