# Trail Mate (T-Display P4) — Maintainer's Manual

Authoritative maintenance guide for the Trail Mate ESP-IDF firmware. Written for any
operator (Claude Code, Axiom/OpenClaw, TARS/Hermes, or a human). If you are picking this
project up, read this file first, then `BENCH_TEST_CHECKLIST.md` and `MAP_UX_ROADMAP.md`.

Owner: Mikhail ("Z"). Last major update: 2026-07-02.

---

## 1. What this is

Trail Mate is a standalone, offline, off-grid trail computer firmware for two LilyGo
T-Display P4 units. It is a **fork** of `vicliu624/trail-mate` (upstream), heavily
extended. It runs from the `ota_1` "flex bay" slot on the same hardware that also boots
the licensed MeshOS and a Meshtastic port; the **boot launcher** that switches between
those slots is a SEPARATE project (repo `Boot-Launcher-for-T-Display-P4`, tracked in the
OpenClaw `tdisplay-p4-dualmesh.md` project doc). This manual is only about the Trail Mate
IDF firmware app.

What the firmware does today (all offline, no cell/wifi needed):
- **Offline maps** with your GPS position, mesh-node markers, tracker/route overlays.
- **LoRa mesh text chat** + a **Team page** showing group members' live positions.
- **Walkie-talkie** (PTT voice over LoRa), **SSTV**, **GNSS sky-plot**, flashlight w/ SOS,
  stopwatch, node radar, and other small apps.
- **Translate**: 12-language offline phrasebook (two-way, hand-over mode).
- **Field Guide**: 54-article offline survival/foraging/nature handbook with plant photos.

Purpose per Z: a family/outdoors device that stays useful with zero connectivity, and a
teaching aid for survival and plant identification.

---

## 2. Hardware — two units

| | Unit A | Unit B |
|---|---|---|
| Serial | COM6 (varies) | COM8 (varies) |
| MAC | f9:fa:fc:5d... | 30:ed:a0:e1:bf:57 |
| Node id | F9FAFC5D | A0E1BF57 |
| Role | member | leader |
| GPS | **DEAD (hardware)** | works (locks 115200) |

- Display: 540x1168 portrait, LVGL 9.5. TFT panel = Hi8561 touch (the shipping
  `tdisplayp4_tft` build); the AMOLED variant uses a GT9895 touch + rm69a10 panel.
- Radios: SX1262 LoRa + a separate ESP32-C6 wifi/BT co-processor. GPS = L76K on the P4's
  own UART (pins TX=23 RX=22), NOT on the C6.
- **Unit A GPS is dead hardware** (electrically silent at all bauds; Unit B works on the
  identical binary). Post-trip fix = multimeter the L76K VCC rail, reflow/replace. Do NOT
  grind firmware for it. See `.claude` memory `tdisplay-p4-gps-unit-a-hardware`.
- **Physical-layer-first rule**: if a working RF/SD feature regresses, re-seat the SD card
  and antenna and check config/proximity BEFORE touching firmware. A shipping firmware
  failing identically on both units is almost never the code.

---

## 3. Repo, branch, remotes

- Working copy: `C:\Users\zaidm\tdisplay-p4-dualmesh\trail-mate`
  (WSL path: `/mnt/c/Users/zaidm/tdisplay-p4-dualmesh/trail-mate`).
- Active branch: **`channel-key-passphrase`** (all current work lives here).
- Remotes: `fork` = github.com/Mikhail-Za/trail-mate-T-display-p4- (PRIVATE, Z's, this is
  where we push); `origin` = upstream vicliu624/trail-mate (do NOT push there).
- Push target: `git push fork channel-key-passphrase`. HEAD as of this writing = `b7c1875`,
  fully pushed.
- **Commit incrementally** (Z's standing rule): commit each verified change as you go,
  never let uncommitted work pile up. Goalkeeper-style tools revert to the last commit.
- **No secrets in commits** (standing rule): never commit raw serial captures or keys;
  scan staged diffs. (Trail Mate itself has no secret material; the launcher project did
  once leak a wifi password — see its doc.)
- GitNexus indexes this repo (`AGENTS.md` block). Use the GitNexus MCP tools to understand
  code and run impact analysis before edits when available.

---

## 4. Building

Toolchain (Windows / The Hive): ESP-IDF v5.5.4 at `C:\Users\zaidm\esp\esp-idf-v5.5.4`.
Source `export.ps1` first. Python env with build extras (Pillow for the content pipeline):
`C:\Users\zaidm\.espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe`.

**Targets** (`builds/esp_idf/target_profiles.cmake`): `tdisplayp4_tft` (PRIMARY — the
shipping TFT build), `tdisplayp4_amoled` (kept building for cross-validation; same SD
stack), `tab5`, `tdeck`, `tlora_pager`, `twatch`.

Build the primary target:
```powershell
. C:\Users\zaidm\esp\esp-idf-v5.5.4\export.ps1
cd C:\Users\zaidm\tdisplay-p4-dualmesh\trail-mate
idf.py -B build.tdisplayp4_tft -DTRAIL_MATE_IDF_TARGET=tdisplayp4_tft build
```
Success = `build.tdisplayp4_tft/trail-mate.bin` generated, partition has headroom.

**sdkconfig — defaults vs generated (READ THIS BEFORE CHANGING CONFIG):**
- Source of truth: `builds/esp_idf/targets/<target>/sdkconfig.defaults`.
- Generated (build dir, do NOT hand-edit as the source): `build.<target>/sdkconfig.<target>`.
- A defaults edit does NOT auto-apply while the generated file exists. Apply recipe:
  1. edit `sdkconfig.defaults`; 2. back up the generated `sdkconfig.<target>`;
  3. DELETE the generated file; 4. rebuild (it regenerates from defaults);
  5. `Compare-Object` regen vs backup to confirm ONLY the intended keys changed (zero drift).

**Cross-target build health (2026-07-02):** `tdisplayp4_tft` and `tdisplayp4_amoled` build
FULLY GREEN. `tab5` is blocked by PRE-EXISTING -O2 format-truncation errors in old code
(settings_page_components.cpp / meshcore_adapter.cpp — not our code; tab5 is display-shell
only, never compiles the GPS page). `tdeck` and `tlora_pager` (PlatformIO / Arduino) are
blocked by a PRE-EXISTING `meshcore_adapter.cpp:5436` `Serial.printf` `%lu` format error
from the June MeshCore arc (logging-only cast would fix). `gat562` builds SUCCESS. None of
these pre-existing breaks are caused by the map/translate/field-guide work.

---

## 5. Flashing / deploy (Windows-side only)

Devices connect over USB to the Windows host. Flashing is a **Windows/OpenClaw job**;
Hermes/WSL cannot drive the COM ports.

```powershell
C:\Users\zaidm\deploy-both-units.ps1
```
- Sources export.ps1, builds, then MAC-guards each flash so Unit A's binary can't go to
  Unit B or vice versa. App-only flash at `0xBC0000` (the ota_1 flex bay). Boot-verifies
  via a 16s serial capture with an RTS reset; exits 0 only if BOTH units flashed AND booted
  with no panic. Expect `RESULT: PASS (both units flashed AND booted clean, no panic)`.
- Check COM ports first: `[System.IO.Ports.SerialPort]::GetPortNames()`. If the two units
  aren't present, do NOT flash.
- **As of 2026-07-02 the devices are AWAY with Z** — everything below "current state" is
  compile-verified but NOT yet flashed. The on-device test script is `BENCH_TEST_CHECKLIST.md`.

---

## 6. SD card content pipeline

The maps, translations, guides, fonts, and plant photos all live on the SD card (FAT32),
not in flash. One pipeline builds the whole staging tree; one robocopy loads a card.

- Source content: `tools/offline_content/` (in-repo): `phrases_master.tsv`,
  `translations/<code>.tsv` (12 langs), `guides/<section>/*.txt` (54 ASCII articles),
  `photos_raw/<section>/<article>/N.jpg` (61 verified Commons photos + credits.tsv),
  `build_sd_content.py` (the assembler), `photo_assignments.md` (the photo shot-list spec).
- Run the pipeline (needs `Pillow` in the IDF python env: `pip install Pillow`):
  ```
  <idf-python> tools/offline_content/build_sd_content.py [staging-root]
  ```
  Default staging root = `C:\osm-tiles\sd-staging`. It validates (id-complete translations,
  ASCII-only guides), merges TSVs, subsets per-language fonts with `lv_font_conv` (run as
  `node <npm-root>/lv_font_conv/lv_font_conv.js`, NOT npx/shell — the `--symbols` string
  contains `<>&|` that cmd.exe mangles), and re-encodes every photo to 500px-wide **baseline**
  JPEG (TJPGD on-device cannot decode progressive). Downloaded Noto/DejaVu fonts cache in
  `fonts_cache/` (gitignored).
- Staging tree (all next to each other under the root): `maps/` (the offline tiles),
  `translate/` (~330KB), `guides/` incl. `guides/photos/` (~4MB).
- Load a card (card reader; the unit SDs are not directly PC-accessible in the device):
  ```powershell
  robocopy "C:\osm-tiles\sd-staging" "E:\" /E /NFL /NDL /NJH /R:1 /W:1 /MT:8
  ```
  (adjust `E:` per card; only new files copy). Card MUST be **FAT32** (exFAT's big clusters
  balloon the ~970k tiny tile files). See `BENCH_TEST_CHECKLIST.md` §0a.
- On device, LVGL reads the SD via the `A:` POSIX FS driver -> `A:/...` resolves to
  `/sdcard/...`.

---

## 7. Feature inventory + current state (all compile-verified, NOT yet on-device)

- **Offline maps** — engine reads `/sdcard/maps/base/osm/{z}/{x}/{y}.png`. Coverage:
  whole-CONUS z0-12 + WI/MN/IL/IA z13-14 + **all CONUS National Parks & NPS public lands
  z13-14** (970,584 tiles, ~4.6GB). Rendered on the Hive via Docker osm-carto; the PostGIS
  DB + tile cache are preserved for top-ups (`render-parks-z13-14.ps1`, `finish-conus-render.ps1`).
  Map render on-device was Z-CONFIRMED working 2026-06-26. See `.claude` memory
  `tdisplay-p4-map-tiles` and `project_tdisplay_p4_map_tiles.md`.
- **Map gestures** (commit 5e6b2dd) — pinch-to-zoom, double-tap zoom in, two-finger-tap
  zoom out. Two-point touch read in the P4 runtime, hook `platform::ui::device::touch_points`,
  integer pinch machine in the 16ms poll. Files:
  `platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp`,
  `platform/esp/arduino_common/src/ui/screens/gps/gps_page_input.cpp`. On-HW UNTESTED:
  the Hi8561 two-finger read is the one unproven path; double-tap is the fallback.
- **Map polish** (7a94150) — last-known-position marker (grey ring, SD-persisted, survives
  reboot) with an age tag; "No offline tiles here" hint so blank never reads as broken.
- **Translate app** (a713861) — `apps/esp32_lvgl/src/esp32_lvgl_translate_app.cpp`. 166
  phrases x 12 langs. SD layout `A:/translate/`. Non-Latin fonts are SD binfonts loaded to
  PSRAM. Arabic uses LV_USE_BIDI + LV_USE_ARABIC_PERSIAN_CHARS + a DejaVu subset (Arabic
  shaping is the riskiest on-device render path to verify).
- **Field Guide app** (a713861, photos 18b52b9) —
  `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp`. 54 ASCII articles in 6 sections.
  Edible/hazard plant articles show real photos (61 Wikimedia Commons images, each visually
  species-verified, free-licensed, credited in `guides/photos/CREDITS.tsv`). Article view
  probes `A:/guides/photos/<section>/<stem>/N.jpg` by convention (no index).
- **JPEG decoder** (18b52b9) — CONFIG_LV_USE_TJPGD enabled (both P4 targets) for the plant
  photos; side effect: satellite `.jpg` tiles become decodable and the decoder-aware
  availability gate un-gates them automatically.
- **2026-07-01 code-review fixes** (06710be..a35febf) — 15 findings from a max-effort review
  of the earlier map-tiles session, all fixed. Headliners: last-known persistence was dead
  as shipped; a corrupt lastfix file could feed +/-Inf into `gps_screen_pos`'s wrap loop and
  crash-loop the device (now `std::isfinite`-guarded); an i18n key was broken; layer-switch
  now refuses unavailable layers at the engine choke point; modal title overlap; SD-write
  throttle; LV_USE_FS_POSIX silently enabled an SD i18n pack scan on IDF (decoupled); pinned
  SPIRAM heap opts. Detail in `.claude` memory and the commit messages.
- **Team app, walkie, SSTV, sky-plot** — pre-existing, shipped. Team app has open items
  (NVS key persistence so pairing survives reboot; secure passphrase pairing — PSK is
  currently in the clear). Walkie has an open human voice bench-test + a blocking-startTransmit
  TX-choppiness risk.

---

## 8. Load-bearing architecture facts & gotchas (do not rediscover)

- **LVGL `A:` drive**: `A:/x` -> `/sdcard/x` via CONFIG_LV_USE_FS_POSIX (letter 65='A',
  path "/sdcard"). Registered by lv_init.
- **Decoders**: lodepng (PNG tiles + fonts) and TJPGD (JPEG photos + satellite tiles) are
  auto-registered by lv_init. **TJPGD decodes BASELINE JPEG ONLY** — the content pipeline
  re-encodes every photo to baseline; a progressive JPEG will silently fail to render.
- **Allocator**: CONFIG_LV_USE_CLIB_MALLOC routes lv_malloc to the IDF heap; with
  SPIRAM_USE_MALLOC + MALLOC_ALWAYSINTERNAL=16384 every >=16KB alloc (256KB tiles) lands in
  PSRAM. Those two SPIRAM opts are now pinned in defaults (they were relied-on-but-unpinned).
  CONFIG_LV_MEM_SIZE_KILOBYTES=192 is INERT under clib malloc (kept as fallback sizing).
- **Fonts**: only montserrat_14 compiles by default; the P4 UI also needs 20 + 24 (enabled
  in defaults). Translate's non-Latin glyphs are SD binfonts, not compiled in.
- **The map-tiles "3 stacked config gaps" history**: tiles were correct on the card but drew
  blank until THREE IDF config gaps were fixed (FS driver, PNG decoder, PSRAM allocator) —
  commits 05fc158/8677eee/675d464, 2026-06-26. If maps ever go blank again, that trio is the
  first suspect, per-layer (open-fail = placeholder, decode-fail = blank white).
- **Tile render pipeline gotchas** (Hive-side, `osm-run` Docker): `meta2tile.py` must apply
  ALL ranges per zoom (was fixed from last-line-only); a `render_list` launched <~16s after
  `docker restart` races the renderd socket and fails silently (wait for the socket or run on
  a warm container).
- **AMOLED variant parity**: `tdisplayp4_amoled` defaults now carry the full offline
  map+content LVGL stack (FS/PNG/CLIB/PSRAM/JPEG) it was missing, plus montserrat 20/24 +
  mbedtls chachapoly it needed just to build.

---

## 9. Verification discipline (how we work here)

1. **Compile-verify every change** on `tdisplayp4_tft` before committing (and the config
   apply recipe's zero-drift check when config changes).
2. **Commit incrementally**, one verified change per commit, push to `fork`.
3. On-device changes are gated behind the hardware: when the units are present, flash with
   `deploy-both-units.ps1` and run `BENCH_TEST_CHECKLIST.md` top to bottom.
4. Use GitNexus impact analysis before non-trivial symbol edits when the MCP is available.
5. For big/risky multi-file work, the parallel-subagent + adversarial-verify pattern is the
   house style (that's how the 2026-07-01 review ran).

Pending on-device verification (devices away): all of section 7 — especially the Hi8561
pinch two-finger read and Arabic text shaping. Runbook = `BENCH_TEST_CHECKLIST.md`.

---

## 10. Division of labor between operators

- **Axiom / OpenClaw** (Windows, The Hive): can do EVERYTHING — build, run the content
  pipeline + tile renders (Docker `osm-run`), flash both units, run the bench checklist.
  The full maintainer.
- **TARS / Hermes** (WSL): can read/understand, edit code and content, run the content
  pipeline from `/mnt/c` if Pillow + node are present, and reason about the firmware. CANNOT
  flash (no COM-port access from WSL) and building via WSL is not set up — treat build/flash
  as Windows-side. Hermes's role is understanding + code/content maintenance, then hand
  build/flash to Axiom or Z.
- **Claude Code**: primary builder of this session's work.
- Boundaries: never blind-update anything; never modify exec allowlists in auto mode (that
  is a firm boundary — needs Z outside auto mode); no secrets in commits; propose don't
  silently apply changes to the tile render set or firmware config that affect on-device
  behavior when the devices can't be re-verified.

---

## 11. Commit map — 2026-07-01/02 session

```
b7c1875 Checklist: plant-photo on-device check
18b52b9 Field Guide plant photos (61 verified Commons images) + JPEG decoder
a713861 Translate (12-lang phrasebook) + Field Guide (survival) apps
6cb782e Settings coverage string incl. parks; checklist headroom fix
98d3bb6 Checklist: park-tiles SD load step
19b8fe5 Bench checklist + amoled/tab5 target-defaults repair
7a94150 Map polish: last-fix age tag + no-coverage hint
5e6b2dd Map: pinch-to-zoom + double-tap + two-finger-tap (P4 multi-touch)
a35febf..06710be  Code-review: 15 findings fixed (persistence/Inf-guard/i18n/
                  layer-gate/title-overlap/throttle/pack-scan/sdkconfig pins)
f027c86 Map UX roadmap + pinch research  (pre-review baseline)
```

Companion docs in-repo: `BENCH_TEST_CHECKLIST.md` (flash-day script),
`MAP_UX_ROADMAP.md` (pinch status + deferred UX items + code-review follow-ups §3).
Detailed history also lives in Claude Code memory (`tdisplay-p4-*` project files).
