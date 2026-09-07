I now have comprehensive evidence to write the full audit report. Let me compile everything:

---

# Trail-Mate T-Display-P4 Enterprise Audit Report

**Audit Date:** 2026-09-07
**Baseline:** `e3c0fbd68663bbeef09056a22cbce950065304bc` (main); active branch `channel-key-passphrase`, maintainer tag `98609d5`
**Auditor:** Astra Conductor audit worker (Anthropic Sonnet 4-6, Ponytail SKILL loaded, full intensity)
**GitNexus:** Unavailable — all evidence from static read-only source inspection
**Scope:** Runtime reliability/data-loss/performance for maps/GPS, LVGL lifetime/concurrency, SD persistence, walkie TX/task, offline apps (field guide, translate, waypoints). Mesh crypto deferred to security worker.

---

## Ponytail SKILL Confirmation

**Confirmed loaded.** `/home/zaidm/.codex/skills/ponytail/SKILL.md` read at session start. The skill governs the audit style: root-cause over symptom, one fix where all callers route through, no speculative abstractions.

---

## Inventory of Reviewed Paths

| Path | Purpose |
|------|---------|
| `AGENTS.md` | GitNexus tooling block (unavailable in audit) |
| `MAINTAINERS.md` §1-12 | Full project state; §12 authoritative |
| `BENCH_TEST_CHECKLIST.md` | Flash-day runbook |
| `MAP_UX_ROADMAP.md` §1-4 | Pinch plan, deferred/accepted items |
| `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp` | Field Guide app (complete read) |
| `apps/esp32_lvgl/src/esp32_lvgl_translate_app.cpp` | Translate app (lines 1–299) |
| `apps/esp32_lvgl/src/esp32_lvgl_waypoints_app.cpp` | Waypoints app (lines 1–150) |
| `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | GPS map render, markers, last-fix, team markers (complete) |
| `platform/esp/arduino_common/src/ui/screens/gps/gps_page_input.cpp` | Touch input, pinch, pan, zoom-apply (lines 1–700, 1300–1610) |
| `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp` | Tile decode cache, load_tile_image, UAF fix (lines 1–500, 970–1165, 2030–2110) |
| `platform/esp/arduino_common/src/walkie/walkie_service.cpp` | Walkie task, TX/RX, codec (lines 1–470) |
| `platform/esp/idf_common/src/walkie_runtime.cpp` | Radio session, startTransmitAsync (lines 1–265) |
| `modules/ui_shared/include/ui/support/lvgl_fs_utils.h` | read_text_file, file_exists, dir_exists (complete) |

**Coverage gaps (not read):**
- `platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp` (Hi8561/GT9895 multitouch read, touch_read_cb) — HW-unverified pinch path
- `builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults` — Kconfig settings
- `modules/ui_map_runtime/core_gps` — map tile source, coordinate transforms
- Walkie SSTV path, walkie_talkie_page_shell
- Emergency beacon app, trip computer app, sun/moon app, power app
- Team pairing / NVS key persistence
- Pipeline scripts (`build_sd_content.py`)
- SD content TSV files (index.tsv, regions.tsv, waypoints.tsv are generated; CREDITS.tsv staged only on Hive)

---

## SECTION 1 — Positive Controls: Prior-Fixed Classes Confirmed

### C1. Map fast-pan UAF (commit 595334e) — ✅ CONFIRMED FIXED

**Evidence:** `map_tiles.cpp:1130–1153`. Before `acquireSlot` reclaims a cache slot, `load_tile_image` scans every tile in `*ctx.tiles`. Any tile whose `cached_img == cache_slot` (i.e., an off-screen tile still pointing at the slot being evicted) has its `img_obj` deleted, `cached_img` cleared, `has_png_file` reset, and `contour_obj` nulled. This prevents LVGL from referencing the freed `img_dsc` buffer on the next draw pass.

```cpp
// map_tiles.cpp:1138-1153
for (auto& other : *ctx.tiles) {
    if (&other != &tile && other.cached_img == cache_slot) {
        if (other.img_obj != NULL) {
            lv_obj_del(other.img_obj);  // delete before the free below
            other.img_obj = NULL;
            ...
        }
        other.cached_img = NULL;
        other.has_png_file = false;
    }
}
```

**Regression failable check:** fast-pan at zoom 14 with > 32 visible tiles + rapid direction changes; should not freeze or watchdog-reset. Covered in BENCH_TEST_CHECKLIST.md §3.

---

### C2. LASTFIX.DAT nonfinite crash — ✅ CONFIRMED FIXED

**Evidence:** `gps_page_map.cpp:107–111`. After magic + size check:

```cpp
if (!std::isfinite(rec.lat) || !std::isfinite(rec.lng) ||
    rec.lat < -90.0 || rec.lat > 90.0 || rec.lng < -180.0 || rec.lng > 180.0)
{
    return false;
}
```

Magic `'LFX1'` (0x4C465831) + finite + range gate blocks the ±Inf path that previously caused `gps_screen_pos`'s longitude wrap loop to spin forever → watchdog.

**Regression failable check:** Write a hand-crafted `lastfix.dat` with correct magic, `lat=+Inf`, `lng=NaN` — device must boot normally, no panic. Covered in BENCH_TEST_CHECKLIST.md §2.

---

### C3. GPS snap-back (commit 8d7da87) — ✅ CONFIRMED FIXED

**Evidence:** `gps_page_map.cpp:1769–1797`. The tick now compares the incoming fix against `last_known_lat/lng` (the real GPS signal), NOT against `g_gps_state.lat/lng` (the map view center). It only writes `lat/lng` when `follow_position == true` or on the very first fix:

```cpp
const bool fix_moved =
    just_got_fix || !g_gps_state.has_last_known ||
    fabs(new_lat - g_gps_state.last_known_lat) > 0.0001 ||  // GPS-space delta
    fabs(new_lng - g_gps_state.last_known_lng) > 0.0001;
if (fix_moved) {
    ...
    if (g_gps_state.follow_position || just_got_fix) {
        g_gps_state.lat = new_lat;   // map center ← GPS only when following
        g_gps_state.lng = new_lng;
    }
    g_gps_state.last_known_lat = new_lat;  // truth always advances
    g_gps_state.last_known_lng = new_lng;
}
```

**Regression failable check:** Pan far (e.g. to Pakistan), wait 5+ s, pan again → map must stay in Pakistan view. Covered in BENCH_TEST_CHECKLIST.md §3, confirmed on-device 2026-07-11.

---

### C4. Safety-Critical Photo Captions — ✅ CONFIRMED PRESENT AND CORRECT

**Evidence:** `field_guide_app.cpp:626–692`. `append_photo_caption` reads `CREDITS.tsv`, matches by stem + photo number, extracts the `species` field, and tints the label red for any of {`deadly`, `poison`, `venom`, `lookalike`} (case-folded ASCII). Called at `show_photo_viewer_screen:846` before the image is displayed and before the CC attribution, guaranteeing the species warning is seen with the photo.

The lookalike hazard path (e.g., poison hemlock photo inside Yarrow article) is correctly rendered with `0xE04030` red text. This feature must not be removed or altered — it is a documented safety requirement per MAINTAINERS.md §12.2.

---

### C5. SD-Write Throttle — ✅ CONFIRMED CORRECT

**Evidence:** `gps_page_map.cpp:1804–1818`. The throttle requires BOTH `≥100m of movement` AND `≥60s since last save` (and OR only on the first save, `last_fix_save_ms == 0`). The OR-on-either-condition bug that would fire every ~3.6s at highway speed was already fixed and is not present.

---

### C6. Walkie Blocking `startTransmit` → Async — ✅ CONFIRMED CODE-LEVEL FIX

**Evidence:** `platform/esp/idf_common/src/walkie_runtime.cpp:261–265`:

```cpp
int startTransmit(Session* session, const uint8_t* data, size_t size)
{
    // Non-blocking launch so the walkie task keeps capturing mic frames during TX
    return resolve_state(session) ? radio().startTransmitAsync(data, size) : -1;
}
```

The comment explicitly documents the rationale. This addresses the known-open "blocking-startTransmit TX-choppiness risk" from MAINTAINERS.md §7. **On-device bench test still pending** (BENCH_TEST_CHECKLIST.md §5).

---

### C7. Last-Known Persistence — ✅ CONFIRMED WORKING

**Evidence:** `gps_page_map.cpp:950–960`. Lazy load once per page entry (`last_fix_load_attempted` flag), gated on `sd_ready`, skipped once a live fix is present. The marker is built on first paint via `update_map_tiles → update_last_known_marker_position`, covering the scenario (no-fix, just entered Map page) where the GPS tick never fires. Age label re-set only when the coarse text changes (prevents per-frame LVGL invalidate churn). Confirmed on-device 2026-07-11.

---

## SECTION 2 — Newly Identified Findings

---

### F1 — MEDIUM | Title Bar Shows Panned View Center Instead of GPS Fix

**Status:** Newly identified. Not in prior reviews.
**File:** `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp:656–662`
**Trigger:** User pans/zooms away from their GPS position (`follow_position == false`), then a GPS tick fires or the 30s forced title update fires.

**Root cause:** After the snap-back fix, `g_gps_state.lat/lng` holds the **map view center** (where the user panned to), while the actual GPS fix is in `g_gps_state.last_known_lat/lng`. The title bar reads `g_gps_state.lat/lng`:

```cpp
// gps_page_map.cpp:656-662
if (g_gps_state.has_fix && gps_ready) {
    ui_format_coords(g_gps_state.lat, g_gps_state.lng, ...); // ← view center!
    ...
}
```

If the user pans to Pakistan to check a route while physically in Wisconsin, the title bar will display Pakistani coordinates even though they have a Wisconsin GPS fix. The coordinates in the title bar are the primary navigation feedback; showing the wrong position is misleading.

**Existing mitigations:** None. The snap-back fix intentionally decoupled `lat/lng` from the GPS signal, but `update_title_and_status` was not updated to match.

**Impact/severity:** Medium. Not safety-critical (user still has the GPS map page open and can see the [P] button to re-center), but confusing in normal use. Reported rather than accepted because the fix is one-line.

**Confidence:** High (fully traced caller chain, reproducible by design).

**Minimal fix:** In `update_title_and_status` at `gps_page_map.cpp:660`, use `last_known_lat/lng` when `!follow_position`:

```cpp
// Minimal fix: use actual GPS fix for title, not view center
const double disp_lat = (g_gps_state.follow_position || !g_gps_state.has_last_known)
    ? g_gps_state.lat : g_gps_state.last_known_lat;
const double disp_lng = (g_gps_state.follow_position || !g_gps_state.has_last_known)
    ? g_gps_state.lng : g_gps_state.last_known_lng;
ui_format_coords(disp_lat, disp_lng, coord_fmt, coord_buf, sizeof(coord_buf));
```

**Failable check:** Pan to Pakistan (follow_position → false), wait for title refresh → title must show Wisconsin coordinates (last_known), not Pakistani. [P] to re-center → title shows Pakistani coordinates (now follow_position = true, lat/lng updated from GPS data).

---

### F2 — LOW | CRLF Not Stripped in Photo Attribution Parser — Trailing `\r` in License Field

**Status:** Newly identified.
**File:** `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp:534–611` (`append_photo_credits`)
**Trigger:** CREDITS.tsv has CRLF line endings (generated on Windows Hive), viewed on device.

**Root cause:** `append_photo_credits` and its sibling `append_photo_caption` extract lines as:
```cpp
const std::string line = text.substr(pos, line_end - pos); // line_end = '\n' pos, '\r' NOT stripped
```
The article body parser strips CR explicitly (line 758). These TSV parsers do not. If the CREDITS.tsv is CRLF:
- `append_photo_caption`: `species` is bounded by `t2` (the tab before `artist`), so CR does not leak into the species field. **Caption is safe.**
- `append_photo_credits`: the `license` field is bounded by `t4` (the tab before `url`). If a row has 6 fields (`t4` found), license is clean. If a row has only 5 fields (`t4 == npos`), license is `line.substr(t3+1, line.size()-t3-1)`, which includes any trailing `\r`. The attribution label would display e.g. `"CC BY-SA 4.0\r"`, with an invisible CR before `"/ Wikimedia Commons"`.

CREDITS.tsv has 6 fields per spec; the url field is always present in the pipeline output. So this is low-severity in practice but latent.

**Impact/severity:** Low — visual artifact (`\r` in rendered attribution on rare malformed rows), not a data loss or safety issue.
**Confidence:** High (static analysis).

**Minimal fix:** Strip `\r` from the end of each `line` in `append_photo_credits` and `append_photo_caption`, matching the pattern used in `load_index`:

```cpp
// One line, same idiom as load_index:
size_t line_end = (eol == std::string::npos) ? text.size() : eol;
if (line_end > pos && text[line_end - 1] == '\r') --line_end;
const std::string line = text.substr(pos, line_end - pos);
```

**Failable check:** Manually add a CRLF row to a test CREDITS.tsv with no url field; the license label must not contain a `\r` character.

---

### F3 — LOW / KNOWN-OPEN | `lv_binfont_create` Can Crash on Corrupt SD Binfont

**Status:** Known-open (MAP_UX_ROADMAP.md §4, explicitly deferred). Confirming it persists and remains unmitigated.
**File:** `apps/esp32_lvgl/src/esp32_lvgl_translate_app.cpp:208`

```cpp
st->font = lv_binfont_create(font_path.c_str());
```

A partially-written binfont (from an interrupted SD copy or corrupted card sector) will pass the existence check but fail internally. `lv_binfont_create` reads a binary format with no external validation; a corrupt header can cause an out-of-bounds read or assertion inside LVGL.

**Trigger:** Interrupted robocopy of the binfont, then opening the Translate app for that language.

**Impact/severity:** Low probability (FAT copy is nearly atomic for small files; only at-risk during active card copy), medium severity (crash/restart).

**Existing mitigations:** None at the call site; LVGL internal guards are implementation-dependent.

**Minimal fix (acceptable at deferred status):** Validate binfont header magic before creating: check that the first 4 bytes match the LVGL binfont magic. This is a 10-line addition. Add when next touching the translate font loading path.

**Failable check:** Truncate a binfont to 50 bytes, then open that language — device must not crash; must show the font-missing warning or a graceful degradation.

---

### F4 — LOW | `count_photos` Performs ≤6 Blocking `file_exists` Calls on LVGL Task Per Article Open

**Status:** Newly identified as a latent performance concern. Accepted per MAP_UX_ROADMAP.md §4 design decision (photos moved to one-at-a-time viewer). Elevating for visibility.
**File:** `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp:700–715`

`count_photos(photo_stem)` loops `i=1..6`, calling `ui::fs::file_exists(photo_path)` each iteration. Each call is a blocking `lv_fs_open` + `lv_fs_close` on the LVGL task thread. Maximum 6 SD opens on the render-critical path every time an article is opened.

**Impact/severity:** Low — bounded (max 6), human-paced (article tap), and the LVGL task blocks the display during this period (~few ms each). Acceptable per current design.

**Existing mitigations:** Cap at 6; contiguous-from-1 convention stops at first gap, so text-only articles exit immediately (n=0).

**Minimal future fix:** Store photo count in `index.tsv` as a 4th column. Eliminates all probes. Consider when next regenerating the pipeline.

---

### F5 — LOW | Walkie `startTransmitAsync` Fix Not Yet Device-Verified for TX Choppiness

**Status:** Known-open per BENCH_TEST_CHECKLIST.md §5 ("Walkie-talkie human voice bench test … pending").
**File:** `platform/esp/idf_common/src/walkie_runtime.cpp:261–265`

The code-level fix exists (`startTransmitAsync`), but actual voice quality on-device has never been bench-tested. TX choppiness could still arise from scheduler jitter, walkie task priority, or codec buffer underruns.

**Trigger:** PTT pressed during active LoRa mesh activity.

**Impact/severity:** Low-medium for field use (voice quality), not a data-loss or crash risk.

**Existing mitigations:** `walkie_yield` at lines 76–90 correctly clamps to `≥1 tick` (was fixed per comments), ensuring the task sleeps real intervals and does not busy-spin. `kWalkieTaskStack = 20KB` is adequate for codec2+jitter buffer.

**Action required:** Human voice bench test per BENCH_TEST_CHECKLIST.md §5 at next flash session.

---

### F6 — INFO | Decode Cache (32 slots) / Image Object Count Mismatch Remains Structural

**Status:** MAINTAINERS.md §12.5 references "12-slot decode cache vs 48 image objects" but current code has 32 (`kCapacity = 32` at `map_tiles.cpp:145`). The UAF fix (C1) correctly handles eviction. The mismatch is structural — normal fast pan will evict and re-decode, which is the intended behavior.

**No action required:** The UAF fix is the correct mitigation. The mismatch does not cause a bug.

---

### F7 — INFO | `no_coverage_hint` Timer Underflow at ~49-Day Uptime

**Status:** Newly noted, cosmetic.
**File:** `gps_page_map.cpp:1036`
`(now_ms - blank_since_ms) >= kBlankHoldMs` — both `uint32_t`. At `now_ms` wrap (~49.7 days), if `blank_since_ms` was set just before the wrap, the hint appears immediately. Field devices are unlikely to stay powered 49 days without a reboot.
**Impact:** Cosmetic (hint shows 1.2 s early). No action needed.

---

### F8 — INFO | "Near Me" Region Classifier Uses Squared-Distance on Non-Validated GPS Fix

**Status:** Newly noted, very low severity.
**File:** `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp:197–225` (`region_for_location`)
The GPS fix is gated by `fix.valid` (line 451) before calling `region_for_location`. If `valid=true` with NaN coordinates (defensive scenario), `dlat*dlat` → NaN, and `NaN < best_any_d` is always false, leaving `best_any = 0` (Pacific Northwest). This is a silent worst-case fallback, not a crash.
**Impact:** Very low — the `valid` flag is the correct gate; no action needed unless `valid` semantics are broadened.

---

## SECTION 3 — Known-Open Items (Not Newly Introduced, Not Yet Closed)

| Item | Status | Source |
|------|--------|--------|
| Hi8561 two-finger read HW verify | Unverified on HW | MAP_UX_ROADMAP §1, BENCH_TEST_CHECKLIST §3 |
| Arabic BIDI shaping on-device | Unverified on HW | BENCH_TEST_CHECKLIST §4 |
| Photo scroll stutter (LV_CACHE_DEF_SIZE=0) | Accepted/deferred | MAP_UX_ROADMAP §4 |
| `lv_binfont_create` corrupt binfont | Accepted/deferred (F3 above) | MAP_UX_ROADMAP §4 |
| Two apps duplicate SdContentBrowser | Accepted/deferred | MAP_UX_ROADMAP §4 |
| `place_map_marker` not reused by team/signal markers | Accepted/deferred | MAP_UX_ROADMAP §4 |
| Layer/zoom SD probes without SharedSpiLockGuard | Accepted (no-op on P4) | MAP_UX_ROADMAP §3 |
| Team app NVS key persistence | Open | MAP_UX_ROADMAP §2 |
| Team app PSK in clear | Open (security worker scope) | MAINTAINERS.md §7 |
| Walkie human voice bench test | Pending HW test | BENCH_TEST_CHECKLIST §5 |
| Unit A GPS hardware (L76K VCC) | Hardware defect | MAINTAINERS.md §2 |

---

## SECTION 4 — Priority Recommendations

**P1 (Apply at next code session, ~2 lines):** Fix title bar coordinate display (F1). The snap-back fix had a side-effect: the title bar now shows the panned view center instead of your GPS position. One conditional in `update_title_and_status` corrects it. Use `last_known_lat/lng` when `!follow_position`.

**P2 (Apply when next touching photo TSV parsers, ~3 lines):** Add CR stripping to `append_photo_credits` and `append_photo_caption` (F2). Same 3-line idiom already in `load_index`. Prevents CRLF-generated CREDITS.tsv from putting `\r` in the on-screen license attribution.

**P3 (Next flash session, no code change):** Run BENCH_TEST_CHECKLIST.md §5 (walkie human voice test). The `startTransmitAsync` fix is code-verified; device verification is the remaining open gate.

**P4 (Next flash session):** Run BENCH_TEST_CHECKLIST.md §3 (Hi8561 two-finger pinch). Double-tap fallback is safe if pinch fails — but the failure mode must be documented (either "confirmed working" or "Hi8561 multi-read layout mismatch confirmed, fallback activated permanently").

**P5 (Pipeline regeneration, no firmware change):** When next regenerating `index.tsv`, add photo count as column 4. Eliminates the 6-probe `count_photos` on every article open (F4). Low priority but trivially cheap at pipeline time.

**P6 (Future, when touching translate font loading):** Add binfont magic pre-validation before `lv_binfont_create` (F3). Prevents crash-restart on a partially-written SD binfont file.

---

## SECTION 5 — Safety-Critical Content Integrity Observations

1. **Photo caption mechanism preserved and correct** (C4): The `append_photo_caption` function, CREDITS.tsv pipeline, and red-tint hazard logic are all intact. The species caption appears **before** the photo image in the render order (`show_photo_viewer_screen:846` → then image at `:857`). The caption is rendered **above** the image, meaning it is seen before the photo loads. This is the correct order for lookalike safety.

2. **Medical/survival content rule (MAINTAINERS.md §12.2):** Not audited for content correctness (out of scope for code audit). The structural protection — ASCII-only pipeline rejection, self-reliance posture, honest "needs real care" notes — is not enforced at the firmware layer; it is a pipeline and content discipline. No firmware enforcement is possible for text content.

3. **CC-BY attribution (F2):** The attribution is legally required. CR stripping (F2 fix) must not accidentally strip or corrupt the artist/license fields — the proposed fix is bounded to the line level, not field level, and is safe.

---

## SECTION 6 — Concurrency / LVGL Lifetime Summary

| Subsystem | Threading model | Guards present | Risk |
|-----------|----------------|----------------|------|
| Map tiles decode | LVGL task only | SharedSpiLockGuard on SD reads; in_use flag on slots | Low (single-task LVGL) |
| GPS tick | LVGL task (timer callback) | `is_alive()` guard on every entry point | Low |
| Touch poll | LVGL timer (16ms) | Modal-open bail, exiting guard, reset_map_pinch_state on modal | Low |
| Walkie task | Dedicated FreeRTOS task, priority 7 | portMUX_TYPE for status updates; exclusive radio hold | Acceptable; yield fix in place |
| SD last-fix save | LVGL task (GPS tick) | SharedSpiLockGuard + 100m+60s throttle | Low |
| Photo decode | LVGL task (photo viewer build) | LVGL image decoder; free_photo_dscs on every nav | Low |
| Binfont load | LVGL task (language select) | None on corrupt binfont (F3) | Low-medium |

All LVGL widget access is single-threaded. The walkie task does not touch LVGL objects directly — it communicates via the `portMUX`-protected `s_status` struct. The SPI radio ownership uses `setExclusiveHold(true)` to prevent the mesh pump from touching the radio during voice sessions.

---

## SECTION 7 — SD Content File Structure Audit

| File | Parser | CR handling | Bounds | Notes |
|------|--------|-------------|--------|-------|
| `A:/guides/index.tsv` | `load_index` | ✅ per-line strip | ✅ category dedup loop | 3-field TSV, graceful on malformed rows |
| `A:/guides/regions.tsv` | `load_region_tags` | ✅ per-line strip | ✅ comma tokenizer with trim | "all" expansion correct |
| `A:/guides/photos/CREDITS.tsv` | `append_photo_credits` + `append_photo_caption` | ❌ no CR strip (F2) | ✅ t1/t2/t3/t4 bounds | 6-field expected; url bounds license |
| `A:/translate/manifest.tsv` | `for_each_tsv_row` | ✅ (len-1 strip) | ✅ min_fields=5 check | LangInfo fields indexed safely |
| `A:/translate/<code>.tsv` | `for_each_tsv_row` | ✅ (len-1 strip) | ✅ min_fields=4 check | roman field optional (f.size()≥5) |
| `A:/waypoints.tsv` | `parse_waypoints` | ✅ per-line strip | ✅ isfinite + range + kMaxWaypoints=32 | strtod + isfinite guard matches lastfix |
| `A:/lastfix.dat` | `load_last_fix_file` | N/A (binary) | ✅ magic + isfinite + range | Fixed-size record, versioned magic |

---

*Report complete. All findings are based on static source evidence. No hardware execution results are claimed. Astra validates and applies fixes.*
