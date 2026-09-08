# Nonce repair — independent review log

Current routing: user explicitly authorizes GPT workers and Anthropic review; Spark is excluded during benchmarks.

Plan gate, round1: claude-fable-5-1, session dd76bac8-9e47-44ad-b29e-443f684b2d3c, VERDICT: REVISE. Raw artifact /home/zaidm/reviews/trailmate-nonce-crucible-9d__2zzp/plan-round-1.jsonl. Five-round limit, 600-second ceiling per call.

I'll start by mapping the review directory, then read the plan and all referenced evidence.

Next I need the adapter source/header, the domain types, the wire codec, the channel hash/key normalization, and the startup wiring. None depend on each other, so I'll request them together.

I have everything the review directory offers. `plan-round-1.jsonl` is empty (no prior-round content to weigh), and no callers of `IMeshAdapter::sendAppData` with non-zero `packet_id`, no `encodeTextMessage` body, and no legacy settings loader are in the copied evidence — I note those gaps where they matter below.

## What the plan gets right (source-backed)

- **Reserve-before-use ordering** is sound in the accepted allocator: `end_` only advances after `reserve()` returns true, and an ID is issued only when `next_ < end_ ≤ 2^32` (`persistent_packet_id_allocator.h:23-35, 39-48`). No wrap is possible; exhaustion at `end_ == kEnd` returns false without invoking the callback (`:44`), matched by the `last`/`exhausted` harness cases.
- **Uncertain commit** is modelled correctly: the NVS substitute applies `saved=pending` *and* returns an error (`check_packet_id_storage.py:60-62`), and `reserve_commit`/`init_commit` require the boot to stay latched after the fault clears (`:119-132`). A commit that actually landed can only make the persisted bound higher, so the next boot skips further — safe.
- **Single ownership**: one static owner in the native layer, scenarios run as separate processes (no reset API needed), and the adapter harness proves a second adapter instance continues the same sequence (`check_radio_packet_ids.py:127`).
- **Fail-closed init** on wrong sender/malformed/zero/overflow/unused-bytes, and init must not write when a record exists (`exhausted` asserts `commits==0`; `fresh` asserts re-init issues no new commit).
- **Rejecting disabled/out-of-range channels before encryption** also fixes a preexisting hole: today a disabled slot has `channel_psk_len_==0`/hash 0 (`meshtastic_radio_adapter.cpp:719-725`), so TX on a disabled channel goes out **in plaintext** with hash 0, and out-of-range channels silently fall back to slot 0 (`:869-872`).
- **Wire codec** needs no change: nonce = LE packet_id(8) ‖ LE from(4) ‖ zero counter (`mt_packet_wire.cpp:128-132`), and the adapter harness observes it directly (`:129-134`).

## Material flaws — resolve before dispatch

**1. Explicit caller IDs can exhaust the identity permanently (or break callers).** `sendAppData` exposes `packet_id` on the public `IMeshAdapter` interface (`meshtastic_radio_adapter.h:27-31`). The allocator's explicit path jumps `end_` to `requested+1024` and the plan states the stored high-water never resets (`allocator.h:39-47`; plan §"Prior Astra plan review"). One caller passing a random 32-bit ID (Meshtastic phone apps assign random `id`s to ToRadio packets; a BLE companion pump exists in `idf_chat_facade.cpp:232-237`) moves the counter near 2^32 and bricks TX for this node until NVS erase — which the migration doc then says requires fresh keys. Conversely, any caller with its own low/non-monotonic sequence fails forever on `requested < next_` (`allocator.h:30`). The plan says "all four direct sendEncodedPayload callers must use one allocator" but does not audit *who supplies* non-zero `packet_id` to `sendAppData`; the harness only uses 100. Evidence gap: callers are not in this directory. **Fix (simplest correct):** stop accepting caller IDs in the native adapter — always allocate and return the real ID via the output pointer so callers correlate on it; or, if an explicit ID is truly required, accept it only inside the current reserved window `[next_, end_)`. Either way, enumerate the actual callers before dispatch.

**2. Migration capture is only as good as the channel-config load, and load errors are indistinguishable from "no blob".** `NvsChannelBlobStore::load` returns 0 for both NOT_FOUND and read/decode failure (`idf_chat_factory.cpp:189-197`); `loadChannelConfigFromNvs` then falls back to migrating whatever scalars are in `config_` (`:384-393`), and the facade discards the result (`(void)loadChannelConfigFromNvs(config_)`, `idf_chat_facade.cpp:185`). If `tm_chans` is unreadable/corrupt while `tm_pkt_ids` works, `applyConfig → initializePacketIds` writes a record with **zero fingerprints**, the counter starts at 1, and the legacy keys — still present in NVS — are never blocked. That reproduces exactly the ID range (1, 2, 3…) the old firmware used every boot, i.e. the nonce reuse this repair exists to stop, and the record then prevents re-capture on later boots. The plan lists "config load errors/migration timing" only as the fact "channel configuration loads before adapter applyConfig". **Fix:** make the store distinguish NOT_FOUND from error; on error (or `channel_persist::decode` failure) do not call `initializePacketIds` — leave TX latched closed for the boot and log. Also state where the legacy `primary_key/secondary_key` scalars are populated and assert it precedes `initialize()` (not in evidence).

**3. Default-PSK capture trap (permanent LongFast lockout).** Init fingerprints every normalized 16/32-byte key. A user-pasted 16-byte key equal to the LongFast PSK bytes (`channel_hash.cpp:20-21`; common in channel exports) is fingerprinted. Slot 0 with an empty key expands to those *same bytes* (`meshtastic_radio_adapter.cpp:734-739`, `channel_hash.cpp:47-58`), so every subsequent PRIMARY send — including the periodic NodeInfo (`:263-273`) — is denied, with no recovery that keeps the counter. The plan's own posture is that default PSKs provide no confidentiality, so blocking them is pure availability loss. **Fix:** at init, skip keys whose bytes equal any short-PSK expansion (index 1..N via `expandChannelPsk`); add a harness case.

**4. Key-identity spec is under-specified where it matters.** (a) The plan says "normalized 16/32-byte keys" but does not name the function. `updateChannelKeys` uses `normalizeMeshtasticChannelKeyLen` (`chat_types.h:40-71`): a record with `key_len=16` but non-zero bytes 16..31 encrypts with 16 bytes, and `key_len=0` with non-zero bytes encrypts with 16 or 32. If init hashes `key_len` bytes or the raw 32-byte buffer, such keys are never denied. The harness has no non-canonical `key_len` case. (b) `psk_len==0` (open channel on slots 1–7, `:740-743`) is neither specified nor tested — `invalid_key` tests 17 and nullptr only; the SHA stub asserts `n==16||n==32`, so the implementation must skip hashing for 0, and the plan must say whether open-channel TX is allowed (RX already supports it, `:474-478`). **Fix:** specify "fingerprint = SHA-256 over exactly the bytes `channelKeyFor` would return for that record (normalize + expand)", allow `psk_len==0` without a fingerprint check (or explicitly forbid it), and add harness cases for both.

## Should fix (non-blocking, cheap)

- `init_read`/`wrong_sender` use `record(1)`, so an implementation that wrongly overwrote the record on a read error would be invisible by `high()`. Use `record(5000)` and assert `commits==0 && high()==5000`.
- The adapter harness's `allocatePacketId` stub ignores `psk`; assert it receives exactly `channelKeyFor(channel)`'s pointer/length so a raw-vs-expanded key bug is caught.
- Confirm `encodeTextMessage` truly ignores `msg_id` before passing 0 (`:126-127`); body not in evidence.
- Reject `node==0` at init (eFuse read is `(void)`-ignored, `:766`).
- `ensure_nvs_ready` auto-erases the whole NVS on `NO_FREE_PAGES/NEW_VERSION_FOUND` (`bsp_runtime.cpp:45-51`), silently destroying the counter and fingerprints. This is inside the documented "NVS erasure requires fresh keys" limit, but it happens without operator action — the migration doc should name this path and the firmware should log it at error level.
- Consider a per-device random salt in the fingerprint (289-byte blob). For 128-bit random keys it changes nothing; for passphrase-derived keys it removes a dictionary-attack shortcut, though ciphertext alone already permits one. Low priority.

## Documented limits / preexisting issues (not counted against the plan)

Full-backup rollback undetectability, historic removed keys, no retroactive protection, and firmware downgrade with fresh keys are honestly scoped in both documents. Team-layer ChaCha20 nonce handling and the `sent_plain` heartbeat (`idf_chat_facade.cpp:974-977`) are independent of this repair. The "start the counter at a high offset instead of rotating keys" alternative was considered and is correctly rejected: baseline already accepted arbitrary explicit IDs (`:312`), so no offset bounds historic nonces; fresh keys are the right call and are user-authorized.

## Summary

Allocator semantics, uncertain-commit latching, single ownership, blob validation and the wire codec are sound, and the two harnesses are well-designed failable gates. But the plan as written can still produce the exact failure it targets (flaw 2), can permanently exhaust or lock out an identity (flaws 1, 3), and leaves the key-identity function unspecified where the normalization rules are non-trivial (flaw 4). These are cheap to fix at the plan stage and must be fixed before workers are dispatched. Nothing here asserts tests pass; physical bench checks remain required.

VERDICT: REVISE

Astra disposition: all four material findings accepted and the plan updated. See NONCE-REPAIR-PLAN.md for exact changes and cheap follow-ups. Per-device fingerprint salt deferred with rationale. Round2 reviews this revision before dispatch.


Plan gate round2, same Fable/session: VERDICT: APPROVED. Raw artifact /home/zaidm/reviews/trailmate-nonce-crucible-9d__2zzp/plan-round-2.jsonl

I'll re-map the directory for newly supplied evidence, then read the revised plan.

All of these are independent of one another, so I'll read the revised plan, the revised API header, and every newly supplied evidence file in one batch.

The impact JSON files may enumerate additional `sendAppData`/`sendTextWithId` callers beyond the two supplied; they're independent, so I'll read them all together before reasoning.

I now have the revised plan, the new API header, and all newly supplied evidence (encoder, interface defaults, Team/pairing callers, ChatService, startup, channel codec, impact graphs). `plan-round-2.jsonl` is empty. Nothing further is needed.

## Round‑1 dispositions checked against source

**1. Explicit IDs → auto‑only.** Resolved and correctly evidenced.
- Every concrete IDF `sendAppData` caller passes `packet_id=0`: `team_service.cpp:1206, 1246` and `idf_lora_pairing_transport.cpp:126-129`. Both consume only the `bool` result, so ignoring hints cannot break correlation.
- The BLE/phone text path cannot force an ID: `MeshtasticRadioAdapter` does not override `sendTextWithId` (`meshtastic_radio_adapter.h:24-42`), so the interface default drops `forced_msg_id` (`i_mesh_adapter.h:66-72`), and `ChatService` already correlates on the *returned* `result.msg_id` (`chat_service.cpp:67-92`).
- The interface documents `packet_id` as "Optional packet id hint (0 = auto)" (`i_mesh_adapter.h:102`), so ignoring it preserves the contract.
- `encodeTextMessage` never reads `packet_id` (`mt_codec_pb.cpp:18-65`), so passing 0 is byte‑identical on the wire.

**2. Fail‑closed channel load (assignment 3).** Resolved; the Astra addendum is accurate. `channel_persist::decode` returns `count` while writing nothing when `count > max` (`channel_persist.cpp:90-106`), and the current caller then copies uninitialised `loaded[]` records for `i < n` (`idf_chat_factory.cpp:367-371`) — a second, independent reason to bound count before copying. `loadChannelConfigFromNvs` has exactly one caller (impact‑1, epistemic "exact"), so the contract change is safe. Startup confirms no on‑disk scalar loader: `config_` is `AppConfig` defaults plus protocol/region/MeshCore fields (`esp32_lvgl_startup_runtime.cpp:195-227`), `saveConfig()` is a no‑op (`idf_chat_facade.cpp:368-373`), and `MeshConfig` zeroes the scalar keys (`chat_types.h:493-494`), so "absent storage → migrate defaults" captures nothing and is correct for a device that never persisted keys. Stopping `initialize()` before `createIdfChatRuntime` means no adapter, no `initializePacketIds`, no record — the next boot retries capture with intact legacy keys. Fail‑closed ordering is right.

**3. Public short‑family exclusion.** Correct: `expandChannelPsk` with `key_len==1` sets `out[15] = default[15] + index − 1` under uint8 wrap (`channel_hash.cpp:47-58`), so all 256 expansions share the first 15 bytes of `kDefaultPskBytes` and differ only in the final byte; a 32‑byte key can never be in the family. Accidental exclusion of a real private key is a 2⁻¹²⁰ event.

**4. Normalisation/open channel.** Correct: fingerprint = `normalizeMeshtasticChannelKeyLen` (as `updateChannelKeys`, `meshtastic_radio_adapter.cpp:729-748`) → `expandChannelPsk`, which is exactly what `channelKeyFor` hands to `allocatePacketId`; `psk_len==0` skips hashing and remains an allowed open channel, matching RX (`:474-478`).

Follow‑ups (read‑failure no‑write fixtures at 5000, zero sender, key pointer/length assertion, handle close on error, erase‑path error wording) are all in the plan.

## Remaining items — none block dispatch

- **Stale API artifact.** `nonce-tasks/persistent_packet_ids.h:5-6` still declares `uint32_t requested`, contradicting assignment 1's four‑argument `allocatePacketId(node, psk, len, out)`. The plan text is authoritative and the prompt acknowledges harness staleness, but the header is the worker's handed‑in API — regenerate it before dispatch so the worker does not reintroduce a requested‑ID input.
- **Operator visibility on denial.** A legacy‑key block surfaces to `ChatService` as `MeshOperationFailure::Unknown` with `msg_id 0` (`i_mesh_adapter.h:83-84`, `chat_service.cpp:68-71`), i.e. a silent send failure. Add a rate‑limited `ESP_LOGE` at the denial and at channel‑load failure with the fresh‑key recovery wording, mirroring the accepted erase‑path change. Cheap; not a correctness issue.
- **Assignment‑3 host regression practicality.** `idf_chat_factory.cpp` pulls in `chat_service.h`, `meshcore_adapter.h`, `settings_store.h` (`:28-43`), and `settings_store::get_blob` returns only `bool` (`:130, 192`). The plan's "direct NVS status" is the right call; the worker should either use body‑extraction as `check_radio_packet_ids.py` does or move the loader into a small TU so the production body is compiled, not modelled.
- **Optional scoping.** Failing the whole facade on a corrupt `tm_chans` blob also disables MeshCore boots, which never consume these keys. Unconditional is simpler and safe; scoping to `mesh_protocol == Meshtastic` is an acceptable refinement, not a requirement.
- **Preexisting, out of scope.** `normalizeMeshtasticChannelKeyLen` turns a stored `key_len==1` record into a verbatim 16‑byte `{index,0,…}` key rather than a short‑PSK expansion (`chat_types.h:56-70`); the plan correctly mirrors whatever TX uses, which is what nonce identity requires, but the on‑air interop question is separate. The double failure notification on failed TX (`ChatService` immediate + `ChatSendResultEvent`) is baseline behaviour and harmless. Team‑layer plaintext status frames and random‑nonce AEAD are independent of this repair. Full‑backup rollback remains a documented physical limitation.

## Summary

The four material findings are closed with plan text that matches the supplied source, and the accepted allocator, uncertain‑commit latching, single ownership, blob validation and wire codec remain as assessed in round 1. Nothing here asserts that absent implementation or tests pass; physical reboot/power‑loss/RF checks remain a bench requirement, and the stale header should be regenerated as a pre‑dispatch chore.

VERDICT: APPROVED


Completed-work gate round 1: claude-fable-5-1, fresh session 758d694a-d61a-497d-92dc-2bf2e9470350, VERDICT: REVISE. Raw artifact /home/zaidm/reviews/trailmate-nonce-crucible-9d__2zzp/final-round-1.jsonl. Five rounds maximum, 600 seconds per call.

I'll start by surveying the review directory to understand what evidence is available.

Let me read the prompt, file listings, verification, and key docs in parallel.

Next I need the diff, the new production sources, the accepted allocator, and the adapter/factory/facade current state. These are all independent, so I'll read them together.

I now need the full adapter, factory, facade, normalization/expansion sources, wire codec, the two remaining scripts in full, and the build/manifest evidence. None depend on each other.

I have everything the directory offers. The one item I still need — the `settings_store::put_blob/get_blob` implementation (`platform/esp/idf_common/src/platform_ui_settings_store.cpp`, listed in the CMake source set at line 767) — is not in the review directory, and no impact graph or prior-round artifact describes it. I'll write up the review with that gap stated explicitly.

## Scope and evidence read

`final.diff`, all five files in `new-files.txt`, the current adapter `.cpp/.h`, factory/facade, `chat_types.h` normalization, `channel_hash.cpp` expansion, `mt_packet_wire.cpp`, `channel_persist.cpp`/`channel_record.h`, startup runtime, `bsp_runtime.cpp`, `i_mesh_adapter.h`, all three regression scripts, CI workflow, CMake source list, both build logs (both compile `persistent_packet_ids.cpp.obj` and reach "Project build complete"), `build-source-manifest.json`, `verification.txt`, both audit docs and the review log. Test/verification claims are treated as evidence only where I could independently confirm the behaviour in source.

## Confirmed sound against source

- **Allocator semantics / reserve-before-use.** `persistent_packet_ids.cpp:208-219` only issues an ID after `persist()` returns true; `allocator.h:23,44-46` cannot wrap or exceed `2^32`; exhaustion at `end_==kEnd` returns false with the callback uncalled. Fresh record writes high-water 1 (`:149`); reboot skips the whole reserved block.
- **Fail-closed latching.** Every open/read/size/validate/set/commit/SHA failure sets `failed=true` (`:131,138,155,164,171,198,212`), which is checked first in both entry points (`:125,188`). `state` is only replaced after a successful persist (`:216`), so an uncertain commit can never lower the persisted bound. Repeated same-node init cannot re-capture or rewrite (`:127-128`).
- **Key identity.** Capture uses `normalizeMeshtasticChannelKeyLen(key, sizeof key, key_len)` then `expandChannelPsk` (`:90-97`) — byte-identical to `updateChannelKeys` (`meshtastic_radio_adapter.cpp:763-788`) — and the adapter hands `allocatePacketId` exactly `channelKeyFor()`'s pointer/length (`:321,326`), which the harness asserts (`check_radio_packet_ids.py:84`). Public-family exclusion compares the first 15 bytes of a 16-byte key (`:62-67`), matching `expandChannelPsk`'s final-byte-only variation (`channel_hash.cpp:47-58`). Zero-length PSK skips hashing and remains an open channel (`:190-206`), matching RX (`adapter:499-511`).
- **Blob validation.** Magic, sender, `1 ≤ high ≤ 2^32`, `count ≤ 8`, zero unused tail (`:69-82`); fixed 273-byte size enforced on both the size query and the read (`:161,168`).
- **Adapter integration.** `next_packet_id_` removed; the only `transmitRadio` call is inside `sendEncodedPayload` (`:357`), which rejects out-of-range/disabled slots before key lookup (`:314-318`), ignores hints (`:324`), zeroes `out_msg_id` on every failure path (`:305-308`), and passes 0 to `encodeTextMessage` (`:126`). `MeshtasticRadioAdapter` does not override `sendTextWithId`, so BLE/ChatService text cannot force an ID (`i_mesh_adapter.h:66-72`). All concrete `sendAppData` callers pass `packet_id=0` (`team_service.cpp:1206,1246`; `idf_lora_pairing_transport.cpp:126-129`).
- **Fail-closed channel load.** `NvsChannelBlobStore::load` distinguishes NOT_FOUND from error at open and at the size query (`idf_chat_factory.cpp:198-219`), bounds `len` to `5 + count·70` with `count` in 1..8 before decoding (`:230-236`; matches `channel_persist.cpp:21-26`), and only then copies. The facade returns before `createIdfChatRuntime` in Meshtastic mode (`idf_chat_facade.cpp:185-193`). `createIdfChatRuntime` constructs exactly one adapter (`:367-375`), so a MeshCore boot never calls `initializePacketIds` and never writes an empty-fingerprint record; `applyMeshConfig` only saves the blob in Meshtastic mode (`:389-392`), so a corrupt blob is not overwritten from MeshCore; `switchMeshProtocol` is a no-op (`:444-450`), so no runtime protocol switch bypasses the guard.
- **Wire nonce** unchanged: LE packet_id(8)‖LE from(4)‖zero counter (`mt_packet_wire.cpp:128-132`); the harness observes it directly.
- **Documents** describe rollback/erase/removed-key limits honestly; no anti-rollback claim is made. The stale task header was regenerated (diff `:153-163`).

## Blocking: one evidence gap at the load/save seam

The repair's central migration guarantee depends on `NvsChannelBlobStore::load` reading **the same NVS partition** that `NvsChannelBlobStore::save` writes. `load` now uses `nvs_open("tm_chans", NVS_READONLY, …)` on the **default** partition (`idf_chat_factory.cpp:198`), while `save` still routes through `platform::ui::settings_store::put_blob` (`:247-251`), whose implementation (`platform_ui_settings_store.cpp`, CMake `:767`) is **not in the review directory**. Nothing in evidence shows whether it calls `nvs_open` or `nvs_open_from_partition(<label>, …)`.

If `settings_store` targets a named partition, then on-device: `load` returns 0 ("absent") for a blob that exists → `loadChannelConfigFromNvs` migrates the scalar defaults → `applyConfig` → `initializePacketIds` commits a record with **zero fingerprints** (`persistent_packet_ids.cpp:145-158`) → the legacy private keys are never blocked, and the record then prevents re-capture on every later boot. This is exactly the failure mode Assignment 3 exists to close, and it would additionally break channel persistence (saves land where `load` never looks). The three host harnesses cannot detect it because they stub both `nvs_open` and `put_blob` (`check_channel_config_load.py:76-106`); no hardware bench has run. Circumstantial evidence (only `nvs_flash_init()` on the default partition in `bsp_runtime.cpp:44`; the 15-char namespace/key comments; `persistent_packet_ids.cpp` using the default partition) points to the default partition, but that is inference, not evidence.

**Practical fix (either closes it):**
1. Supply `platform_ui_settings_store.cpp` showing `put_blob/get_blob` use `nvs_open(ns, …)` on the default partition; or
2. Make the pair symmetric by routing `save()` through the same direct `nvs_open("tm_chans", NVS_READWRITE)` + `nvs_set_blob/nvs_commit/nvs_close` in the same class (and extend `check_channel_config_load.py` to assert save and load hit the same namespace/key). This is simpler than depending on an out-of-file abstraction and removes the divergence risk permanently.

## Non-blocking

- `check_radio_packet_ids.py:170` omits `-Werror`, unlike the other two scripts; add it for consistency.
- `initializePacketIds` opens `tm_pkt_ids` in `NVS_READWRITE` even on the pure validation path (`:136`), which creates the namespace entry as a side effect before the record is validated. Harmless, but `NVS_READONLY` for the read and a second RW open only when writing would make the "no write on wrong sender/malformed" property structural rather than test-enforced.
- A stored record with `key_len==1` normalizes to a verbatim 16-byte `{idx,0,…}` key (`chat_types.h:56-70`) and would be fingerprinted/blocked; this matches what TX would use and was accepted as pre-existing in round 2, but bench step 1 should include such a record if any import path can produce one.
- The rate-limited denial log (`adapter:328-339`) is correct; note that a blocked legacy key still surfaces to `ChatService` as `Unknown/msg_id 0`, as previously accepted.

## Summary

No defect was found in the allocator, storage, adapter integration, loader bounds, facade guard, wire codec, or documents; every plan-gate disposition is implemented as specified and the builds compile the new source. Approval is withheld on a single item: the load path was moved to direct NVS while the save path remains behind an implementation not in evidence, and if the two disagree on partition the repair silently fails on-device in exactly the way it is meant to prevent. Supplying the `settings_store` source (or making `save()` symmetric) is a small chore and would close this.

VERDICT: REVISE

Astra disposition: accepted the missing-evidence finding. Read the native settings-store translation unit and its complete included implementation: put_blob and get_blob both use open_namespace, which calls nvs_open on the default partition; channels_v1 has no alias. Loader and saver therefore use the same partition, namespace and key. Supplied the complete implementation and API header for round 2; no code changes needed. Optional RW namespace split and warning-flag consistency are deferred because no correctness failure was identified. Existing normalization behavior and operator visibility remain documented. Clarification: the reviewer phrase "zeroes out_msg_id on every failure path" is too broad; failures before allocation leave zero, while post-allocation encode/TX failure retains the consumed ID, as intended and tested.


Completed-work gate round 2: claude-fable-5-1, same session 758d694a-d61a-497d-92dc-2bf2e9470350, VERDICT: APPROVED. Raw artifact /home/zaidm/reviews/trailmate-nonce-crucible-9d__2zzp/final-round-2.jsonl.

I'll read the three newly supplied sources, the verification JSON, and the updated review log together, then verify the partition/namespace claim directly against `NvsChannelBlobStore`.

## Round 2: closing the blocking finding against source

**What I needed next:** only the three supplied files, the hash manifest, and the appended disposition — all now read. Nothing further is outstanding.

### Verification of the load/save seam

The supplied `settings_store_impl.h` (included verbatim by `platform_ui_settings_store.cpp:1`) confirms the claim directly:

- `put_blob(ns, key, data, len)` → `resolve_storage_key(key)` (`:346`): the alias table (`:23-33`) contains no entry for `channels_v1`, so `storage_key == "channels_v1"` (11 chars, passes the 15-char check at `:86-89`).
- `open_namespace(ns, false, &handle)` (`:353`) → `nvs_open(ns, NVS_READWRITE, handle)` (`:116`). Plain `nvs_open`, i.e. the **default** NVS partition; there is no `nvs_open_from_partition` or `nvs_flash_init_partition` anywhere in this file or in the rest of the evidence (round-1 grep).
- `nvs_set_blob(handle, "channels_v1", data, len)` then `nvs_commit`/`nvs_close` (`:380-388`).

`NvsChannelBlobStore::save` calls `put_blob("tm_chans", "channels_v1", …)` (`idf_chat_factory.cpp:250-251`); `NvsChannelBlobStore::load` calls `nvs_open("tm_chans", NVS_READONLY, …)` and `nvs_get_blob(handle, "channels_v1", …)` (`:198, 209, 223`). Same partition, same namespace, same key. The hypothetical divergence that could have produced an empty-fingerprint record on an upgraded device is ruled out by source, not inference. `persistent_packet_ids.cpp` likewise uses plain `nvs_open` (`:112, 136`), so all three stores share the default partition initialised by `nvs_flash_init()` in `bsp_runtime.cpp:44`.

One consistency detail also checks out: `put_blob` with `len==0` erases the key (`:360-378`), which `load` would then correctly classify as *absent* (`:210-214`); `saveChannelConfigToNvs` always passes `kMaxChannels` (`:450`), so that path is never taken in production.

**Finding closed.** Rewriting `save()` was offered only as an alternative to supplying evidence; with the evidence supplied, it would duplicate a working implementation without fixing a demonstrated defect. I agree with the disposition.

### Other checks this round

- **Byte-identical claim:** `nonce-verification.json` hashes for all ten changed production files match `build-source-manifest.json` from round 1 exactly, and the reread `idf_chat_factory.cpp`/`persistent_packet_ids.cpp` bodies are unchanged. All round-1 source-backed conclusions therefore stand without re-derivation.
- **Wording correction accepted:** my round-1 phrase "zeroes `out_msg_id` on every failure path" was imprecise. `sendEncodedPayload` zeroes it on entry and on pre-allocation failure (`adapter:305-308, 340`), then sets it to the allocated ID before encode/TX (`:342-345`), so a failed encode/TX retains the consumed ID — exactly what the plan ("a failed transmission consumes its allocated ID") requires and what `check_radio_packet_ids.py:124-125` asserts (`id==3` after a failed board TX). That is correct behaviour, not a defect.
- **Deferred non-blocking items** (RO/RW open split in `initializePacketIds`, `-Werror` in the adapter harness): neither is a correctness flaw; the no-write-on-malformed/wrong-sender property is enforced by the storage harness (`set_calls==0 && commits==0` in `wrong_sender`, `init_read*`), and the adapter harness compiles cleanly under `-Wall -Wextra` in CI. Acceptable to defer.
- **Documents:** the appended disposition records the evidence accurately and does not claim anti-rollback protection, hardware verification, or cryptographic proof; the `limitations` block in `nonce-verification.json` states the accepted limits plainly.

### Remaining scope statement (unchanged)

No hardware flash, physical NVS power-loss, or RF interoperability test has been performed; those remain bench requirements per `BENCH_TEST_CHECKLIST.md`. Full old-NVS rollback and historic removed-key reuse are undetectable by design and correctly documented as requiring fresh private keys. Team-layer issues are separately open and untouched. This approval is reviewer approval of the completed work against the approved plan; commit/push authorization remains the user's, which the prompt states has been given.

VERDICT: APPROVED

Astra acceptance: read the entire critique, independently confirmed the default-partition save/load chain, and verified production/test files match the reviewed packet. All ten production hashes match both firmware builds. Host acceptance checks passed before review; no implementation changed during review. User authorization for commit/push remains in force. Physical bench checks and separate Team repairs remain outstanding.
