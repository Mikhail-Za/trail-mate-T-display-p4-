# Trail Mate engineering and security audit — 2026-09-07

**Current nonce continuation:** Native Meshtastic persistent allocation and fail-closed channel loading are integrated; all three host harnesses and TFT/AMOLED builds pass. Final Fable review approved in two rounds; see NONCE-STATUS.md. Team defects and physical bench checks remain open. Current routing uses GPT workers and Anthropic review; Spark is untouched.

**Photo continuation (historical):** Astra completed the photo-viewer metadata gate after explicit takeover from failed Spark task S3. Runtime regression and mutation checks pass; both TFT and AMOLED IDF5.5.4 firmware builds now pass using a rootless Podman build environment. Simulator remains71/71PASS. SD photo mapping is also repaired by Astra after S4 timed out without edits; its ten regression tests pass. Team-key and nonce repairs remain open. See PHOTO-REVIEW.md, PHOTO-STAGING-REVIEW.md and SECURITY-IMPLEMENTATION-PLAN.md. Earlier toolchain-unavailable statements below are historical. Nothing flashed.

**Earlier photo/NMEA review:** Astra under the then-current user override. The nonce continuation uses a separate Fable plan and completed-work gate, recorded in NONCE-CRUCIBLE-REVIEW.md.

## Executive assessment

The audit found a reproducible Team-key replacement defect, a safety-critical missing-caption failure mode, and a broken Linux smoke-test build. Applied fixes: the Linux build dependency, read-only CI token defaults, and Spark-authored NMEA coordinate validation. The integrated suite now passes71/71tests, including50new coordinate cases. Photo viewer and SD photo/credit mapping repairs are applied and tested. High-priority Team remediation remains open; nonce code is verified and independently approved, with hardware validation pending in NONCE-STATUS.md. GitNexus is operational and both TFT/AMOLED firmware builds pass; on-device verification remains outstanding.

This is a risk-based engineering audit with recorded coverage and gaps, not a certification, exhaustive review of every line, penetration test of the radios, or assertion that all bugs are fixed. The scope includes security, reliability/data loss, SD content, UI/map behavior, build/test/release controls, dependency updates and optimization recommendations. External boot launcher, RF certification, medical accuracy and exhaustive vendor-code review are outside this pass.

## Baseline and method

- User-selected repository: `/home/zaidm/tdisplay-p4-dualmesh/trail-mate`.
- Baseline: `e3c0fbd68663bbeef09056a22cbce950065304bc`, branch **main**, initially clean; it descends from maintainer commit `98609d5`. Older references to `channel-key-passphrase` and Unit A's GPS being permanently dead are stale; MAINTAINERS §12 reports both GPS units working as of July 11. Current hardware state was not independently checked.
- Astra performed Conductor planning, verification and synthesis. Three parallel **claude-sonnet-4-6** workers reviewed security, runtime and delivery; each explicitly loaded Ponytail. One targeted follow-up each supplied corrections and additional evidence; Astra rejected remaining unsupported claims. No GPT workers or automatic model substitution. Native Claude subscription authentication was used without provider/billing changes.
- Crucible planning: **claude-fable-5-1**, two rounds, REVISE then APPROVED; five-round cap and ten-minute ceiling per call. Independent final-state review has NOT run. See `review-log.md` for the completed planning gate.
- Evidence levels: **host reproduced** means actual project code was compiled/executed with described host fixtures; **code traced** means source/production wiring inspected; **candidate** means reachability or dependency behavior is unverified. No hardware reproduction is claimed.
- `verification.md` contains exact checks and limitations. Raw worker reports are evidence drafts, not authoritative conclusions: the dispositions below supersede incorrect worker claims.

## Prioritized findings

| ID | Priority | Finding | Evidence | Disposition |
|---|---|---|---|---|
| TM-01 | High | Plaintext foreign-team KeyDist replaces existing service keys | Host reproduced + code traced | Open; authorization fix required |
| TM-02 | High | Missing photo credits silently remove safety-critical species label while image remains visible | Host regression + TFT/AMOLED builds | Applied; viewer gate and staging mapping repaired; device/card checks pending |
| TM-03 | High, mode-dependent | Legacy pairing carries raw PSK; pairing source metadata is not authentication | Code traced | Open; secure pairing design/compatibility required |
| TM-04 | Medium | Linux phone-core smoke executable omits channel_hash.cpp | Failed-before / passed-after host build | Applied; original69tests pass |
| TM-05 | Medium recommendation | ESP-IDF5.5.4 patch migration and advisory applicability review | Lock + official upstream data | Evaluate5.5.5; no blind6.x upgrade |
| TM-06 | Low/Medium | Map title uses panned map center while a live GPS fix exists | Code traced; UX interpretation explicit | Label center or display live fix consistently |
| TM-07 | Low/Medium | Mutable CI tools/actions and incomplete dependency provenance | Source inspection | Token defaults applied; version/action pins remain recommended |

### TM-01 — Plaintext KeyDist overwrites already-established keys

**Source:** `modules/core_team/src/usecase/team_service.cpp:321`, `:447`, `:461`; event sink at `platform/esp/idf_common/src/team/idf_team_event_sinks.cpp:26`.

`processIncoming()` first tries a Team encrypted envelope and falls back to plaintext Team management decoding. Unlike Kick, TransferLeader and KeyRequest, KeyDist has no `decoded_encrypted` gate. After dispatching the event it directly calls `setKeysFromPsk()` for nonempty PSK/nonzero key ID, without restricting this to an authorized pairing transaction or checking the currently established team. An event handler rejecting a UI update cannot veto that direct assignment.

The host demonstration initializes valid keys for one team, injects a plaintext KeyDist carrying another team ID and synthetic PSK, and confirms all four key derivations consume the injected PSK. It uses actual TeamService and protocol codecs, with existing fake mesh/crypto/runtime fixtures. This proves the service-layer control-flow bug; it does not demonstrate radio injection, real cryptography or device behavior. Radio exploitability depends on reaching TEAM_MGMT_APP through the configured mesh/channel. A public/default shared channel cannot be treated as sender authentication.

**Impact:** Unauthorized key replacement can desynchronize the victim from its team and potentially expose subsequent victim traffic under an attacker-known key. It does not reveal earlier ciphertext or replace every other member's keys automatically.

**Remediation:** Make key installation an explicit authorized action. Established-team updates must bind to the authorized team/leader/session and authenticated protocol state. Bootstrap must require an explicit pairing transaction and validated key establishment. Validate before emitting effects or mutating keys. Merely adding an encrypted-only guard can break legitimate bootstrap and is not a complete leader-authorization design; merely checking team ID or sender address is insufficient.

**Acceptance:** In the service-level reproduction, foreign/plaintext KeyDist triggers zero derivations and no key/UI mutation; malformed, unsolicited, cross-team, stale/replayed and unauthorized member updates are rejected. Legitimate authenticated rotation and both supported pairing flows still succeed; two-device pairing/reboot/rotation tests required. Artifact: `repro_plain_keydist.cpp` (demonstration assertions document current failure, not a passing security regression).

### TM-02 — Photo viewer fails open when species metadata is missing

**Source:** `apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp:627` and `:811`.

**Original defect (repaired; see PHOTO-REVIEW.md and PHOTO-STAGING-REVIEW.md):** `append_photo_caption()` silently returns if CREDITS.tsv cannot be read, there is no matching photo row, or the species field is empty. It returns no success status. `show_photo_viewer_screen()` calls it and then unconditionally creates/decodes the image while retaining the article title. This matters because the maintained content intentionally includes poisonous lookalikes in edible-plant photo sets. The normal red hazard-caption mechanism is present, but incomplete SD content can bypass it.

**Impact:** A lookalike photo can appear without the species/warning caption that distinguishes it from the article subject. This is a content-integrity failure, not a claim that any specific medical text or photo is incorrect.

**Remediation:** Return caption lookup success and withhold the image if required species metadata is missing/invalid; display an explicit metadata-missing message with a route back to the article. Validate every photo-to-credit mapping before staging. Preserve genuine lookalike warnings and attribution. Do not infer species from the article title.

**Acceptance:** Missing/unreadable/truncated credits, missing photo row and empty species all withhold the affected image. Valid ordinary and hazard rows display the exact corresponding caption; previous/next navigation cannot carry over a prior photo's caption. Verify actual on-device layouts using a copied test card.

### TM-03 — Pairing confidentiality and authentication depend on mode

**Source:** `modules/core_team/src/usecase/team_pairing_coordinator.cpp:285`, `:394`; `modules/core_team/src/protocol/team_pairing_wire.cpp:185`; `firmware/c6_companion/components/tm_espnow/tm_espnow.c:114`; `platform/esp/idf_common/src/team/idf_lora_pairing_service.cpp:155` and `:263`.

The generic pairing Key packet serializes the supplied PSK; ESP-NOW peers are configured without link encryption. The IDF LoRa wrapper explicitly warns that its no-passphrase mode sends the key in the clear. Its passphrase mode instead sends a sentinel and derives the real PSK locally, so a blanket assertion that all current P4 pairing always transmits the real key is incorrect.

The coordinator's `handleKey()` ignores source MAC and checks a team ID and join nonce that are not secrets on the unencrypted transport. This permits unwanted source acceptance at that layer. A source-MAC comparison is worthwhile filtering but does not authenticate spoofable frames. Unauthenticated ECDH alone cannot stop MITM; a short authentication string still requires authenticated key-establishment semantics and user verification. Do not invent a custom protocol or claim a one-line comparison solves this.

**Remediation:** Prefer the existing passphrase path where appropriate, make insecure mode visible or disable it by explicit product policy, and validate source/session binding. Design authenticated pairing/rotation using existing vetted primitives, version the interoperability change, and benchmark a standard password KDF against the device budget. No arbitrary KDF iteration count is endorsed without measurement. See TM-01: secure pairing does not cure a separate runtime KeyDist bypass.

**Acceptance:** Packet captures of secure mode do not contain real PSK material; wrong password, altered sender/session, replay, racing key responses and unsolicited packets do not establish a trusted team. Both devices agree on keys and recover correctly across reboot/rotation. No RF tests ran here.

### TM-04 — Verified Linux build repair

`apps/linux_sim_shell/CMakeLists.txt:209` includes `mt_protocol_helpers.cpp` in `trailmate_phone_core_smoke`, but omitted its existing `domain/channel_hash.cpp` dependency. The linker reports undefined `expandChannelPsk` and `computeChannelHashBytes` references. Adding that one source file resolves the link failure. This list entry does not edit a function/class/method.

Baseline: 68 tests pass, one cannot run. Isolated patched checkout: complete build and **69/69 tests pass**. `0001-link-channel-hash.patch` contains the exact repair. No broad source-list mirroring or speculative future dedup sources were added. No firmware correctness claim follows from a Linux-only build.

### TM-05 — Update recommendation based on official sources

`dependencies.lock:142` locks IDF5.5.4; `:149` locks LVGL9.5.0. Official IDF5.5.5 was published July17,2026 and is a sensible patch-level evaluation target. IDF6.1 is newer but a major migration is not necessary merely to address the5.5 maintenance branch. LVGL9.5.0 matches its latest upstream release at query time.

Public IDF advisories list5.5.4 for several components, including esp_driver_jpeg DQT parsing (GHSA-v6r2-f6p2-88cj), ESP-TEE, protocomm BLE, WebSocket server and DHCP parsing. Component/version correspondence must be checked against generated target configuration and actual linked paths. P4 defaults use **LVGL TJPGD**, so an ESP hardware-JPEG advisory is not automatically applicable to displayed photos. Classic Bluetooth advisories likewise must not be assigned to P4/C6 solely by version. No confirmed reachable third-party CVE is claimed by this pass.

Evaluate5.5.5 in isolated TFT/AMOLED builds, inspect regenerated sdkconfig for unintended drift, run the bench checklist and review all active advisories before rollout. Five official public API responses are archived; see `upstream-checks.md`. Empty public LVGL advisory results do not establish vulnerability absence.

### TM-06 — Map-coordinate meaning is ambiguous after pan

`gps_page_map.cpp:660` formats `g_gps_state.lat/lng` in the title. Those fields are the map center after pan/zoom; the GPS update code intentionally keeps the view center independent while updating last-known GPS coordinates. The title says Map rather than explicitly My location, so center coordinates may be deliberate UI behavior: this is a UX/safety recommendation rather than a proven algorithm defect without a product specification.

Label the coordinates Map center while panned, or use a clearly labeled live-position source if the title is meant to tell the user where they are. Test panning away from a fixed simulated location, GPS refresh, loss/reacquisition and re-centering. Re-centering should show the actual GPS location, not the previously panned destination.

## Rejected or qualified worker claims

- **P4 keys lost on every reboot:** rejected; the IDF NVS store saves/restores PSK separately from the shared UI snapshot codec. Do not add secret fields to a generic codec based on partial tracing.
- **Unsigned timer-wrap bug:** rejected; uint32_t elapsed subtraction correctly handles wrap for the bounded interval in question.
- **Public channel-hash helper overflow is a reachable remote vulnerability:** not established. The current radio caller normalizes key length and terminates the name. API-boundary validation is a hardening candidate, not a demonstrated exploit.
- **Increase dedup ring size to stop replay:** rejected as a security fix. Capacity tuning is performance/traffic engineering; replay resistance needs authenticated message freshness and bounded state.
- **XOR time and a counter gives unique nonces:** rejected; XOR can collide. Assess actual production ID generation, reboot behavior and protocol constraints before changing it.
- **Any missing IDF source in Linux must be added:** rejected. Only demonstrate and fix actual supported consumers; do not add future-only sources to manufacture parity.
- **Corrupt binfont definitely crashes and a magic check prevents it:** unverified without actual decoder inspection/fuzzing. Header-only validation cannot establish binary-parser safety.
- **Normal caption path implies fail-safe caption handling:** rejected; TM-02 documents the missing-metadata branch.
- **All LVGL operations are proven single-threaded / no races / sufficient stack:** broader than sampled evidence. Hardware/task-lifetime stress remains required.
- **Current walkie TX is blocking:** outdated for IDF; runtime calls `startTransmitAsync`. Voice quality and radio arbitration still need bench verification.

## Verification and remaining work

See `verification.md` for commands and results. Host checks cover the simulator's69 registered tests, not every firmware module. The TeamService reproduction is additional to that suite. ASan+UBSan configuration failed because the host ASan library is missing; it is not a sanitizer pass or a project compile failure. TFT/AMOLED, BLE, radio, touch/Arabic rendering and actual SD fault injection were not run.

Before further firmware changes: use the now-available GitNexus impact analysis, report callers/processes/risk for each symbol and warn on HIGH/CRITICAL; make bounded fixes and regression checks; build TFT (AMOLED when shared), review actual diffs under the current user-authorized Astra review process; do not invoke Anthropic while the user has selected local workers. GitNexus detect_changes remains mandatory before any commit. No commits, pushes, flashes, provider changes or SD writes occurred in this audit.

## Coverage and delivery addendum

The following additional findings and dispositions were saved at the user-requested provider pause. They remain subject to final Crucible review.


### Additional verified findings and qualified recommendations

**TM-08 — High, code-traced: Meshtastic packet counter restarts on reboot.** `platform/esp/radio/meshtastic_radio_adapter.h:86` initializes next_packet_id_ to1; sendText at.cpp:123 and sendEncodedPayload at:312 increment it. No other counter initialization was found. `initNodeIdentity():763` derives the stable node ID from the eFuse MAC. `mt_packet_wire.cpp:128` constructs AES-CTR nonce from packet ID and sender node ID. With the same private channel PSK and sender across restarts, reused IDs imply reused CTR keystream; equal-position ciphertext XOR reveals plaintext XOR. This is a source-traced encryption defect, not an on-air demonstration. It supersedes the worker's same-millisecond claim for this adapter, which actually uses a counter. Remediation must preserve nonce uniqueness across restarts and handle wrap/explicit IDs within Meshtastic compatibility constraints; neither timestamp XOR nor a small random seed guarantees uniqueness. Test two cold boots with a stable private key and verify the allocation scheme cannot reuse sender/packet nonce pairs. The native implementation now uses durable reservation, a serialized owner, fresh-key migration and fail-closed channel loading. Host checks and both firmware builds pass; independent Fable review passed; physical checks remain pending. See NONCE-STATUS.md.

**TM-09 — FIXED in core_gps, host verified: previously accepted nonfinite fix.** `modules/core_gps/src/protocol/nmea/nmea_parser.cpp:44` accepts strtod NAN/Inf and converts the result to int before checking bounds. A checksum-valid RMC with NAN coordinates publishes a fix with valid=true and nonfinite coordinates in the host reproduction. The float-to-int conversion is itself undefined for nonfinite/out-of-range inputs; checking isfinite only at the end is too late. Validate numeric syntax, finiteness and magnitude before conversion; validate minutes, hemisphere and geographical range. Spark’s accepted fix now rejects invalid coordinates before the cast and enforces hemisphere/minute/range checks; original+50newcases pass and both tests run in the71-test suite. Current P4 wiring to this core parser was NOT established (the IDF source list search showed its include directory but no nmea_parser.cpp), so do not claim a reproduced P4 crash. `repro_nmea_nonfinite.cpp` is a demonstration against actual parser/sentence code; after repair require rejection while preserving ordinary valid/checksum/fragment tests.

**TM-10 — Medium robustness: unbounded SD text reads.** `modules/ui_shared/include/ui/support/lvgl_fs_utils.h:143` appends until EOF with no byte budget. Guide indexes, region tags, credits and translation TSV callers use it. An oversized/corrupt local SD file can exhaust memory or stall the UI. A hardware crash was not reproduced. Add explicit file-class limits informed by real content sizes, fail cleanly before excess allocation, and ensure failure invokes TM-02's photo-withholding behavior. Test exact limit, limit+1, partial read errors and ordinary largest content. No arbitrary512KB/64KB limit is approved without checking content.

**TM-11 — Medium data-loss risk: waypoint saves overwrite the only file.** `apps/esp32_lvgl/src/esp32_lvgl_waypoints_app.cpp:179` pads to old length and writes in place. A power interruption can destroy previously valid entries. The parser checks that strtod consumes at least one character and rejects nonfinite/out-of-range values, so the worker's empty-field-to-zero example is incorrect; numeric prefixes with trailing garbage remain accepted. Use a tested recoverable save scheme with temporary file, checked writes/close/sync and retained previous copy or recovery logic supported by the actual VFS. Do not claim FAT rename alone is power-loss atomic. Hardware fault injection must demonstrate old or new complete data survives every interrupted stage.

**TM-12 — Low, local SD trust: guide index paths are not restricted to article descendants.** `field_guide_app.cpp:730` concatenates `A:/guides/` and SD-provided entry.path. If the VFS resolves dot segments, `../lastfix.dat` leaves guides; the worker's `../../` normalization example was wrong. Appending a prefix and checking that same raw prefix does not prevent traversal. Reject absolute paths, parent segments and invalid expected article paths before read; validate actual VFS behavior before claiming access outside the SD mount. No network exfiltration or system-file write was demonstrated.

### Delivery/security configuration repair prepared

`0002-readonly-ci-tokens.patch` adds only top-level `permissions: {contents: read}` to ci.yml, linux-simulator.yml, cardputer-zero-linux.yml and uconsole-linux.yml. The release job's explicit write permissions stay unchanged. PyYAML before/after comparison verified no other semantic changes; git diff --check passed. No hosted workflow run occurred. Both this repair and the CMake fix are now applied to the main checkout after Astra review and independent integration checks. Original patches are preserved; current-fixes.patch includes all current code/test/config changes.

### Dependency inventory and remaining recommendations

| Dependency | IDF path | PlatformIO path | Recommendation |
|---|---|---|---|
| ESP-IDF | lock5.5.4 | PlatformIO espressif32 6.10.0 platform | Evaluate IDF5.5.5 with target-specific advisories |
| LVGL | lock9.5.0 |9.4.0 | Document target differences; avoid blind alignment |
| RadioLib | vendored BuildOpt.h7.6.0 |7.4.0 | Record vendor revision/patches and assess target-specific fixes |
| nanopb | vendored pb.h identifies1.0.0-dev |^0.4.8 | Capture exact provenance/generated-code compatibility; version label alone does not prove wire incompatibility |
| esp_codec_dev / esp_lvgl_port | lock1.5.4 /2.7.2 | separate target stack | Preserve component lock hashes |
| PlatformIO CLI | not used for native IDF | pip --upgrade platformio in CI | Pin the known-tested CLI version |
| Python/font tools | Pillow and font downloads not integrity/version locked | same content pipeline | Record tested versions; verify approved font hashes, including cache hits |

Pin third-party GitHub Actions to reviewed full commits and track updates; retain the prepared explicit token-default fix. Audit Linux workflow path filters against actual transitive source/test dependencies (core_team/core_phone/ui_shared etc.) before narrowing CI for cost. Broad branch builds are not themselves defects. The simulator does not use CURL/SQLite; the worker's missing-package claim was retracted. Do not add mt_dedup or persistence files to Linux solely to mirror IDF. Shared Linux settings/hash link paths are follow-up candidates until an actual supported target fails. Use toolchain disassembly to verify sensitive-buffer cleanup; ordinary memset is not guaranteed zeroization. Font data passes through parsers and must not be dismissed as harmless, but no exploitable font-parser vulnerability was demonstrated.

Optimization priorities: profile map decode/eviction and SD latency before enlarging caches; keep bounded six-photo probes unless measured latency justifies an index count; inspect disabled-debug hex formatting in TeamService (strings constructed outside TEAM_LOG macro) before claiming avoidable heap work in the optimized binary. No speed, RAM or battery improvement is claimed without measurements.

### Additional rejected worker claims

- Delivery addendum's repeated assertion that branch is channel-key-passphrase/diverged from main is false: Astra's git output verifies main/e3c0fbd and ancestor relationship. No branch-only code distinction applies to the files read here.
- P4 default BLE is not PAIRING_NONE: `platform/esp/idf_common/src/c6_companion_runtime.cpp:1044` explicitly configures FIXED_PIN with device passkey. Open-mode acceptance in tm_ble.c is a mode-specific policy/hardening observation, not proven default unauthenticated P4 access. BLE end-to-end penetration tests remain outstanding.
- A POSIX directory name returned by os.listdir cannot contain `../` as a literal filename; the delivery worker's photo-directory traversal example is invalid. Symlink trust would be a distinct question.
- Do not use a grey Species unknown caption as the sole remedy for safety-critical lookalike images; TM-02 recommends withholding the unlabeled image.
- Prefix-only path checks, post-conversion finite checks and claims that FAT rename guarantees power-loss durability are unsafe remediation shortcuts and are rejected.

### Coverage limits at checkpoint

Three Sonnet reports plus one targeted follow-up each sampled Team/channel/wire crypto, pairing and NVS wiring, parts of PKI/BLE/C6, map cache/lifetime, input runtime, SD readers/writes, guide/translate/waypoint apps, core GPS parser, CI/manifests and content pipeline. MeshCore receive/crypto and PKI trust were not exhaustively traced; a whole-vendor audit, radio/phone adversarial tests, C6 fuzzing, sanitizer run, alternate Linux device builds and firmware/hardware tests remain open. Draft worker coverage tables are retained, but their claims are subordinate to Astra's corrections above. Existing fixed map UAF, stored-lastfix validation, GPS pan/fix separation, null-font fallback, NVS key persistence and async walkie call were inspected; that is not blanket assurance of all task lifetimes or malformed-input behavior.

## Current continuation notes
The original audit records describe tools as unavailable at initial intake. During local continuation, pinned GitNexus1.6.11 indexing and impact checks succeeded; flow truncation remains disclosed. The original RESUME.md is historical. Current authority/status is LOCAL-WORK.md and NONCE-STATUS.md. Accepted photo repairs and integrated nonce work supersede the earlier source-scope checkpoint; Team findings remain open. Team retry/recovery additionally calls sendKeyDistPlain on PRIMARY at team_page_deferred_dispatch.cpp:137, so secure pairing alone does not secure every later key-distribution path.
