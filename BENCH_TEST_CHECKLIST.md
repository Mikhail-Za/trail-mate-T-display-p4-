# Flash-day bench test (for the 2026-07-01 review fixes + gesture build)

Everything below was compile-verified only; the devices were away. Flash both units,
then run the checks in order. Total time ~10-15 minutes.

## 0a. Load the SD content onto both cards (card reader, per card)
The staging tree now carries THREE content sets: map tiles (incl. z13-14 for all
CONUS National Parks + NPS units; 970,584 tiles, ~31GB on-card, ~28GB free),
`translate/` (12-language phrasebook + subset fonts, ~330KB) and `guides/`
(54-article survival/foraging/nature handbook, ~250KB). One robocopy of the
staging ROOT covers all of it; only new files copy:
```powershell
robocopy "C:\osm-tiles\sd-staging" "E:\" /E /NFL /NDL /NJH /R:1 /W:1 /MT:8
```
(adjust E: per card). The Settings coverage string already mentions the parks
(updated in-repo), so no code edit is needed on flash day.

## 0b. Flash
```powershell
C:\Users\zaidm\deploy-both-units.ps1
```
MAC-guarded, app-only @0xBC0000, boot-verifies both units over serial. Expect
`RESULT: PASS (both units flashed AND booted clean, no panic)`.

## 1. Review-fix spot checks (any unit)
- [ ] **Modal titles**: open Map -> Layer, Route, and Track. Each modal shows its title
      bar at the top with the content list BELOW it (previously the content area was
      sized over the title). 30 seconds.
- [ ] **Layer panel behavior**: rows show a checkmark (OSM) / X (Terrain, Satellite,
      Contour on the shipped cards). X rows are greyed and DEAD to taps. Tapping
      between rows feels instant (the per-tap SD re-probe is gone).
- [ ] **Zoom panel**: rows read "Level N" with marks. If you ever set a non-English
      locale: rows must be translated (they silently fell back to English before).
- [ ] **No-coverage hint**: pan far off the tiled area (into the ocean). After ~1.2s a
      grey "No offline tiles here" appears bottom-center; pan back and it clears once
      tiles decode.

## 2. Last-known-position persistence (Unit B, the working GPS)
- [ ] Get a fix outdoors, watch the blue pin. Power-cycle the unit indoors (no fix).
      Open the map: grey hollow ring at the last position on the zoomed-out view,
      with a small age tag under it ("12m" etc.). This never worked as shipped;
      it is the review's headline fix.
- [ ] Leave the GPS page, re-enter it (still no fix): the ring must STILL show
      (the old code lost it for the whole boot after one page exit).

## 3. Gestures (the new build; any unit, no GPS needed)
- [ ] **Pinch out** (spread two fingers on the map): zooms IN one level per pull;
      map recenters on the screen center. **Pinch in**: zooms OUT.
- [ ] **Two-finger tap** (brief, no spread): zoom OUT one level.
- [ ] **Double-tap**: zoom IN one level.
- [ ] **Pan is unaffected**: single-finger drag pans exactly as before; putting a
      second finger down mid-drag cancels the pan into a pinch; lifting one finger
      after a pinch does NOT yank the map (cooldown swallows the survivor).
- [ ] Buttons (Zoom/Layer/Route/[P]) still respond normally to taps.

- [ ] **Single-touch is now the proven path everywhere.** After the 2026-07-04 review,
      the wider 2-finger read is only ON while the Map screen is open; every other screen
      (menu, chat, flashlight, etc.) uses the original single-point read. Sanity-check
      that plain tapping still works app-wide -- if ANY screen's touch is dead, that is
      the regression to report (should be impossible by design now).

### If pinch is erratic or dead
The Hi8561 two-finger read is the one piece never run on hardware (the known risk in
MAP_UX_ROADMAP.md sec.1). Single-touch is unaffected by design (it uses the proven
5-byte read; the multi-read only runs on the map), and double-tap / two-finger-tap still
give full zoom control. The per-step gesture traces are now behind GPS_DEBUG, so to
diagnose, set `#define GPS_DEBUG 1` in gps_page_input.cpp + trail_mate_t_display_p4_runtime
is unaffected, rebuild, and watch for `[GPS][MAP][touch] pinch_begin / pinch_in /
pinch_out / two_finger_tap / double_tap`.
- `pinch_begin` never appears -> the controller is not reporting 2 fingers (read_hi8561
  multi-read layout / finger-count byte is the suspect; the multi-read is gated behind
  trail_mate_t_display_p4_set_multitouch, called by the map on entry).
- `pinch_begin` but no steps -> spread hysteresis never crossed (finger geometry noise).
- Gestures now debounce single-sample noise (need 2 consecutive polls to engage/end), so
  a brief ghost touch should NOT cancel a pan or flip a zoom; if it still does, note it.

## 4. New apps: Translate + Field Guide (either unit, needs the SD card)
- [ ] **Field Guide**: launcher shows the new icon; categories list (Survival, Water &
      First Aid, Edible Plants, Plant Hazards, Wildlife & Nature, Preparedness) with
      counts; open an article: title + scrollable body render, Back walks list ->
      categories -> launcher.
- [ ] **Field Guide plant photos** (JPEG decoder path): open an Edible Plants article
      (e.g. Cattail) -> 2-3 real photos render inline above the text, sharp, full
      screen width, with a small grey "Photos: <artist> (<license>) / Wikimedia Commons"
      attribution line under them (required by the CC-BY licenses). Open a Plant Hazards
      article (e.g. poison ivy) -> its photos show. Article body text has NO stray boxes
      at line ends (the CRLF->tofu fix). Text-only sections have no photos by design.
- [ ] **Photo scrolling** (known perf caveat): scrolling a photo-heavy article may
      stutter because LVGL's image cache is OFF (CONFIG_LV_CACHE_DEF_SIZE=0), so photos
      re-decode on repaint. Deferred, not applied blind: if the jank is bad, the fix is
      to set an image cache (~2-4MB, PSRAM) and re-verify map memory. Note the severity.
- [ ] **Translate, forward**: pick Spanish -> categories appear + native name
      "Español" renders with accents; open Emergency -> "Help!" -> card shows
      "¡Auxilio!" LARGE. Latin fonts prove the SD binfont path.
- [ ] **Translate, non-Latin**: pick Chinese -> card shows 救命！ with pinyin line
      under it (both from the SD font). Then Arabic -> text renders SHAPED
      (connected letters) and right-to-left. Arabic is the riskiest renderer path
      (BIDI + presentation forms via the DejaVu subset); if it shows disconnected
      letterforms, capture a photo.
- [ ] **Translate, font-missing warning**: rename one SD font (e.g.
      translate/fonts/zh30.bin) and pick that language -> an orange warning appears
      ("Font ... missing ... Romanization still works"), not silent blank boxes. Restore
      the file after.
- [ ] **Translate, hand-over**: "Hand device to them" -> flat list in the target
      language with the tap-a-phrase hint on top; tapping a phrase shows its English
      big. Back returns through list -> categories -> languages.
- [ ] **No-SD fallback** (optional): eject SD, open both apps -> clean "no data
      found" message, no crash.

## 5. Still pending from earlier arcs (not this session)
- Walkie-talkie human voice bench test (PTT build, 06-25).
- Unit A GPS: hardware fault; multimeter the L76K VCC rail before any firmware theories.

## 6. Six new field-toolkit apps (2026-07-05; compile-verified tft+amoled, on-device pending)
All six appear in the launcher after &g_field_guide_app. Built off a scouted spec,
then adversarially reviewed (11 fixes applied, Beacon was clean). Use the working-GPS
unit (B) for the GPS-dependent ones.

- [ ] **Waypoints** (icon: map pin): with a fix, tap "Save current location" -> a row
      appears with live distance + bearing. Tap it -> Go-To view shows big bearing/
      distance updating as you move. Power-cycle -> the waypoint reloads (persisted to
      A:/waypoints.tsv). Delete a row -> file rewrites (save 5, delete 4, power-cycle,
      exactly 1 reloads). Pull the SD while on the list -> Save disables + warning; reseat
      it -> Save re-enables and the list reloads within ~1s (no exit/re-enter needed).
- [ ] **Compass** (icon: gps): stand still -> needle hidden, amber "Move to get a heading".
      Walk -> north-up dial shows a needle at your course + "043deg NE" + speed. HONEST
      LIMIT: this is GPS-course, not magnetic (no magnetometer driver on this board), so
      it only works while moving. Lose the fix mid-walk (canopy) -> needle blanks
      immediately to "No GPS fix", never freezes on a stale bearing. No flicker at a slow
      hovering pace (0.3-0.5 m/s hysteresis band).
- [ ] **Sun & Moon** (icon: gear): shows local date, Sunrise/Solar noon/Sunset/Day length,
      Moon phase + illumination %, and the position used. Cross-check sunrise/sunset vs a
      known almanac value for your location/date (algorithm was validated against Rochester
      MN). No fix -> "Need a GPS position first". Clock not RTC-synced -> "Clock not set yet".
      A position older than 24h -> "open Map/GPS for a fresh fix"; a stale-but-recent fix
      shows "(last known Xh ago)".
- [ ] **Emergency** (icon: SOS): tap ACTIVATE -> screen strobes SOS Morse (... --- ...) at
      full brightness, an audible SOS tone plays (toggle Sound off/on), and it broadcasts
      "SOS <lat>,<lng>" over the LoRa mesh every 30s (watch a second node's chat on PRIMARY
      to confirm receipt; sends silently skip if Walkie holds the radio). "sent N" counter
      climbs. DEACTIVATE / Back -> strobe stops, tone stops, brightness restored, screen-
      sleep re-enabled. Leave it armed and hit Back -> it still force-stops everything.
- [ ] **Trip** (icon: tracker): walk a known loop -> Distance (m/km), Speed, Max, Avg
      (moving), Moving time, Altitude, Ascent all populate (km/h + mph). Stand still ->
      Speed reads 0 (no phantom jitter), Distance doesn't creep. Reset zeroes everything.
      Back and reopen quickly while walking -> the closed gap is NOT folded in (no phantom
      jump). Note: stats accumulate only while the app is open (v1).
- [ ] **Battery** (icon: gear): shows percent + bar, charging state, power tier, and (P4
      gauge) numeric voltage + signed current. Plug/unplug USB -> "Charging"/"Discharging"
      flip and current sign changes. If the gauge can't be read: "--%" + "Unknown" +
      "Current: n/a" (never a fake "0 mA / Discharging"). Leave it open a while on a healthy
      gauge -> no I2C/touch stutter (dead-gauge re-probe is throttled to 5s).
