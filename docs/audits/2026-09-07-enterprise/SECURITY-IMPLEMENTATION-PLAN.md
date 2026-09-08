# Security remediation acceptance plan (2026-09-07 continuation)

User authorizes the explained photo, Team-key, and reboot nonce repairs. Current routing uses GPT workers, Astra orchestration/verification and Anthropic Crucible; do not access Spark during benchmarks. This document records acceptance criteria; NONCE-STATUS.md and the photo review records give current outcomes.

## Photo metadata

Runtime: validate a unique complete matching six-field credits row before rendering a caption or creating/decoding an image. Missing, unreadable, malformed, blank and ambiguous metadata withhold the image with an explanation. Keep navigation and clear previous labels. Ordinary captions and hazard coloring must remain exact. Two actual production function bodies are exercised with observable host display/file substitutes; firmware build/layout remain separate requirements.

Staging: build_sd_content.py currently renumbers numeric source images but copies credits unchanged. A source gap can attach the wrong valid caption to a photo, defeating a runtime presence check. Preflight all photo-to-credit mappings before writing any photo output, map unique source numeric identities to output names together (the existing sourcing instructions allow PNG sources with matching-number JPG credit names), preserve species/attribution, reject ambiguous numeric names and more than six photos (viewer cap). Do not touch content on physical cards. Current raw image collection is not available for validation here.

## Team keys — protocol work required

Evidence: TeamService::processIncoming installs any decoded KeyDist directly. UI reducer also installs keys from its event. Both trust boundaries matter. Pairing uses a distinct coordinator event but the shared event shape currently does not carry authenticated pairing provenance. Sender IDs alone are spoofable, including by members who know the group key.

Additional code trace: TeamPageKickConfirmAction::confirmKick sends the replacement PSK encrypted by the old group's key to remaining destinations, switches local keys, and queues retries. TeamPageDeferredDispatchQueue::processKeyDistRetries sends the replacement PSK in plaintext. A kicked member knows the old key and can capture packets addressed to remaining members; group-key encryption cannot establish exclusion. Replacing the retry call with sendKeyDist encrypts under the new key that lagging members lack. Neither change alone is a complete repair.

Required design: authenticate pairing using vetted key establishment with user-verifiable trust; retain a per-peer protected relationship for later rotation/recovery. Bind updates to team, authenticated leader, recipient, transaction, and monotonic epoch before ANY event/key/persistence mutation. Preserve replay state across reboot. Reject legacy insecure updates; provide explicit version/migration behavior and recovery. Initial pairing and recovery must never transmit real PSK material in plaintext. Existing passphrase sentinel mode avoids transmitting the key but has no proof-of-password key confirmation; do not call it authenticated pairing.

Acceptance: turn the existing foreign-team reproduction into rejection tests; verify zero derivations/events/persistence changes for unsolicited, plaintext, cross-team, forged-leader, member-forged and replayed updates. Correct pairing, wrong-password rejection, leader transfer, rotation, missed first delivery, reboot recovery and removal must pass. Packet captures must show no raw PSK, and the removed member must be unable to decrypt new-epoch traffic. Two-device/firmware verification is required; host fake crypto alone cannot prove these properties.

## Meshtastic nonce allocation

User-confirmed scope: private channel keys are used only in Trail Mate. No coordination with Meshtastic/MeshOS counters is needed for those keys. This resolves the cross-firmware question, but does not establish unused packet numbers for existing keys or make NVS rollback safe. The native repair is now integrated; see NONCE-STATUS.md for verification and review status.

Evidence: adapter restarts next_packet_id_ at 1; AES-CTR nonce includes that packet ID and stable sender identity. sendText and sendEncodedPayload allocate IDs independently; sendAppData can supply explicit IDs. All must use a single allocation policy.

Required design: persist a reserved upper bound BEFORE transmitting any ID in its range; skip unused reserved IDs after restart. Allocate within the range in RAM to limit flash writes. Reject exhaustion and storage errors; do not wrap, use time XOR, or substitute a small random seed. Address explicit caller IDs without allowing previously consumed values. NVS erase/restore and two adapter instances cannot silently restart the same key/identity sequence.

Migration is essential: beginning a fresh counter at 1 on an upgraded device with an existing key repeats historical nonces. Existing private keys need a controlled fresh-key transition or another demonstrably nonoverlapping protocol-compatible nonce domain. Default public channel encryption does not provide confidentiality. Persisted state and channel key import/restore need a coherent policy; no claim of retroactive protection for already-recorded traffic.

Acceptance: repeated cold boots with stable key/identity, simulated power loss before/after reservation commit, commit/open/read failures, exact range/end-of-space boundaries, explicit IDs, concurrent allocation, NVS loss and old-key migration. Inspect resulting wire nonces, not only allocator return values. Verify Meshtastic compatibility and actual IDF NVS integration.

## Build prerequisite

No idf.py/export toolchain found in documented local /home/zaidm/esp, /home/zaidm/.espressif, /opt or Windows mount locations. Asked user for current build machine/path while continuing host work. No firmware compile, hardware pass, commit, push or deployment is claimed by this plan.

Build recovery: user suggested parent folder. Parent contains launcher/C6 updater and historical Windows IDF references, no installed toolchain; recovery mounts/local project searches also found none. Official docker.io/espressif/idf:v5.5.4 image acquired (image config SHA256 209569617d40dc2190907bfa7508aa5dfc0823698663fc28a4405ef8012a78b0). TFT build started in isolated /tmp/trailmate-audit-20260907/idf-source copy under rootless Podman, never against physical hardware. Outcome pending.

Build recovery result: both TFT and AMOLED full builds PASS using IDF 5.5.4. dependencies.lock byte-identical after component resolution. Artifacts and source scope recorded in firmware-baseline-builds.json; this predates pending photo edits. Build prerequisite resolved, physical device/SD/radio checks remain.

Staging evidence strengthened: actual baseline build_sd_content.main executed in a temporary fixture with source0.jpg/1.jpg, mocked image I/O, and unrelated font/translation groups empty. It writes output1.jpg/2.jpg but credits remain0.jpg/1.jpg, confirming misassociation and an unlabelled second image. This is host-reproduced control flow, not only a source trace; no actual SD content modified.
