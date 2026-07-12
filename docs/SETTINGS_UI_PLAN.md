# Plan: Standardized Wi-Fi + Bluetooth settings (T-Display-P4)
_Locked via grill — by Claude + Mikhail (Z). Revised after Codex adversarial review (round 1)._

## Goal
Make Settings manage the C6 wireless features like a standard device: a **Wi-Fi
page** that scans and lists nearby networks (signal, secured icon, connected
marker), joins by typing a password on an **on-screen keyboard**, remembers
multiple networks (auto-reconnect + forget), and shows live status
(connecting/connected/IP/mapped errors); and a **Bluetooth page** managing the
device as the BLE *peripheral* it is (pairing PIN, connected/pairing status, real
BLE on/off, per-profile enables, PIN rotation that truly re-pairs). BLE *central*
(discover + connect out) stays out of scope.

**Reality check from the review: this needs C6 firmware changes, not just P4 UI.**
Runtime radio on/off, per-profile enable, and bond reset are firmware-level; they
bundle into the C6 reflash already scheduled for the lossless queue. The work is
sequenced firmware → protocol → P4 state/storage → UI so the UI never rests on
unverified behavior.

Target: T-Display-P4 (touch/pointer only). Other boards (T-Deck physical
keyboard) must be unaffected.

## Approach

### Phase 0 — C6 firmware + HostLink protocol (bundle into the C6 reflash; implement, compile-verify, then hardware-test before any P4 UI)
1. **Narrow control commands instead of whole-config replay.** Replaying
   `default_config()` resets unrelated radios (`c6_companion_runtime.cpp:982-991`)
   and the dispatcher *skips disabled services*
   (`tm_hostlink.c:229-255`). Add targeted, idempotent control frames:
   - Wi-Fi: extend `tm_c6_wifi_command` with `STA_ENABLE`/`STA_DISABLE` (reuse the
     existing `WIFI_CONTROL` 0x40 frame).
   - BLE: activate the reserved `BLE_CONTROL` (0x23) frame with a command enum:
     `BLE_ENABLE`, `BLE_DISABLE`, `SET_PROFILE_MASK(meshtastic/meshcore/trailmate)`,
     `SET_PIN(pin)`, `BOND_RESET`, `DISCONNECT`.
   Each maps to a real firmware action (below). This avoids the retained-full-
   config problem entirely for toggles.
2. **Make disable actually disable.**
   - `tm_wifi` STA_DISABLE: `esp_wifi_disconnect()` + drop STA mode, coordinated
     with ESP-NOW ownership (see 4). Emit `STA_DISCONNECTED`.
   - `tm_ble` BLE_DISABLE: `ble_gap_terminate(s_conn_handle)` if connected + stop
     advertising + reject data paths while disabled; emit DISCONNECTED/STOPPED.
   - Fix the dispatcher so a service's `*_apply_config`/control runs on the
     disable transition (don't skip disabled services).
3. **BLE bond reset for PIN rotation.** `BOND_RESET` → `ble_store_clear()` (or
   `ble_store_util_delete_peer` for the active peer) + `ble_gap_terminate`, so a
   rotated PIN forces genuine re-pairing (today a bonded phone reconnects with
   stored keys and never hits `REPEAT_PAIRING`). Do PIN change + bond reset on the
   NimBLE host task, after disconnect, clearing active-passkey state.
4. **Radio coexistence contract.** Wi-Fi STA and ESP-NOW share the C6 radio.
   Define who owns mode/channel; STA_DISABLE must not kill ESP-NOW team discovery,
   and STA associate/scan must tolerate active ESP-NOW. Bench-test STA
   enable/disable/scan/associate with ESP-NOW traffic live.
5. **Config-report correlation.** Every control/config carries an incrementing
   `config_seq`; the C6 echoes it in `CONFIG_REPORT` with accepted/rejected. Lets
   the P4 show pending→accepted per toggle and reject stale reports.
6. **Scan capacity:** keep **6** results this release (raising to 16 changes the
   exact-`sizeof` `tm_c6_wifi_event_t` wire struct on both ends — a versioned/
   paginated message is a separate task). Add `static_assert` on both builds that
   every 6-entry bound derives from one protocol constant.
7. **On-the-wire `op_id`.** Add an echoed `op_id` (uint16) to
   `tm_c6_wifi_control_t` AND `tm_c6_wifi_event_t` so the C6 stamps every async
   reply — SCAN_DONE, STA_CONNECTED, STA_GOT_IP, STA_DISCONNECTED, ERROR — with
   the id of the command that caused it. Without this the P4 cannot tell a late
   event of operation A from operation B (the Wi-Fi state machine + auto-join
   controller in Phase 2); P4-only generations are not enough. The BLE control
   frames carry the same `config_seq` for correlation.
8. **Protocol version bump (mismatch safety).** These changes alter wire payloads
   and add commands while P4 and C6 are flashed independently. Bump
   `TM_C6_PROTO_MAX` (and `MIN` for the breaking payload) so HELLO negotiation
   rejects an incompatible peer *before* any new control is sent; a new P4 must
   not drive an old C6 (and vice versa). This turns a silent size-mismatch into a
   loud, diagnosable handshake failure. `sizeof` static-asserts back it up.

### Phase 1 — P4 companion glue (`c6_companion_runtime.cpp`, `c6_companion.h`)
7. **Bounded-batch poll.** `poll()` drains one frame today (`:576-591`). Drain a
   bounded batch / time budget per main-loop tick so scan+connect+event bursts do
   not backlog. The UI never polls; it reads snapshots.
8. **Typed control API** on `WirelessCompanion`: `setWifiEnabled(bool)`,
   `setBleEnabled(bool)`, `setBleProfiles(mask)`, `setBlePin(pin)`,
   `bleBondReset()`, `bleDisconnect()` — each sends the Phase-0 frame with a fresh
   `config_seq`; expose the last accepted/pending seq in `status()`.
9. **PIN ownership stays on the P4** (NVS `tm_c6/ble_pin`); rotation = new random
   PIN → `setBlePin` + `bleBondReset`.

### Phase 2 — P4 Wi-Fi runtime: a correlated state machine (`platform_ui_wifi_runtime.cpp`, `wifi_runtime.h`)
10. **Operation generations (using the on-wire `op_id` from Phase 0).** A
    monotonically increasing `op_id` stamped on each connect/scan/disconnect and
    echoed by the C6 in every async event. `c6_wifi_ingest_event` ignores events
    whose `op_id` is stale (fixes: late disconnect/error from network A clobbers
    attempt to B; late scan drives auto-join after Wi-Fi disabled).
11. **Split scan API:** `start_scan()` (kick + set scanning, publish send-failure
    as an error, do NOT reuse the send-return as "results present") vs
    `get_scan_results()` (read cache). The UI timer calls `get_scan_results()`,
    never `start_scan()`.
12. **Distinguish transitions:** user-disconnect vs radio-disable vs forget vs
    auth-failure vs link-loss. Preserve the C6 disconnect reason + attempted SSID;
    define stable HostLink Wi-Fi error enums (Phase 0) and map wrong-password /
    not-found / timeout only after correlating to the active attempt. User-
    initiated disconnect/disable/forget suppress auto-join.
13. **Saved-networks store:** one **versioned NVS blob** (schema version + bounded
    record array `{ssid, security, password, auto_join}`), written transactionally
    (single blob, or shadow-slot + committed generation), with migration from the
    legacy `wifi_ssid/wifi_password` pair, a duplicate-SSID/security policy, and
    deterministic eviction at capacity. API: `list/save/forget/set_auto_join/
    connect_saved`. A newly typed password stays **transient** until auth succeeds,
    then is committed (never overwrite a known-good credential with a typo).
14. **Single-flight auto-join controller:** one in-flight attempt, manual-connect
    priority, cooldown/backoff, generation-checked; triggered on boot + on link-
    loss + on scan surfacing a saved auto-join AP. Never on user disconnect/disable.
15. **Security enum** on `ScanResult`/saved records preserving the C6/ESP-IDF
    authmode values; label + disable join for modes the device can't do
    (enterprise/OWE) rather than treating `authmode!=0` as "just needs a password".

### Phase 3 — P4 UI (`settings_page_components.cpp` + new modals)
16. **On-screen keyboard, correctly gated.** Gate on **actual pointer/touch input
    capability** (a real P4 feature flag / indev query), NOT `use_touch_first_
    settings_mode()` (that predicate is T-Deck-only and inverted for P4). On the
    pointer-only P4: build a P4-specific full-height flex sheet, add `lv_keyboard`
    bound to the textarea, and keep the keyboard **out of the encoder/nav group**
    (pointer devices don't need group traversal; this avoids trapping the two-pane
    D-pad nav on other builds). On teardown, detach indev/keyboard focus BEFORE
    deleting modal objects, then restore the prior group. Measure the keyboard for
    sizing; keep Save/Cancel/Show-Hide reachable above it.
17. **Live refresh without row rebuilds.** The status `lv_timer` updates stable
    labels in place (`refresh_visible_item_values`) and only does a generation-
    checked `build_item_list()` when no modal/callback is active (visibility
    actually changed), then `on_ui_refreshed()`. Never invalidate an open modal /
    focused object / `ItemWidget*` mid-callback.
18. **Wi-Fi page:** on/off (real, via `setWifiEnabled`), connected card
    (SSID/signal/security/IP + Disconnect + Forget), live nearby list (bars glyph,
    lock icon, connected check, sorted signal + connected-first), tap → open:
    connect / saved: connect stored / new-secured: password sheet → connect;
    Saved-networks sub-page (Forget + auto-join per network); mapped error text +
    inline re-enter-password on auth failure.
19. **Bluetooth page:** on/off (real, via `setBleEnabled`), Pairing PIN (Info +
    Rotate action = `setBlePin`+`bleBondReset`, with a "you'll need to re-pair"
    confirm; reject/queue while pairing), Status (connected/advertising/pairing),
    per-profile toggles (`setBleProfiles`). Move the cosmetic Network `ble_enabled`
    row here.

## Key decisions & tradeoffs
- **Narrow control commands, not whole-config replay** — avoids resetting
  unrelated radios and the disabled-service-skip bug; small protocol surface.
- **C6 firmware is in scope**, bundled into the reflash with the lossless queue.
  Accepts that the full feature needs a C6 flash (already planned).
- **Keyboard out of the nav group on pointer-only P4**; gate on real input
  capability. Avoids D-pad traps; T-Deck path untouched.
- **Transient-until-authenticated credentials** + transactional saved-networks
  blob — no known-good credential lost to a typo or a torn write.
- **Keep 6 scan results** this release; a paginated scan message is future work.
- **Correctness via op-generations + config_seq**, kept lightweight (a single
  current-op id and a last-accepted-seq), not a heavy transaction framework.

## Risks / open questions
- **C6 reflash is required** and needs the physical CNC1/UART dance (Z's hands).
  Firmware changes are compile-verified + bench-tested after the flash; the P4 UI
  lands only once the C6 side is confirmed.
- **ESP-NOW ↔ Wi-Fi STA coexistence** is the highest firmware risk (channel/mode
  ownership); must bench-test enable/disable/scan/associate with ESP-NOW live.
- **BLE_DISABLE terminating an active connection** and **BOND_RESET during
  pairing** must be serialized on the NimBLE host task to avoid callback races.
- Portrait keyboard real-estate + safe areas (540×1168) — validated on-device.
- UI is only fully verifiable by Z's eyes/hands (pairing, typing); automated
  checks = build + boot-clean + serial round-trip logs (reconfigure accepted,
  scan populated, connect event, disable actually silences the radio).

## Out of scope
- BLE central: discovering / connecting out to nearby BLE devices; pairing as
  initiator.
- 16-result scan (wire-struct/paginated message) — future.
- Non-P4 input paths (T-Deck keyboard unchanged).
- Multi-AP bonding beyond one in-flight attempt; enterprise/OWE join.
