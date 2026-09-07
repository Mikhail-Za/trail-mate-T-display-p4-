I now have sufficient evidence across all required trust boundaries. Producing the final report.

---

# Trail Mate — Enterprise Security & Code Audit Report

**Auditor:** Astra Conductor / Claude Sonnet 4.6 (read-only, bounded)
**Baseline:** `e3c0fbd` (main) · session branch `channel-key-passphrase` · maintainer baseline `98609d5`
**Ponytail skill:** ✅ Loaded and active at **full** intensity — `/home/zaidm/.codex/skills/ponytail/SKILL.md` confirmed: "Forces the laziest solution that actually works… ACTIVE EVERY RESPONSE." Fixes recommended below use the shortest correct change; no speculative abstractions.
**GitNexus:** Unavailable (MCP not present in this environment). Call-graph traces performed by static read-tool traversal; this is disclosed as a coverage limitation.
**Hardware/runtime claims:** None made from source alone.

---

## 1. Documents Loaded & Key Context

| Document | Key facts consumed |
|---|---|
| `AGENTS.md` | GitNexus unavailable; instructions to use read tools |
| `MAINTAINERS.md §12` | On-device state current; both units flashed & confirmed booting. Two **open items** named in §7: Team PSK cleartext pairing, NVS key persistence |
| `BENCH_TEST_CHECKLIST.md` | Flash-day runbook; single-touch path proven; pinch HW-unverified |
| `MAP_UX_ROADMAP.md §3-4` | Code-review follow-ups tracked; deferred items listed |

---

## 2. Reviewed Path Inventory

| Module / File | Scope |
|---|---|
| `modules/core_chat/src/domain/channel_hash.cpp` | `channelHashFromRecord`, `expandChannelPsk` — primary Astra candidate |
| `modules/core_chat/src/domain/channel_persist.cpp` | NVS channel-blob decode/encode |
| `modules/core_chat/src/domain/channel_key.cpp` | Passphrase → PSK derivation |
| `modules/core_chat/include/chat/domain/channel_record.h` | `ChannelRecord` struct definition |
| `modules/core_chat/include/chat/domain/chat_types.h` | `normalizeMeshtasticChannelKeyLen` |
| `modules/core_team/src/usecase/team_service.cpp` | Full MGMT receive path (all 8 message types), encrypt/decrypt, send helpers |
| `modules/core_team/src/usecase/team_pairing_coordinator.cpp` | Pairing state machine (Beacon/Join/Key), PSK distribution |
| `modules/core_team/src/protocol/team_pairing_wire.cpp` | Wire encode/decode for pairing packets |
| `modules/core_team/src/protocol/team_mgmt.cpp` | `decodeTeamKeyDist` validation |
| `modules/core_team/include/team/protocol/team_wire.h` | Nonce size (12 bytes), envelope version |
| `modules/core_team/include/team/protocol/team_mgmt.h` | `kTeamChannelPskSize` = 16 |
| `modules/core_team/include/team/domain/team_types.h` | `TeamKeys`, `TeamId` |
| `modules/core_mesh/src/usecase/mesh_dedup_service.cpp` | Replay / dedup ring |
| `modules/core_mesh/include/mesh/usecase/mesh_dedup_service.h` | `kCapacity = 32`, `ttl_ms_ = 60000` |
| `modules/core_mesh/src/usecase/receive_packet_service.cpp` | `packet_id == 0` bypass |
| `modules/core_mesh/src/protocol/meshtastic/meshtastic_protocol_strategy.cpp` | Build/parse paths |
| `modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp` | AES-CTR nonce construction |
| `firmware/c6_companion/components/tm_espnow/tm_espnow.c` | ESP-NOW peer config, recv/send, `peer.encrypt = false` |
| `firmware/c6_companion/components/tm_hostlink/tm_hostlink.c` | SDIO host-link frame dispatch, state machine |
| `modules/ui_shared/src/ui/team_persistence/team_ui_snapshot_codec.cpp` | NVS snapshot codec — PSK persistence check |
| `modules/ui_shared/src/ui/screens/team/team_page_state_store.cpp` | Snapshot apply |
| `platform/esp/idf_common/src/idf_chat_factory.cpp` | NVS channel-blob load/save |
| `platform/esp/radio/meshtastic_radio_adapter.cpp` (lines 706-759) | `updateChannelKeys` normalization path |

---

## 3. Positive Controls (Working Correctly)

The following security-relevant behaviours are **correctly implemented**:

| # | What | Evidence |
|---|---|---|
| P1 | Team encrypted-envelope nonce is 12-byte random (`fillRandomBytes`) → appropriate for AES-GCM | `team_wire.h:12` `kTeamNonceSize=12`; `team_service.cpp:1100` |
| P2 | `decodeTeamKeyDist` caps `channel_psk_len ≤ kTeamChannelPskSize=16` | `team_mgmt.cpp:239` |
| P3 | Kick and TransferLeader hard-gate on `decoded_encrypted` | `team_service.cpp:411`, `430` |
| P4 | KeyRequest hard-gates on `decoded_encrypted` | `team_service.cpp:470` |
| P5 | PSK derivation uses key context labels ("team_mgmt", "team_pos", etc.), producing separate sub-keys per channel | `team_service.cpp:287-303` |
| P6 | Team key-ID mismatch rejects decryption (`envelope->key_id != keys_.key_id`) | `team_service.cpp:1050-1063` |
| P7 | Channel-persist blob uses magic+version header before parsing | `channel_persist.cpp:79-80` |
| P8 | Beacon packet name-length capped at `kMaxTeamNameLen=15` | `team_pairing_wire.cpp:148` |
| P9 | `startLeader` guards against null/empty PSK | `team_pairing_coordinator.cpp:127-129` |
| P10 | Pairing join-nonce is a freshly randomized 32-bit value | `team_pairing_coordinator.cpp:254` |
| P11 | `normalizeMeshtasticChannelKeyLen` correctly limits to 0/16/32 before radio-adapter `channelHashFromRecord` call | `chat_types.h:40-71`; `meshtastic_radio_adapter.cpp:730-748` |
| P12 | Dedup service skips zero packet_id consistently at both call and service level | `receive_packet_service.cpp:31-35`; `mesh_dedup_service.cpp:13-16` |
| P13 | `decryptPayload` returns false when no crypto backend is available (fail-safe) | `mt_packet_wire.cpp:217-219` |
| P14 | `expandChannelPsk` handles the zero-key (no PSK, open channel) case correctly by returning early | `channel_hash.cpp:43-46` |

---

## 4. Security Findings

### FINDING 1 — `expandChannelPsk` stack-buffer overflow: latent but function is unsafe at API level

**Status:** Latent (production call-site has caller-side mitigation; function itself is unsafe)
**Severity:** Medium | **Confidence:** High

**Evidence:**
- `modules/core_chat/src/domain/channel_hash.cpp:63`
```cpp
// 16/32 (or any explicit length): used verbatim.
if (key)
{
    std::memcpy(out, key, key_len);  // ← key_len is uint8_t, out is 32 bytes
}
```
- `modules/core_chat/src/domain/channel_hash.cpp:84`: the only caller within the function allocates `uint8_t expanded[32]`, then passes it as `out`:
```cpp
uint8_t expanded[32] = {};
expandChannelPsk(rec.key, rec.key_len, expanded, &expanded_len);
```
- `modules/core_chat/src/domain/channel_persist.cpp:101`: `r.key_len = p[1 + kNameBytes + kKeyBytes];` — reads a raw byte from the NVS blob with **no post-read validation**. Any value 0–255 is stored.
- `ChannelRecord.key` field is 32 bytes; `ChannelRecord.key_len` is `uint8_t`. If `key_len > 32`, the `memcpy` reads 32 valid bytes then reads past `rec.key` into neighboring struct members (`key_len` itself, `channel_id`), and writes all `key_len` bytes into the 32-byte `expanded` stack buffer → **stack buffer overflow + out-of-bounds struct read**.

**Trigger / reachable callers:**
1. Direct path (MITIGATED): `channel_persist::decode` → `NvsChannelBlobStore::load` → `MeshConfig.channels` → `meshtastic_radio_adapter.cpp:730` calls `normalizeMeshtasticChannelKeyLen` (caps to 0/16/32) → `channelHashFromRecord` with safe `key_len`.
2. Unprotected path (NO CURRENT caller, but the function is `extern` in the public header `channel_hash.h`): any future call to `channelHashFromRecord` or `expandChannelPsk` that passes an un-normalized `ChannelRecord` (e.g., a test, a tool, a new feature) will overflow.
3. Host test path: `channel_hash.h` is explicitly intended for host-buildable tests; a fuzzer or malformed test vector could trivially hit this.

**Existing mitigation:** `normalizeMeshtasticChannelKeyLen` at `meshtastic_radio_adapter.cpp:730` protects the single production call site. No other mitigation exists inside the function itself.

**Minimal fix (ponytail):** Add a single cap inside `expandChannelPsk`; no signature change needed because the maximum valid PSK is 32 bytes:

```cpp
// In expandChannelPsk, before the memcpy:
// 16/32 (or any explicit length): used verbatim, capped to 32.
const uint8_t capped_len = key_len > 32 ? 32 : key_len;
if (key)
{
    std::memcpy(out, key, capped_len);
}
if (out_len)
{
    *out_len = capped_len;
}
```

Also add in `channel_persist::decode` after reading `r.key_len`:
```cpp
if (r.key_len > 32) { r.key_len = 0; }  // reject impossible key length
```

**Failable check:**
```cpp
// Test: key_len = 64 must NOT produce more than 32 bytes of output and must NOT crash
uint8_t fake_key[32] = {};
uint8_t out[32] = {};
std::size_t out_len = 0;
chat::meshtastic::expandChannelPsk(fake_key, 64, out, &out_len);
assert(out_len <= 32);  // fails without fix
```

---

### FINDING 2 — Team PSK transmitted cleartext over unencrypted ESP-NOW (Known-Open Confirmed)

**Status:** Known-Open (MAINTAINERS.md §7: "PSK is currently in the clear") — **Confirmed by source**
**Severity:** High | **Confidence:** High

**Evidence:**
- `firmware/c6_companion/components/tm_espnow/tm_espnow.c:114`: `peer.encrypt = false;`
  All ESP-NOW peers are added with WPA encryption disabled at the 802.11 layer.
- `modules/core_team/src/usecase/team_pairing_coordinator.cpp:394-419` (`sendKey`): builds a `KeyPacket` containing the raw `team_psk_` bytes and sends it over ESP-NOW unicast to the member's MAC:
```cpp
packet.channel_psk = team_psk_;      // raw PSK bytes
packet.channel_psk_len = team_psk_len_;
... transport_.send(mac, wire.data(), wire.size());  // ← no encryption
```
- `modules/core_team/src/protocol/team_pairing_wire.cpp:185-203` (`encodeKey`): serializes the PSK in plaintext (length prefix + raw bytes, no confidentiality).

**Trigger:** Any WiFi-capable device in RF range during the pairing window (leader opens for 120 seconds, `kLeaderWindowMs`) can passively capture the `KeyPacket` frame in promiscuous mode with a standard monitor-mode adapter and recover the full PSK.

**Existing mitigation:** None at the transport layer. The pairing window is time-limited (120s). The nonce in the JoinPacket is 32-bit random, providing marginal protection against packet injection (see Finding 3), but offers no protection against passive capture.

**Minimal fix (ponytail):** The minimal correct approach is **ECDH key agreement** during pairing: leader and member exchange ephemeral public keys, compute a shared secret, then encrypt the PSK with that secret before sending. The existing `ed25519` library under `modules/core_chat/src/infra/meshcore/crypto/ed25519/` supports key exchange (`key_exchange.c`), so no new dependency is needed.

A shorter alternative is a **passphrase-displayed-on-screen confirmation** (numeric SAS: both sides display a hash of `team_id || nonce || computed_shared_secret`; user verbally confirms match). This provides authentication without requiring ECDH changes to the wire format, at the cost of one user interaction step. This is the approach already described in the roadmap.

**Failable check:** A protocol-level test that verifies the `KeyPacket` wire bytes do NOT contain the PSK in any recognizable form when a shared secret is in use (requires the fix to be implemented first).

---

### FINDING 3 — `handleKey` discards source MAC; nonce is visible in JoinPacket → MITM attack on pairing

**Status:** Newly identified
**Severity:** High | **Confidence:** High

**Evidence:**
- `modules/core_team/src/usecase/team_pairing_coordinator.cpp:285-287`:
```cpp
void TeamPairingCoordinator::handleKey(const uint8_t* mac, const uint8_t* data, size_t len)
{
    (void)mac;  // ← source MAC is explicitly discarded
```
- The only validation before accepting the `KeyPacket` is at line 299:
```cpp
if (!has_team_id_ || packet.team_id != team_id_ || packet.nonce != join_nonce_)
{
    return;
}
```
- `join_nonce_` is the 32-bit random nonce that the member sends in its own `JoinPacket` (line 370): `packet.nonce = join_nonce_;`
- The `JoinPacket` is sent unicast to `leader_mac_` but over unencrypted ESP-NOW — it is fully readable by any monitor-mode WiFi device in RF range.

**Attack:** Attacker in RF range sees Beacon (gets `team_id`), sees JoinPacket (gets `nonce`), constructs a fake `KeyPacket` with attacker-controlled PSK, sends it to the member before the real leader responds. Member accepts it (team_id + nonce match, MAC ignored). All subsequent team traffic is encrypted with the attacker's PSK.

**Existing mitigation:** 30-second member scan window, 32-bit nonce (2^32 brute-force impractical in window). But passive capture makes brute-force unnecessary — the nonce is visible.

**Minimal fix (ponytail):** Two independent one-line changes:
1. **Validate source MAC** in `handleKey`: only accept `KeyPacket` from `leader_mac_` (which is already stored at line 252: `memcpy(leader_mac_, mac, 6); leader_mac_valid_ = true;`):
```cpp
// In handleKey, replace (void)mac; with:
if (!leader_mac_valid_ || memcmp(mac, leader_mac_, 6) != 0)
{
    return;
}
```
This does not prevent Finding 2 (cleartext PSK capture) but defeats the injection attack.
2. Long-term: encrypt the `KeyPacket` (see Finding 2 fix).

**Failable check:**
```cpp
// Test: a KeyPacket from a different MAC than the stored leader_mac_ must be rejected
// even when team_id and nonce match.
coordinator.handleKey(wrong_mac, valid_key_packet, valid_len);
assert(coordinator.getStatus().state != TeamPairingState::Completed);
```

---

### FINDING 4 — `KeyDist` accepted without encryption guard; any mesh node can replace the team PSK

**Status:** Newly identified (design gap)
**Severity:** High | **Confidence:** High

**Evidence:**
- `modules/core_team/src/usecase/team_service.cpp:447-466` (the `KeyDist` case in `processIncoming`):
```cpp
case team::proto::TeamMgmtType::KeyDist:
{
    // ← NO if (!decoded_encrypted) break;  guard here
    team::proto::TeamKeyDist msg;
    if (!team::proto::decodeTeamKeyDist(payload.data(), payload.size(), &msg)) { break; }
    ...
    if (msg.channel_psk_len > 0 && msg.key_id != 0)
    {
        setKeysFromPsk(msg.team_id, msg.key_id,    // ← no team_id match check
                       msg.channel_psk.data(), msg.channel_psk_len);
    }
}
```
- Contrast with `Kick` (line 411), `TransferLeader` (line 430), and `KeyRequest` (line 470), which ALL have `if (!decoded_encrypted) break;` — `KeyDist` is uniquely unguarded.
- Additionally, `setKeysFromPsk` (called at line 463) does **not verify** that `msg.team_id` matches the current `keys_.team_id`. A malicious node can set any team_id.
- The `sendKeyDistPlain` API exists intentionally for bootstrapping members who don't yet have the key — but the **receive** side accepts it without any authentication, creating an unintended attack surface.

**Trigger / reachable caller:** Any LoRa-range radio (including replay of a valid plaintext bootstrap `KeyDist`) can send `TEAM_MGMT_APP` portnum with a `KeyDist` message on the Meshtastic channel. On the primary (LongFast) channel, the default PSK is publicly known, so packet injection requires no prior knowledge.

**Impact:** Attacker replaces the team PSK with an attacker-controlled value → all encrypted team traffic (position, chat, waypoints) becomes decryptable by the attacker; legitimate team members can no longer communicate with the victim device.

**Existing mitigation:** If the team is on a non-default channel with a private PSK, the attacker must know or brute-force that PSK to inject a valid Meshtastic-layer packet. If on the default LongFast channel (public PSK), no mitigation exists.

**Minimal fix (ponytail):** Two independent guards:
1. Add the same `decoded_encrypted` guard as Kick/TransferLeader — this stops the unauthenticated path:
```cpp
case team::proto::TeamMgmtType::KeyDist:
{
    if (!decoded_encrypted)  // ← add this guard
    {
        break;
    }
    ...
}
```
2. Inside `setKeysFromPsk` (or at the call site), verify the incoming `team_id` matches the current one if keys are already set:
```cpp
if (keys_.valid && msg.team_id != keys_.team_id)
{
    break;  // reject cross-team key injection
}
```

Note: This changes the plaintext bootstrap behaviour — members who receive a plain `KeyDist` before they have any key will no longer auto-accept it. The correct bootstrap path should be the ESP-NOW pairing (once Finding 2 is fixed) or a secured `KeyDist`.

**Failable check:**
```cpp
// Inject a plaintext KeyDist with a different team_id — service must reject it.
// After fix: keys_.team_id must remain unchanged.
assert(service.keys().team_id == original_team_id);
```

---

### FINDING 5 — Team PSK not persisted in NVS snapshot; lost on reboot (Known-Open Confirmed)

**Status:** Known-Open (MAINTAINERS.md §7: "NVS persistence so pairing survives reboot") — **Confirmed by source**
**Severity:** Medium | **Confidence:** High

**Evidence:**
- `modules/ui_shared/src/ui/team_persistence/team_ui_snapshot_codec.cpp:118-182` (`encodeTeamUiSnapshot`): the codec serializes `in_team`, `team_id`, `security_round`, `member_count`, etc. but **never writes `team_psk` or `has_team_psk`**.
- `modules/ui_shared/src/ui/screens/team/team_page_state_store.cpp:76-77`: `snapshot.team_psk` is copied into `state` on load/apply — but since the encoder never writes it, the decoded snapshot always has a zero PSK.
- Result: every reboot requires re-pairing even if the team is still active.

**Minimal fix (ponytail):** Extend the snapshot codec (bump kTeamUiSnapshotCurrentVersion, add a v3 section) to serialize `team_psk[kTeamChannelPskSize]` and `has_team_psk`. The existing versioned decoder already handles format evolution (v1/v2 at line 231). This adds 17 bytes to the NVS blob. The PSK is stored in NVS plaintext — on ESP32, NVS is unencrypted by default. For this device's threat model (personal family device), this is acceptable. If NVS encryption is desired in the future, enabling `CONFIG_NVS_ENCRYPTION` is a single sdkconfig flag.

**Failable check:**
```cpp
// Encode a snapshot with has_team_psk=true, decode it, verify psk survives.
TeamUiSnapshot s; s.has_team_psk = true; s.team_psk[0] = 0xA5;
auto bytes = encode(s, ts, out);
TeamUiSnapshot decoded; assert(decode(bytes, decoded));
assert(decoded.has_team_psk && decoded.team_psk[0] == 0xA5);
```

---

### FINDING 6 — Dedup ring capacity too small; replay possible in active mesh (32-entry FIFO, 60s TTL)

**Status:** Newly identified
**Severity:** Low-Medium | **Confidence:** Medium

**Evidence:**
- `modules/core_mesh/include/mesh/usecase/mesh_dedup_service.h:28`: `static constexpr size_t kCapacity = 32;`
- `modules/core_mesh/src/usecase/mesh_dedup_service.cpp:31-38`: entries written to a round-robin ring with `next_slot_ = (next_slot_ + 1) % kCapacity`. When 33+ unique `{peer, packet_id}` pairs arrive within 60 seconds, the ring wraps and older entries become eligible for re-acceptance.
- `modules/core_mesh/src/usecase/receive_packet_service.cpp:31-35`: `packet_id == 0` also bypasses dedup (deliberate — zero IDs are never deduplicated).

**Trigger:** An attacker who can replay a captured mesh packet waits until 32 newer packets from different nodes have pushed the target entry out of the ring (at busy trail-head or event with multiple mesh nodes this can happen in seconds), then retransmits.

**Existing mitigation:** 60-second TTL evicts expired entries on the next check for a given peer; in practice, TTL alone provides some protection for low-traffic meshes. For a personal 2-unit device, this is low risk.

**Minimal fix (ponytail):** Increase `kCapacity` from 32 to 64 or 128 (trivial memory impact: each `Entry` is ~13 bytes → 64 entries = ~832 bytes on stack/BSS).

**Failable check:**
```cpp
// Insert 33 distinct {peer, packet_id} pairs, then verify the first is re-accepted.
MeshDedupService dedup(60000);
for (uint32_t i = 1; i <= 32; ++i) dedup.accept(i, i, 0);
assert(dedup.accept(1, 1, 0) == false);  // should still be in ring — fails today at >32
```

---

### FINDING 7 — Meshtastic AES-CTR nonce collision: same-millisecond packets from same node reuse keystream

**Status:** Newly identified (protocol limitation inherited from Meshtastic)
**Severity:** Low | **Confidence:** High

**Evidence:**
- `modules/core_mesh/src/protocol/meshtastic/meshtastic_protocol_strategy.cpp:37-39`:
```cpp
return context.now_ms != 0 ? context.now_ms : 1U;
```
  When no explicit `packet_id` is provided, the packet ID defaults to `now_ms`.
- `modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp:128-133`: the AES-CTR nonce is `[packet_id LE (8 bytes)][from_node LE (4 bytes)][block_counter (4 bytes, starts 0)]`. Two messages sent by the same node in the same millisecond → identical `now_ms` → identical nonce → AES-CTR keystream reuse → XOR of ciphertexts reveals XOR of plaintexts.

**Existing mitigation:** The AES-CTR keystream depends on `from_node` XOR'd in as bytes 8–11, so only same-node collisions matter. For a 2-device personal deployment, same-millisecond collision probability is negligible. This is Meshtastic's standard protocol limitation, not a local defect.

**Minimal fix (ponytail):** Add a per-session monotonic counter to `packet_id` generation when `context.packet_id == 0`: `static uint32_t counter = 0; return now_ms ^ (++counter);`. This effectively eliminates same-millisecond repeats without breaking inter-device packet_id semantics (dedup uses `{peer, packet_id}`, not absolute monotonicity).

**Failable check:**
```cpp
// Two consecutive buildDirectMessage calls in same ms must produce different packet_ids.
auto id1 = packetIdFromContext(ctx_with_same_now_ms);
auto id2 = packetIdFromContext(ctx_with_same_now_ms);
assert(id1 != id2);  // fails today without counter
```

---

### FINDING 8 — C6 companion SDIO host-link has no P4 authentication (informational)

**Status:** Informational — internal bus
**Severity:** Informational | **Confidence:** High

**Evidence:**
- `firmware/c6_companion/components/tm_hostlink/tm_hostlink.c:157-192` (`handle_hello`): the C6 accepts any `HELLO` frame with a compatible protocol version and transitions to `TM_HOSTLINK_READY` — no shared secret, challenge-response, or MAC-level identity.
- Post-handshake, `TM_C6_FRAME_CONFIG_SET` can disable BLE, reconfigure WiFi credentials, trigger ESP-NOW with arbitrary MAC/channel settings (line 217-281).
- The trust boundary is the physical P4↔C6 SDIO bus. An attacker with PCB access (or exploiting the P4 app) could issue arbitrary C6 commands.

**Existing mitigation:** Physical isolation (internal bus); the P4 app is the sole SDIO master. No remote attack path exists today.

**Minimal fix:** None needed given the physical trust model. Document this as an intentional design assumption. If flash-via-OTA or BLE-remote-config is added in the future, add a HMAC challenge on the HELLO handshake.

---

## 5. Coverage Gaps

| Gap | Reason | Risk |
|---|---|---|
| `platform/esp/radio/meshtastic_radio_adapter.cpp` (full file) | Only lines 706-759 read | Could miss LoRa packet injection guards in receive path |
| `modules/core_chat/src/usecase/chat_service.cpp` | Not read | Channel-key selection logic for send not verified |
| `modules/core_chat/src/infra/meshcore/` (MeshCore protocol) | Not read | Alternate protocol's crypto not audited |
| `modules/core_chat/src/infra/meshtastic/mt_pki_crypto.cpp` | Not read | PKI key verification / ECDH path not audited |
| `modules/core_mesh/src/protocol/meshtastic/mt_receive_flow.cpp` | Not read | Meshtastic RX channel matching / PSK selection not traced |
| `modules/ui_shared/src/ui/screens/team/` (full UI action layer) | Partially sampled via grep | Full PSK flow from user input to `setKeysFromPsk` not traced |
| BLE phone-core (`modules/core_chat/include/chat/ble/`) | Not read | BLE admin channel channel-config injection not assessed |
| `firmware/c6_companion/components/tm_ble/tm_ble.c` | Not read | BLE GATT characteristic write bounds not checked |
| Hardware timing / radio duty-cycle | Source-only audit | Cannot confirm compliance from code |

---

## 6. Summary Table

| # | Finding | Status | Severity | Path:line | Has Fix |
|---|---|---|---|---|---|
| 1 | `expandChannelPsk` stack overflow (latent) | Latent, caller-mitigated | Medium | `channel_hash.cpp:63` | Yes |
| 2 | Team PSK cleartext over ESP-NOW | Known-Open Confirmed | High | `team_pairing_coordinator.cpp:404-419`, `tm_espnow.c:114` | Partial (ECDH or SAS) |
| 3 | `handleKey` ignores source MAC → PSK injection | Newly identified | High | `team_pairing_coordinator.cpp:287` | Yes (1-line guard) |
| 4 | `KeyDist` accepted without `decoded_encrypted` guard | Newly identified | High | `team_service.cpp:447-466` | Yes (1-line guard) |
| 5 | Team PSK not persisted in NVS | Known-Open Confirmed | Medium | `team_ui_snapshot_codec.cpp:118-182` | Yes (extend codec) |
| 6 | Dedup ring wraps at 32 entries → replay window | Newly identified | Low-Med | `mesh_dedup_service.h:28` | Yes (increase kCapacity) |
| 7 | AES-CTR nonce collision on same-ms packets | Newly identified (protocol) | Low | `meshtastic_protocol_strategy.cpp:38` | Yes (counter XOR) |
| 8 | C6 SDIO no P4 authentication | Informational | Info | `tm_hostlink.c:157-192` | N/A |

---

## 7. Priority Recommendations (Ponytail order — highest leverage per line changed)

1. **[P1 — 1 line] Finding 3: MAC guard in `handleKey`** — `if (memcmp(mac, leader_mac_, 6) != 0) return;` in `team_pairing_coordinator.cpp:287`. Zero risk of regression; fixes a concrete MITM injection attack with a single comparison.

2. **[P2 — 1 line] Finding 4: `decoded_encrypted` guard on `KeyDist`** — `if (!decoded_encrypted) break;` at `team_service.cpp:447`, matching the exact pattern already on Kick/TransferLeader. No other change needed. Monitor whether any legitimate use of `sendKeyDistPlain` breaks — if so, the receive-side must filter by whether the receiver has no key yet.

3. **[P3 — 2 lines] Finding 1: Cap `key_len` in `expandChannelPsk`** — add `const uint8_t capped_len = key_len > 32 ? 32 : key_len;` before the `memcpy` and use `capped_len`. Also add the 1-line reject in `channel_persist::decode`. Eliminates the latent overflow without changing any observable behaviour (valid PSKs are always ≤ 32 bytes).

4. **[P4 — small codec extension] Finding 5: Persist team PSK** — Extend `encodeTeamUiSnapshot`/`decodeTeamUiSnapshot` to round-trip the 16-byte PSK and the `has_team_psk` flag (version bump to v3). Operational friction of re-pairing every reboot will vanish.

5. **[P5 — 2-byte constant] Finding 6: Widen dedup ring** — Change `kCapacity = 32` to `kCapacity = 64` in `mesh_dedup_service.h`. Doubles the replay-suppression window at negligible RAM cost (~416 extra bytes).

6. **[P6 — design, medium effort] Finding 2: Secure PSK delivery** — The cleanest solution using existing code: generate an ephemeral X25519 keypair per pairing session (the `ed25519` key-exchange primitive is already vendored under `modules/core_chat/src/infra/meshcore/crypto/ed25519/key_exchange.c`), exchange public keys in Beacon/Join, derive a wrapping key, encrypt the PSK before sending in `KeyPacket`. This is the proper fix for the cleartext PSK problem. The shorter-term mitigation (Finding 3's MAC check) blocks injection; this blocks passive eavesdropping.

7. **[P7 — counter add, Finding 7] AES-CTR nonce counter** — Add a module-level `static uint32_t s_tx_seq = 0;` in `meshtastic_protocol_strategy.cpp` and XOR `++s_tx_seq` into `packet_id` when defaulting to `now_ms`. Low priority for a 2-device deployment.

---

*Report complete. All findings cite exact file:line evidence read from source. No CVE numbers assigned (no CVE lookup tool available). No hardware or performance claims made. Astra Conductor handles fix validation and application.*
