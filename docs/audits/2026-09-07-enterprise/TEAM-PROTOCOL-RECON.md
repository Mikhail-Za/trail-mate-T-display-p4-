# Authenticated Team protocol — research checkpoint

Read-only Terra research with Astra source spot-checks, September 2026. This is evidence for the next design gate, not an approved protocol or completed implementation. No external specification/library review has run. Spark was not accessed.

## Reusable pieces and missing guarantees

- `modules/core_team/src/usecase/team_pairing_coordinator.cpp` implements Beacon/Join/Key with asserted IDs and a 32-bit join nonce. `handleKey` checks team/nonce but ignores the sender MAC. Adding a MAC comparison is filtering, not authentication: `idf_lora_pairing_transport.cpp` synthesizes the MAC from the claimed mesh node ID.
- `platform/esp/idf_common/src/team/idf_lora_pairing_service.cpp` uses a public sentinel in passphrase mode, then derives the real PSK locally. It provides no proof of password knowledge. On member derivation failure it forwards the received key unchanged. That fail-open branch needs its own bounded fix with failed-state/no-success-event checks.
- Team crypto currently exposes SHA256(key || info) truncation and ChaCha20-Poly1305. It has no authenticated handshake or DH/signature interface. `modules/core_mesh/src/protocol/meshcore/mc_identity_flow.cpp` offers signing/shared-secret primitives through MeshCore helpers; reuse is a candidate, not proof of a secure Team protocol.
- Both actual TFT/AMOLED generated ESP-IDF 5.5.4 configurations enable ECDH, ECDSA, Curve25519, ChaCha20/Poly1305 and disable HKDF. Configuration alone does not prove a particular complete protocol is linked or suitable. Source defaults and generated config must be updated together if a library requires additional primitives. Do not use root sdkconfig as native-target evidence.
- `team_runtime_idf.cpp` obtains random words from esp_random. Entropy guarantees at the P4 startup/runtime state require checking the actual ESP-IDF documentation before generating long-term identities or protocol nonces.

## Transport and persistence constraints

The current serializers use about 40 bytes for a named Beacon, 20 for Join and 37 for Key before mesh framing. Native adapter buffers (256-byte encoded app data, 512-byte wire buffer) are not an established safe radio MTU. Measure full encoded packet size/airtime and loss/reordering at the supported radio configurations. Pairing uses a 120-second leader window, 2-second beacons, 30-second member timeout and six 1.5-second Join retries. IDF receives through a single last-writer-wins frame buffer. A multistep handshake must explicitly handle loss, duplicates, limits and cancellation.

`idf_nvs_team_ui_snapshot_store.cpp` stores one 30-byte record (team ID, epoch, plaintext PSK and role flags), with no per-peer credentials, replay high-water or transaction state. Save/clear return void; save failures only log, erase ignores commit failure, and `platform_ui_team_ui_store_runtime.cpp::team_ui_save_keys_now` reports true after the void call. Do not treat this as a durable success acknowledgment or assume power-loss atomicity from source. The replacement must propagate storage failure before accepting a new epoch or reporting success. Both target configs disable NVS/flash encryption; changing device security configuration is outside this containment patch.

## Candidate directions requiring external review

1. A vetted embedded PAKE implementation with explicit key confirmation can authenticate knowledge of a pairing secret. It does not automatically identify the leader if all members know that secret. Bind a persistent leader credential and retain authenticated per-peer protection for later operations.
2. A vetted Noise-style authenticated handshake using persistent identities, with an appropriate verified initial trust anchor (for example a fingerprint/QR/SAS or existing trusted credential). Do not assemble raw ECDH and signatures into a homegrown protocol. No suitable implementation has been selected or evaluated yet.

These are alternatives to investigate, not instructions to combine every mechanism. Keep the current passphrase UX only if the selected protocol actually authenticates it. Subsequent recovery should authenticate the pinned leader credential; routine SAS confirmation is not inherently required by PAKE.

## Acceptance before restoring removal/recovery

- Confirmed pairing with wrong-secret/identity/transcript/tamper/timeout/cancel rejection; no raw PSK on radio and no success before key confirmation and durable installation.
- Leader-bound, recipient-bound, team-bound and epoch-bound updates. A removed member cannot decrypt the new group key or authorize updates using its old group secret. Per-peer delivery/retry remains confidential even when the removed member captures every packet.
- Replay, stale transfer, rollback and reboot behavior explicitly specified; persistent transaction/replay state with tested failure handling around every storage commit. Full backup rollback must not be promised detectable without an independent monotonic anchor.
- Lost first delivery, duplicate/out-of-order messages, transfer to a new leader, offline members and recovery tested with real crypto, host fault injection and both physical devices. Fake crypto alone cannot establish authentication or member exclusion.
