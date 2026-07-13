# C6 companion: BLE + Wi-Fi + pairing — status & morning steps

Overnight work to get the ESP32-C6 companion "working on all counts": BLE
Meshtastic sync, Wi-Fi management, and a real pairing PIN. Branch
`channel-key-passphrase`, commits on top of `77cc0e0`.

## What is done and verified (P4 side, already flashed to Unit A / COM6)

- **BLE config sync is reliable.** The P4 paces Meshtastic downlinks to the C6
  at ~110 ms and holds-and-retries any frame whose SDIO send fails. Verified
  **5/5** `want_config` syncs completing (`config_complete_id` received) with a
  PC BLE test harness. This fixes the "too many retries / connected one node
  then disconnected" loop.
- **Pairing is a real per-device PIN.** The P4 generates a random 6-digit PIN
  once, persists it in NVS (stable across reboots), and configures the C6 for
  fixed-PIN pairing. **Current PIN: `&lt;per-device; shown on the on-screen pairing popup, persisted in NVS ns `tm_c6` key `ble_pin`&gt;`.** It shows on screen (BLE pairing
  popup) while a phone is pairing, and the connected state drives the existing
  "BLE linked" status. The temporary no-PIN debug mode is reverted; verified the
  C6 now rejects an unpaired write with `Insufficient Authentication`.
- **Wi-Fi works.** STA is enabled on the C6 (`enabled=0x1f` = BLE×3 + ESP-NOW +
  Wi-Fi). Scan verified end-to-end (6 networks with SSID/RSSI/channel/auth). The
  settings page now reports `wifi_supported=1`; scan/connect/disconnect/status
  are wired through the C6 over HostLink. BLE + ESP-NOW + Wi-Fi coexist (BLE sync
  still 2/2 with Wi-Fi enabled).
- Codex adversarially reviewed the whole change; its material findings were
  fixed (see the commit + `docs/`), then re-reviewed.

## The one remaining step: flash the new C6 firmware (recommended, ~3 min)

The P4-side pacing already makes sync *reliable*. For **fully lossless** delivery
(no dropped config frames even with a slow phone) the C6 needs its new FromRadio
FIFO queue. That is a C6 flash — the C6 cannot be reflashed from the P4.

New C6 binary (already built):
`firmware/c6_companion/build.c6_companion/trail-mate-c6-companion.bin`

### Wiring + download mode (from prior C6 bring-up)
- CNC1 connector (ZX-SH1.0 4-pin): pin1 GND, pin3 = C6_U0TXD, pin4 = C6_U0RXD,
  via the CH340 (COM11).
- Enter download mode: hold the C6 **BOOT** firmly, tap **RESET**, then freeze
  ("one-tap-then-freeze").
- **Use 115200 baud** (460800 corrupts the C6 flash).

### Flash (app only — bootloader/partition are unchanged)
```
cd firmware/c6_companion
python -m esptool --chip esp32c6 -p COM11 -b 115200 \
  --before default_reset --after hard_reset \
  write_flash 0x10000 build.c6_companion/trail-mate-c6-companion.bin
```
If esptool can't sync, re-do the BOOT/RESET dance and retry. A backup of the
currently-running C6 image can be read first with
`esptool --chip esp32c6 -p COM11 -b 115200 read_flash 0x0 0x400000 c6_backup.bin`.

### Verify after flashing
1. Power-cycle; on the P4 UART (COM6) confirm `C6 present ... enabled=0x1f`.
2. Pair a phone in the Meshtastic app using PIN **&lt;per-device; shown on the on-screen pairing popup, persisted in NVS ns `tm_c6` key `ble_pin`&gt;**; the app should reach
   the node and complete config with no "retries" churn.
3. (Optional) In the app, confirm all channels/config load (lossless).

## Codex review outcome

Two adversarial passes. All critical/high correctness findings were fixed and
confirmed clean on re-review: the FromRadio queue (portMUX critical section, no
deadlock, no head-overwrite between peek and drop), the frame-loss on mbuf
exhaustion, the FromNum desync, the blocking scan that stalled BLE, the Wi-Fi
event/BLE-sink coupling, and the SSID/password NUL-termination. Two items are
accepted as-is (not correctness bugs), see below.

## Notes / known limitations
- Wi-Fi management is driven by the C6 companion poll that runs from the BLE
  service; with BLE **enabled** (the default) this is a non-issue. If BLE is ever
  turned off, Wi-Fi status/scan pause until it is re-enabled.
- Wi-Fi scan in the settings page is asynchronous: tap Scan, the list populates a
  few seconds later (status shows "Scanning..."); tap again to see them. This is
  intentional -- a blocking scan would freeze the UI ~5s and stall BLE. A future
  polish is to rebuild the list from the ScanDone event so the first tap suffices.
- FromRadio delivery: if the FromNum notify for the *last* frame transiently fails
  (rare mbuf exhaustion, only while paired+connected), the phone re-requests config
  on its want_config timeout and recovers; there is no explicit notify retry.
- Trail-Mate BLE profile is still a placeholder (accepted, not routed).
