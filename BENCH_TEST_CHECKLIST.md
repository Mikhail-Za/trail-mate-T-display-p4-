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

### If pinch is erratic or dead
The Hi8561 two-finger read is the one piece never run on hardware (the known risk in
MAP_UX_ROADMAP.md sec.1). Single-touch is unaffected by design, and double-tap /
two-finger-tap still give full zoom control. For diagnosis, capture serial while
pinching: the firmware prints `[GPS][MAP][touch] pinch_begin / pinch_in / pinch_out /
two_finger_tap / double_tap` lines unconditionally.
- `pinch_begin` never appears -> the controller is not reporting 2 fingers (read_hi8561
  multi-read layout is the suspect).
- `pinch_begin` but no steps -> spread hysteresis never crossed (finger geometry noise).

## 4. New apps: Translate + Field Guide (either unit, needs the SD card)
- [ ] **Field Guide**: launcher shows the new icon; categories list (Survival, Water &
      First Aid, Edible Plants, Plant Hazards, Wildlife & Nature, Preparedness) with
      counts; open an article: title + scrollable body render, Back walks list ->
      categories -> launcher.
- [ ] **Field Guide plant photos** (JPEG decoder path): open an Edible Plants article
      (e.g. Cattail) -> 2-3 real photos render inline above the text, sharp, full
      screen width. Open a Plant Hazards article (e.g. poison ivy) -> its photos show.
      Photo attributions are on the card at guides/photos/CREDITS.tsv. Text-only
      sections (Survival, Preparedness, etc.) have no photos by design.
- [ ] **Translate, forward**: pick Spanish -> categories appear + native name
      "Español" renders with accents; open Emergency -> "Help!" -> card shows
      "¡Auxilio!" LARGE. Latin fonts prove the SD binfont path.
- [ ] **Translate, non-Latin**: pick Chinese -> card shows 救命！ with pinyin line
      under it (both from the SD font). Then Arabic -> text renders SHAPED
      (connected letters) and right-to-left. Arabic is the riskiest renderer path
      (BIDI + presentation forms via the DejaVu subset); if it shows disconnected
      letterforms, capture a photo.
- [ ] **Translate, hand-over**: "Hand device to them" -> flat list in the target
      language with the tap-a-phrase hint on top; tapping a phrase shows its English
      big. Back returns through list -> categories -> languages.
- [ ] **No-SD fallback** (optional): eject SD, open both apps -> clean "no data
      found" message, no crash.

## 5. Still pending from earlier arcs (not this session)
- Walkie-talkie human voice bench test (PTT build, 06-25).
- Unit A GPS: hardware fault; multimeter the L76K VCC rail before any firmware theories.
