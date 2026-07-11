# Trail Mate (T-Display P4) — Maintainer's Manual

Authoritative maintenance guide for the Trail Mate ESP-IDF firmware. Written for any
operator (Claude Code, Axiom/OpenClaw, TARS/Hermes, or a human). If you are picking this
project up, read this file first, then `BENCH_TEST_CHECKLIST.md` and `MAP_UX_ROADMAP.md`.

Owner: Mikhail ("Z"). Last major update: 2026-07-11.

> **Sections 5-9 below were written 2026-07-02 when the devices were away and everything was
> "compile-verified, not flashed." That is no longer the current state. Read `## 12` (the
> 2026-07-11 update) FIRST for the current on-device status, the full map render pipeline, the
> field-guide expansion, and the SD-card workflow. Sections 1-11 remain accurate as baseline.**

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

---

## 12. 2026-07-11 update — current state, map render pipeline, field-guide expansion, SD workflow

This section is the CURRENT state and SUPERSEDES the "devices away / not flashed" caveats in
sections 5-9. Both units are flashed and on-device verified.

### 12.1 On-device state (CONFIRMED, not just compiled)
- Both units flashed (`deploy-both-units.ps1`, app-only @0xBC0000) and booting clean, no panic.
- Maps render on device; **both units now get a GPS fix** (Unit A's GPS started working — it was
  hardware-marginal, not firmware; keep the physical-layer-first rule if it drops again).
- Field Guide (incl. the expansion below), Waypoints (save-to-SD works), Translate all working.
- The map fast-pan crash (12.5) is fixed and flashed.
- **SD cards are two 128GB FAT32 (32KB cluster)** cards holding the full set (world + regional
  maps + field guide + translate + firmware). The old 64GB cards could NOT hold the full set.

### 12.2 Field Guide expansion
- New sections: **Animals** (12 articles) and **Medicinal & First Aid** (9), added to the
  original survival/edible/hazard/nature/preparedness. The app builds its category list from
  each article's title line via `guides/index.tsv` (regenerated by the pipeline).
- **Near Me** (GPS regional view): `guides/regions.tsv` tags each article to 8 US regions; the
  app maps the live GPS fix to a region and lists matching articles. Source =
  `tools/offline_content/guides_regions.tsv` (pipeline copies it to `guides/regions.tsv`).
- **Per-photo species captions (SAFETY feature):** the one-at-a-time photo viewer shows each
  photo's species from `guides/photos/CREDITS.tsv`; hazard wording (deadly/poison/venom/
  lookalike) tints RED. This exists so a deliberately-included deadly-lookalike contrast shot
  (poison hemlock in the Yarrow deck, yew in the pine deck, coral-snake mimics) can never be
  mistaken for the plant/animal the article is about. Code: `append_photo_caption` in
  `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp`.
- **Self-reliance rewrite (35 medical/survival articles):** "seek care" is now ONE option
  ("evacuate if you can safely reach help"), not the terminal answer. Each leads with concrete
  field actions + 2nd/3rd self-reliance options, with an honest "needs real care" note where
  there is genuinely NO field substitute (antivenom, rabies PEP, epinephrine, amatoxin liver
  failure, cyanide) instead of a fabricated cure. Researched vs WMS/WFR/NOLS/CDC/EPA/military
  field manuals and independently safety-verified. **RULE for any future medical-content edit:**
  keep this posture (real self-reliance, honest hard truths, NO invented cures — a fabricated
  remedy gets people killed), and stay ASCII-only (no em-dashes / non-ASCII, or the pipeline
  rejects it). A verification pass on the last rewrite caught + removed a dangerous fabricated
  "wood ash as a salt substitute" tip (wet ash is caustic lye) — always re-verify medical text.
- Photos: ~96 verified free-licensed Wikimedia images (species + diagnostic feature visually
  confirmed; deadly lookalikes labeled in CREDITS). **Always eyeball-verify any new safety-
  critical lookalike photo** (snakes, yarrow/hemlock, pine/yew) before staging.

### 12.3 Map coverage + how tiles are made (Hive-side)
Coverage in staging (`C:\osm-tiles\sd-staging\maps\base\osm\{z}/{x}/{y}.png`, standard XYZ,
256px PNG, `A:/maps/...` on device):
- **z0-6 = whole globe** (Natural Earth world overview: continents, oceans, borders, major
  cities/labels).
- **z7-14 = regional detail** — US (whole CONUS + National Forests/Wilderness/BLM + all NPS
  parks + TX + WI/MN/IL/IA) and Pakistan (country + Karachi). Total ~2.24M tiles, ~72GB
  physical @ 32KB clusters.

Two render engines, both writing into the SAME tile tree. Toolchain lives on the Hive at
`C:\osm-tiles\work` (NOT in git):
1. **OSM regional detail (z7-14)** — Docker container `osm-run` (overv/openstreetmap-tile-
   server = mapnik + renderd + PostGIS `gis` DB with the US+Pakistan OSM extracts; volume
   `osm-data`). Scripts: `gen_ranges.py`/`gen_park_ranges2.py`/`gen_pk_ranges.py` (bbox ->
   per-zoom ranges file `z x0 x1 y0 y1`); `render-tiles.sh <ranges>` (`render_list -m default`
   into the metatile cache); `meta2tile.py <cache> <out> <ranges>` (walks the WHOLE renderd
   cache -- slow by design -- exporting in-range tiles to staging); `export_after_render.sh
   <ranges> <label>` (render+export in one). **Add a region:** put its OSM extract pbf in
   `/work`, then (as user `renderer`) `osm2pgsql --append --slim -G --hstore
   --tag-transform-script /data/style/openstreetmap-carto.lua -S
   /data/style/openstreetmap-carto.style --flat-nodes /data/database/flat_nodes.bin <pbf>`,
   then gen-ranges -> render -> export. `pk_full.sh` is the worked Pakistan example.
2. **Natural Earth world base (z0-6)** — python-mapnik 3.1 (already in `osm-run`), NO PostGIS.
   Artifacts at `C:\osm-tiles\work\ne`: `ne_style.xml` (mapnik style over the ne_50m_*.shp
   shapefiles) + `render_ne.py` (renders every z0-6 tile). Re-render:
   `docker exec osm-run python3 /work/ne/render_ne.py`. Data: naciscdn.org/naturalearth/50m/
   {physical,cultural}/*.zip (public domain). The render DB has ONLY US+Pakistan, so a real
   world map REQUIRES this NE base -- do not try to render the globe from the OSM DB.

### 12.4 SD card workflow
- Cards MUST be **FAT32**. **Cluster size is load-bearing:** each of the ~2.24M tiny tiles
  rounds up to one cluster, so physical size = tiles x cluster. 32KB -> ~72GB (fits 128GB with
  ~47GB free); 16KB -> ~36GB; **64KB -> ~143GB, overflows even a 128GB card -- never use 64KB.**
  A 64GB card cannot hold the full set. Windows won't FAT32-format >32GB; use guiformat/Rufus,
  pick 16-32KB clusters. exFAT is worse (128KB default cluster) and the firmware wants FAT32.
- Staging `C:\osm-tiles\sd-staging` is a **complete card image**: `maps/ guides/ translate/
  firmware/ notifications/ voice/ adverts.bin autorun.*`. `firmware/` holds the MeshOS launcher
  app binaries (meshos.bin, trailmate-chat.bin, etc.) captured off a working card -- the device
  needs them.
- **Copy = DETACHED robocopy.** `C:\osm-tiles\work\copy_full_card.ps1 -Drive F:` copies staging
  -> a card (`/E`, additive) + writes a `.status` marker. Launch via
  `Start-Process powershell ... -WindowStyle Hidden` so it survives agent/session restarts. It
  is SLOW (~2 MB/s, ~60-70 files/sec -- FAT32 rewrites its dir+FAT per file), ~8-9 hours for the
  full ~72GB per card even with /MT:16 (run both cards in parallel). A small content update
  (just `guides/`+`translate/`) is fast -- copy only those.
- **Preserve user data:** the device writes `waypoints.tsv`, `captures/`, `SSTV/`, `LASTFIX.DAT`
  to the SD. Never `/MIR` a card that has them without backing them up first.

### 12.5 New gotchas (do not rediscover)
- **Map fast-pan crash (FIXED, commit 595334e):** at the deepest zoom, fast panning could evict
  and `lv_free()` a decoded-tile buffer while a hidden-but-alive image object still pointed at
  it -> use-after-free -> freeze then crash. Fix: when a decode slot is reclaimed, invalidate
  any tile still referencing it (`load_tile_image` in `platform/esp/arduino_common/src/ui/
  widgets/map/map_tiles.cpp`). The 12-slot decode cache vs 48 image objects mismatch was the
  root. Optional un-done follow-ups: clamp zoom to available tiles; debounce per-touch recompute.
- **python-mapnik 3.1 SRS:** use `+init=epsg:3857` / `+init=epsg:4326`; bare `epsg:3857` and
  proj4 `+proj=merc...` FAIL ("without proj4 support") on this PROJ6 build.
- **osm2pgsql --append vs `statement_timeout`:** the tile-server caps query time, which cancels
  the append's COPY/index -> set `PGOPTIONS='-c statement_timeout=0'`. Appending is also slow
  (flat-nodes random I/O, ~2.6k nodes/s).
- **Long host jobs get REAPED** if launched as `&`-children of a tracked bg task or left idle as
  Bash bg tasks -> use detached `Start-Process` / `docker exec -d` for anything that must
  outlive the turn (`docker exec -d` needs `-u renderer` + a non-root-owned log).
- **Trail corridors z14-15 NOT renderable from this DB:** openstreetmap-carto doesn't
  materialize `route=hiking` relations as lines (only the underlying path ways). Deferred.

### 12.6 This session's commits (branch channel-key-passphrase, fork)
```
595334e map: fix use-after-free crash on fast pan at deep zoom
41ced7c field guide: rewrite medical/survival articles for self-reliance + verify credibility
b318050 field guide: caption each photo with its species in the one-at-a-time viewer
68cd54c field guide expansion (Near-me regional view, Animals, Medicinal, seasonal notes)
```
Non-git assets (on the Hive only): `C:\osm-tiles\work` (render toolchain + `ne/` Natural Earth
setup), `C:\osm-tiles\sd-staging` (the card image), `tools/offline_content/photos_raw`
(source photos; gitignored). Deep history: Claude Code memory `tdisplay-p4-map-tiles` +
`tdisplay-p4-offline-reference`.
