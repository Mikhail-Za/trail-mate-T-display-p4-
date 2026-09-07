Now I have all the evidence needed. Writing the addendum.

---

# Audit Addendum — Trail-Mate Security Review
**Baseline:** `main e3c0fbd68663bbeef09056a22cbce950065304bc`
**Worker:** Astra Conductor — Bounded Read-Only (Sonnet 4.6)
**Date:** 2026-09-07

---

## Skill Confirmation

**Ponytail loaded and active.** Verified at session start from `/home/zaidm/.codex/skills/ponytail/SKILL.md`. Full-intensity discipline: shortest correct fix, no speculative abstractions. Standing constraint honoured: "Never simplify away: input validation at trust boundaries, security measures."

**GitNexus MCP unavailable** — this session uses read tools only. No git diff, history, or branch comparison was performed. All citations are from source files at the paths listed.

---

## RETRACTION — Finding 5: Team PSK Not Persisted on Reboot

**Original claim:** The team PSK was not persisted to NVS and would be lost on reboot because `team_ui_snapshot_codec.cpp` does not serialize `team_psk`.

**Retracted.** This was a false positive. Evidence:

**`platform/esp/idf_common/src/team/idf_nvs_team_ui_snapshot_store.cpp:127-128`** (restore path):
```cpp
std::memcpy(out.team_psk.data(), &blob[kOffPsk], kTeamChannelPskSize);
out.has_team_psk = true;
```

**`platform/esp/idf_common/src/team/idf_nvs_team_ui_snapshot_store.cpp:171`** (save path):
```cpp
std::memcpy(&blob[kOffPsk], in.team_psk.data(), kTeamChannelPskSize);
```

The 30-byte NVS blob (`namespace: team_keys, key: v1`) stores:
`version(1) + team_id(8) + key_id(4) + psk(16) + flags(1)`.

The PSK is correctly persisted in a **dedicated key store**, separate from the UI snapshot codec (`team_ui_snapshot_codec.cpp`). The UI codec omitting PSK is correct design: UI state and key material use separate serialization paths. The `save()` function erases the entry when `!in.has_team_psk || !teamIdIsNonZero(in.team_id)` — safe erase on leave or clear.

**Known open item from `MAINTAINERS.md §7` ("NVS key persistence so pairing survives reboot") refers to a different milestone — confirmed resolved at the code level for the baseline commit.**

---

## RETRACTION — Team Log Key Leaks

**Original implication:** Team log macros might expose PSK/plaintext material.

**Retracted.** Evidence: `modules/core_team/src/usecase/team_service.cpp:16-21`:
```cpp
#define TEAM_LOG_ENABLE 0
#if TEAM_LOG_ENABLE
#define TEAM_LOG(...) std::printf(__VA_ARGS__)
#else
#define TEAM_LOG(...)
#endif
```

All `TEAM_LOG(...)` calls expand to nothing at compile time. No PSK or plaintext is emitted through these macros at the baseline. This is not an active leak.

---

## CORRECTION — Finding 1: `expandChannelPsk` Latent Buffer Overflow

**Classification revised:** Helper-level hardening only. Not production-critical at baseline.

**Evidence chain (confirmed):**

| Step | File | Line | Action |
|------|------|------|--------|
| NVS decode | `channel_persist.cpp` | 101 | `r.key_len = p[1 + kNameBytes + kKeyBytes]` — raw byte, no post-read clamp |
| Adapter normalise | `meshtastic_radio_adapter.cpp` | 730 | `normalized_len = normalizeMeshtasticChannelKeyLen(eff.key, sizeof(eff.key), eff.key_len)` — **caps to 0, 16, or 32** |
| Hash call | `meshtastic_radio_adapter.cpp` | 759 | `channelHashFromRecord(eff)` — called with normalized len |
| Overflow site | `channel_hash.cpp` | 63 | `std::memcpy(out, key, key_len)` — writes up to `key_len` bytes into `uint8_t expanded[32]` with no internal bound check |

**Production call is protected.** `normalizeMeshtasticChannelKeyLen` at `meshtastic_radio_adapter.cpp:730` is the load-bearing mitigation. Any future call to `expandChannelPsk` that bypasses this normalisation is at risk.

**Why it is still a finding:** The vulnerability lives in the helper function itself. Any new caller that directly calls `channelHashFromRecord` or `expandChannelPsk` without pre-normalising `key_len` will trigger a stack write past the 32-byte `expanded` buffer, reading past `rec.key` into adjacent struct fields. The struct field immediately following `key[32]` is `key_len` itself (`channel_record.h`), then `channel_id` — stack corruption from controlled input is achievable from a future call site.

**Unsafe simplifications rejected:** None proposed here.

**Minimal safe fix:** Add an internal clamp inside `expandChannelPsk` before the `memcpy`:

```cpp
// channel_hash.cpp (or wherever expandChannelPsk is defined)
// Before: std::memcpy(out, key, key_len);
const uint8_t safe_len = (key_len > 32u) ? 32u : key_len;
std::memcpy(out, key, safe_len);
*out_len = safe_len;
```

Also clamp in `channel_persist.cpp:101` after decode:
```cpp
r.key_len = p[1 + kNameBytes + kKeyBytes];
if (r.key_len > sizeof(r.key)) r.key_len = sizeof(r.key);  // add this line
```

**Failable test:**
```cpp
// Unit test — must not overflow
ChannelRecord rec{};
rec.key_len = 255;  // adversarial value
memset(rec.key, 0xAA, sizeof(rec.key));
uint8_t expanded[32];
uint8_t expanded_len = 0;
expandChannelPsk(rec.key, rec.key_len, expanded, &expanded_len);
assert(expanded_len <= 32);  // must clamp
```

---

## CORRECTION — Finding 2: ESP-NOW PSK Transmitted in Cleartext

**Evidence (confirmed, unchanged):**

- `firmware/c6_companion/components/tm_espnow/tm_espnow.c:114`: `peer.encrypt = false` — all ESP-NOW peers unencrypted at WiFi layer
- `modules/core_team/src/usecase/team_pairing_coordinator.cpp:394-419`: `sendKey()` sends raw `KeyPacket` containing PSK bytes over this unencrypted channel
- `modules/core_team/src/protocol/team_pairing_wire.cpp:encodeKey()`: serialises PSK as length-prefixed raw bytes

**Corrections to original report:**

| Original suggestion | Status | Reason |
|---|---|---|
| ECDH key agreement using `ed25519 key_exchange.c` | **Rejected** | Unauthenticated ECDH is susceptible to MITM — attacker intercepts both sides and presents own public keys |
| Numeric SAS comparison | **Rejected** | SAS authenticates key agreement; it does not encrypt the PSK in transit and cannot substitute for confidentiality |

**Proper remediation boundary:** This finding requires mutual authentication **plus** confidentiality at the pairing transport layer. The minimal correct approach is a passphrase-based key agreement (e.g., PAKE variant such as SPAKE2 or J-PAKE) — but this is a non-trivial protocol change requiring its own design review. A partial interim measure would be deriving a session wrapping key from a pre-shared passphrase entered out-of-band; however the existing `join_nonce_` (32-bit random) is transmitted in cleartext over ESP-NOW before the PSK is sent, so the nonce does not provide confidentiality.

**Current scope:** The finding is confirmed production-reachable (ESP-NOW is active during pairing). The `join_nonce_` provides replay differentiation between sessions but not confidentiality. Any passive 802.11 monitor-mode observer on the 2.4 GHz channel recovers the PSK.

**Failable test:** Capture ESP-NOW frames on the pairing channel with an 802.11 monitor. The `KeyPacket` payload must NOT appear in plaintext — this test would currently fail.

---

## CORRECTION — Finding 3: `handleKey` Discards Source MAC

**Evidence (confirmed):**

`modules/core_team/src/usecase/team_pairing_coordinator.cpp:285-287`:
```cpp
(void)mac;  // source MAC is discarded
```

Line 299 validates only `team_id` and `join_nonce_` (32-bit).

**Corrections to original report:**

| Original suggestion | Status | Reason |
|---|---|---|
| `if (memcmp(mac, leader_mac_, 6) != 0) return;` | **Rejected** | WiFi MAC addresses are trivially spoofable in monitor/injection mode; MAC-only gating does not defeat an attacker who can observe the leader's MAC from the beacon or a prior frame and inject a crafted `KeyPacket` |

**Finding remains valid:** The source MAC is discarded. The only validation of the `KeyPacket` sender is the `join_nonce_` (32-bit random). An adversary who observes the `JoinPacket` over unencrypted ESP-NOW recovers the nonce and team_id and can inject a `KeyPacket` with an attacker-controlled PSK. The discarded MAC is a missed opportunity, but MAC verification alone is not a sufficient fix.

**Proper remediation boundary:** Sender authentication requires cryptographic binding, not MAC comparison. This shares the same root cause as Finding 2 — the pairing transport lacks confidentiality and authentication at the protocol level.

---

## DEEPENED — Finding 4: `KeyDist` Accepts Plaintext — Unauthenticated Key Replacement

**Severity: HIGH — Production reachable**

### Confirmed code path

**`modules/core_team/src/usecase/team_service.cpp:447-466`:**

```cpp
case team::proto::TeamMgmtType::KeyDist:
{
    // NO "if (!decoded_encrypted) break;" guard here
    team::proto::TeamKeyDist msg;
    if (!team::proto::decodeTeamKeyDist(payload.data(), payload.size(), &msg))
    {
        emitError(data, TeamProtocolError::DecodeFail, ...);
        break;
    }
    logTeamKeyDist(msg, "RX");
    TeamKeyDistEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr, runtime_), msg};
    sink_.onTeamKeyDist(event);          // line 458: EventBus publish (non-blocking, deferred)
    rememberTeamMember(event.ctx.from);

    if (msg.channel_psk_len > 0 && msg.key_id != 0)
    {
        setKeysFromPsk(msg.team_id, msg.key_id,   // line 463: synchronous key overwrite
                       msg.channel_psk.data(), msg.channel_psk_len);
    }
    break;
}
```

Compare: `Kick` (line 411), `TransferLeader` (line 430), `KeyRequest` (line 470) — all have `if (!decoded_encrypted) break;`. `KeyDist` does not.

### `setKeysFromPsk` — unconditional overwrite confirmed

**`team_service.cpp:275-316`:**

```cpp
bool TeamService::setKeysFromPsk(const TeamId& team_id, uint32_t key_id,
                                 const uint8_t* psk, size_t psk_len)
{
    // ... derives 4 sub-keys from wire-supplied psk ...
    // Lines 307-312: clears member list only if team_id or key_id changes
    // Line 314: keys_ = keys;   <-- unconditional replacement
    return true;
}
```

There is **no check** that `msg.team_id == keys_.team_id` (current team). Any `team_id` value from the wire replaces the active keys.

### Sink/observer wiring trace

**`platform/esp/idf_common/src/team/idf_team_event_sinks.cpp:26-29`:**
```cpp
void IdfTeamEventBusSink::onTeamKeyDist(const ::team::TeamKeyDistEvent& event)
{
    ::sys::EventBus::publish(new ::sys::TeamKeyDistEvent(event), 0);
}
```

`sink_.onTeamKeyDist(event)` at `team_service.cpp:458` calls `IdfTeamEventBusSink::onTeamKeyDist`, which queues to `sys::EventBus`. This is a non-blocking publish: the EventBus subscribers (UI reducers, page handlers) process the event on their own scheduling turn — **after** `team_service.cpp:463` executes `setKeysFromPsk` in the same call stack.

**Consequence:** Any application-layer "passphrase sentinel" that subscribes to `sys::TeamKeyDistEvent` to validate or block the key replacement fires **after** `keys_` is already overwritten. There is no veto path between `sink_.onTeamKeyDist` and `setKeysFromPsk`.

### Attack scenario (production-reachable)

1. Attacker is on the same mesh (any Meshtastic node, monitor-mode WiFi, or forged Meshtastic node).
2. Attacker broadcasts a plaintext `TeamMgmtType::KeyDist` message with attacker-chosen `team_id`, `key_id`, and `channel_psk`.
3. Victim device (not guarded by `decoded_encrypted`) calls `setKeysFromPsk` with attacker's values.
4. All subsequent team-encrypted messages from legitimate members decrypt with the wrong key and are silently rejected.
5. Attacker can encrypt messages under the replacement key and have them accepted.

### Minimal fix

Add the encryption guard at the top of the `KeyDist` case, consistent with `Kick`, `TransferLeader`, and `KeyRequest`:

```cpp
case team::proto::TeamMgmtType::KeyDist:
{
    if (!decoded_encrypted)   // ADD THIS GUARD
    {
        break;
    }
    // ... existing decode and setKeysFromPsk ...
}
```

Additionally, add a team_id membership check inside `setKeysFromPsk` or at the call site:

```cpp
// In team_service.cpp before calling setKeysFromPsk:
if (keys_.valid && msg.team_id != keys_.team_id)
{
    break;  // reject KeyDist for a team we don't belong to
}
```

**Failable test:**
```cpp
// Send an unencrypted KeyDist with foreign team_id and attacker PSK
// Expect: setKeysFromPsk is NOT called; active keys unchanged
TeamService svc{...};
svc.setKeys(legitimate_keys);
inject_plaintext_keydist(svc, attacker_team_id, attacker_psk);
ASSERT_EQ(svc.getKeys(), legitimate_keys);  // must not change
```

---

## NEW FINDING — Finding 8: BLE Default Mode (`PAIRING_NONE`) Accepts All Writes Without MITM Protection

**Severity: MEDIUM — Production-reachable, default configuration**
**Confidence: HIGH**

### Evidence

**`firmware/c6_companion/components/tm_ble/tm_ble.c:386-401`** (`apply_security_config`):

```c
static void apply_security_config(void)
{
    if (!pairing_requires_pin())
    {
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
        ble_hs_cfg.sm_bonding = 0;
        ble_hs_cfg.sm_mitm   = 0;   // NO MITM protection
        ble_hs_cfg.sm_sc     = 0;   // NO Secure Connections
        ...
        return;
    }
    // PIN modes: sm_mitm=1, sm_sc=1
}
```

**`firmware/c6_companion/components/tm_ble/tm_ble.c:429-434`** (`connection_is_authenticated`):

```c
static bool connection_is_authenticated(uint16_t conn_handle)
{
    if (!pairing_requires_pin())
    {
        return true;   // UNCONDITIONAL TRUE in PAIRING_NONE mode
    }
    ...
    return desc.sec_state.encrypted && desc.sec_state.authenticated;
}
```

**`firmware/c6_companion/components/tm_ble/tm_ble.c:458-465`** (GATT write gate):

```c
if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
{
    if (!connection_is_authenticated(conn_handle))
    {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
```

### Analysis

When `pairing_mode == TM_C6_PAIRING_NONE` (the default per `apply_security_config`):

- NimBLE advertises with `sm_mitm=0`, `sm_sc=0`: BLE pairing may encrypt the link but does not authenticate either side — any BLE device can satisfy this.
- `connection_is_authenticated()` returns `true` for **all connections** regardless of whether the remote device authenticated.
- The GATT write gate at `tm_ble.c:458-465` consequently allows **any BLE device** that completes the (unauthenticated) pairing to write all GATT characteristics across the Meshtastic, MeshCore, and TrailMate service profiles.

This means a BLE-capable adversary in range can:
- Write arbitrary data to the Meshtastic TX characteristic (inject mesh packets)
- Write to MeshCore NUS TX (inject MeshCore commands)
- Write to TrailMate service characteristics (issue configuration or admin commands)

Without PIN mode enabled, the "authenticated" gate provides no security guarantee — it is a label without a substance check in `PAIRING_NONE` mode.

### What is correct in PIN modes

`TM_C6_PAIRING_FIXED_PIN` and `TM_C6_PAIRING_RANDOM_PIN` set `sm_mitm=1, sm_sc=1` and verify `desc.sec_state.encrypted && desc.sec_state.authenticated` — this is correct.

### Minimal fix

`connection_is_authenticated` should not return `true` unconditionally for `PAIRING_NONE`. If `PAIRING_NONE` is intended to allow open access (e.g., for trusted local use), that policy decision should be explicit and documented rather than structuring the gate as if authentication passed. The minimal safe fix is one of:

**Option A — Require PIN for write access (security-preserving):** Remove the `!pairing_requires_pin() → return true` short-circuit. Document that GATT writes require PIN pairing. This is a user-experience change.

**Option B — Explicit open policy (honest gate):** Rename `PAIRING_NONE` mode handling to clearly signal "open access" and document it as an intentional trust decision, not an authentication result. The gate logic itself must not pretend authentication occurred.

Do not combine Option B with writing sensitive configuration (channel keys, team config) through the GATT interface — those characteristics need explicit guards independent of BLE pairing state.

**Failable test:**
```
1. Configure C6 in TM_C6_PAIRING_NONE mode (default)
2. Connect from any BLE device without entering a PIN
3. Write to a TrailMate GATT write characteristic
4. Expected (failing today): INSUFFICIENT_AUTHEN returned
5. Actual: write succeeds
```

---

## Confirmed Positive Controls

| Control | Location | Evidence |
|---|---|---|
| ChaCha20-Poly1305 with RFC 8439 KAT | `idf_team_crypto.cpp:runSelfTest()` | KAT vector from RFC 8439 §2.8.2 verified at runtime |
| SHA256 KDF label separation | `idf_team_crypto.cpp:sha256Kdf` | Four sub-keys: `team_mgmt`, `team_pos`, `team_wp`, `team_chat` |
| AES-CCM constant-time compare | `mt_pki_crypto.cpp:constantTimeCompare` | `volatile` pointers, XOR-fold — correct implementation |
| `hashSharedKey` before AES-CCM | `mt_pki_crypto.cpp:hashSharedKey` | SHA256 in-place; raw DH output never used directly |
| PKI nonce non-zero guard | `mt_pki_flow.cpp:encryptWithSharedSecret` | `packet_id != 0` required; `shared_secret.size == 32` required |
| Crypto fail-safe | `mt_pki_crypto.cpp` | Returns `false` without crypto backend — no silent plaintext fallback |
| Random nonce per team message | `team_service.cpp:1100` | `runtime_.fillRandomBytes(envelope->nonce.data(), 12)` |
| KeyDist decode bounds | `team_mgmt.cpp:239` | `if (out->channel_psk_len > out->channel_psk.size()) return false` |
| Pairing wire decode bounds | `team_pairing_wire.cpp:148, 218` | Name length and PSK length capped before use |
| NVS PSK erase on leave | `idf_nvs_team_ui_snapshot_store.cpp:save()` | Erases blob when `!in.has_team_psk || !teamIdIsNonZero(in.team_id)` |

---

## Architectural Limitations (Not Speculative Fixes)

### Packet ID / Nonce Reuse (Finding 7, original report)

**Evidence:** `meshtastic_protocol_strategy.cpp:37-39`:
```cpp
return context.now_ms != 0 ? context.now_ms : 1U;
```

Same-millisecond transmissions from the same node produce identical AES-CTR nonces → keystream reuse. **Finding stands.** The originally proposed XOR counter was rejected because XOR of two values is not collision-proof (wrap-around, counter overflow). The correct fix is a monotonic per-node sequence number with no reuse on wrap. This is a protocol-level change; no safe minimal source-only patch exists without a protocol decision.

### Dedup Ring Capacity (Finding 6, original report)

**Evidence:** `mesh_dedup_service.h:kCapacity = 32`, 60s TTL, round-robin eviction.

A larger ring was rejected: **cache size is not cryptographic replay protection**. With >32 distinct (from_node, packet_id) pairs in a 60-second window, entries are silently evicted. The design limitation is architectural — proper replay protection requires a monotonic sequence number at the protocol layer, not a larger ring. Document as known architectural constraint; no safe minimal fix is available at the source level without protocol changes.

---

## Coverage Gaps and Limitations

| Area | Status | Gap |
|---|---|---|
| `sys::EventBus` scheduling model | Not fully traced | Whether EventBus dispatches synchronously or deferred affects whether any UI-layer veto could theoretically be inserted before `setKeysFromPsk` fires. Evidence suggests async (deferred), making veto impossible in practice. |
| SDIO host-link P4→C6 identity | Previously noted | `handle_hello()` accepts any HELLO with matching protocol version; no P4 identity token. No new evidence contradicts this. |
| MeshCore decode bounds (full trace) | Partial | `mc_packet_codec.h` does not exist at the searched path — MeshCore codec file not found by glob. This path remains unverified. |
| BLE MTU-sized write overflow | Not found | `mt_queue_push` checks size before copy; no confirmed overflow. |
| Radio adapter channel update callers beyond `updateChannelKeys` | Not traced | If a second call site to `channelHashFromRecord` or `expandChannelPsk` exists without normalisation, Finding 1 becomes production-critical. |
| GitNexus / diff | Unavailable | Branch `channel-key-passphrase` (HEAD `98609d5`) changes relative to baseline `e3c0fbd` not inspectable without git tooling. If that branch modifies pairing or key distribution paths, findings may change. |

---

## Priority Recommendations (Production-Reachable First)

| Priority | Finding | File | Minimal Action |
|---|---|---|---|
| **P0** | Finding 4: KeyDist plaintext key replacement | `team_service.cpp:447` | Add `if (!decoded_encrypted) break;` + `msg.team_id == keys_.team_id` check |
| **P1** | Finding 8: BLE PAIRING_NONE authentication gate is vacuous | `tm_ble.c:429-434` | Explicit policy decision: either require PIN for GATT writes or document open-access intent with honest gate |
| **P2** | Finding 2+3: ESP-NOW PSK in cleartext, no sender auth | `tm_espnow.c:114`, `team_pairing_coordinator.cpp:394` | Protocol-level design review required; no safe minimal source patch |
| **P3** | Finding 7: Packet ID time-based nonce collision | `meshtastic_protocol_strategy.cpp:37` | Protocol-level sequence number; no safe quick patch |
| **P4** | Finding 1: `expandChannelPsk` latent overflow | `channel_hash.cpp:63`, `channel_persist.cpp:101` | Internal clamp in helper + post-decode clamp in persist; helper hardening only |
| **Note** | Finding 6: Dedup ring eviction | `mesh_dedup_service.h` | Architectural constraint; document only |
