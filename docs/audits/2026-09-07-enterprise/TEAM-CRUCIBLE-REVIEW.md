# Team containment independent review

Plan round 1: claude-fable-5-1, session 016cc7a5-4c0a-4265-86bd-c406afcdcd06, REVISE. Raw artifact /home/zaidm/reviews/trailmate-team-crucible-11x4s6nt/plan-round-1.jsonl. Five-round cap; 600-second ceiling per call.

I'll start by surveying the review directory to find PLAN.md and the supplied evidence files.

I need PLAN.md, the remaining ~39 files (tests etc.), and the core service/mgmt sources. Let me fetch those in parallel.

I've now read the plan, the service, pairing coordinator, kick/deferred/UI handlers, their tests, and the existing reproduction; I have enough evidence to write the review.

# Plan-gate review: PLAN.md — legacy Team KeyDist containment

## What I verified against source

The plan's factual claims hold up against the supplied code:

- `TeamService::processIncoming` (`team_service.cpp:447-467`) decodes `KeyDist` from **either** the encrypted branch or the plaintext fallback (`:380-405`), emits `onTeamKeyDist`, mutates the roster via `rememberTeamMember`, and calls `setKeysFromPsk` — with no provenance check beyond the `team_id/key_id` envelope match, which the plaintext branch bypasses entirely. `repro_plain_keydist.cpp` demonstrates exactly this (4 derivations from a foreign-team plaintext packet).
- `sendKeyDist` encrypts under the *current* (old) group key (`:685-702`); `sendKeyDistPlain` sends the PSK unencrypted (`:704-721`). Both are reachable from UI: kick-confirm (`team_page_kick_confirm_action.cpp:139`), key-request response (`team_page_key_request_action.cpp:58`), and deferred retries (`team_page_deferred_dispatch.cpp:137`).
- `confirmKick` switches local keys via `reduceKickConfirmed` + `setKeysFromPsk` (`:157-176`) regardless of send outcome, and `handle_kick_confirm` (`team_page_components.cpp:1583-1601`) unconditionally applies state, persists, and navigates.
- Pairing (`TeamPairingCoordinator::handleKey`) goes through a **separate** sink method `onTeamPairingKeyDist`, not `ITeamEventSink::onTeamKeyDist`, so refusing TEAM_MGMT ingress does not affect the ESP-NOW pairing path. The plan's out-of-scope claim is consistent with the code.
- Explicit local `setKeysFromPsk` (create-team, pairing-KeyDist → `TeamPageEventEffectSink::applyEffects:64-71`) is untouched by the plan. Key/roster persistence for legitimate paths is preserved.

The scope is coherent and the containment closes the described management path. The findings below are gaps, not scope objections.

## Findings

### 1. Missed caller: `TeamPageKeyRequestAction` / `handle_team_key_request` (medium)

The plan names the kick action, deferred dispatch, and components handlers, but not `team_page_key_request_action.cpp`. It copies the live PSK into a `TeamKeyDist` (`:50-55`) and calls `runtime.sendKeyDist`. With service-side refusal the packet won't transmit, but:

- The plan applies a "no read/copy of PSK before calling ports" rule to the deferred queue yet is silent on this action doing the same thing.
- Its ingress trigger (`onTeamKeyRequest`) is dropped by the plan, so in production this becomes dead code — but `test_team_page_key_request_action.cpp::testLeaderRespondsWithKeyDist` (fake controller returns `true`) will still pass and continue to assert that the leader "responds with KeyDist". That's a green test documenting behaviour the plan has just declared insecure.
- `handle_team_key_request` (`components.cpp:1343-1349`) shows **"Sent team keys"** and enqueues a retry on `sent_keydist == true`. Only unreachable because ingress is dropped, not because the UI is correct.

**Fix:** Add this action to assignment 2: refuse before building the KeyDist (return a `SecurityUnavailable` failure), update the test to assert zero `sendKeyDist` calls and no PSK copy, and leave `handle_team_key_request` unable to reach the "Sent team keys" branch.

### 2. Member-side recovery UI still steers users into a refused path (medium)

For non-leaders, `waiting_new_keys` becomes true on `KeyMismatch`/`DecryptFail` (`event_reducer.cpp:167-173`) and the status page renders **"Waiting for new keys..."** plus a **"Request Keys"** button (`lvgl_renderer.cpp:401-430`). After this patch that button can never succeed. The plan only maps the *failure* wording; it doesn't address that the persistent label promises keys that cannot arrive via any legacy path, nor that `management_actions_enabled` (`read_model.cpp:150-155`) implicitly relies on the same recovery.

**Fix:** Either state explicitly that the "Waiting for new keys..." label and "Request Keys" button remain and only the tap result changes (accepted UX debt, called out in operator notes), or include a read-model/renderer tweak in assignment 2. Right now it's unstated.

### 3. Production `handle_kick_confirm` guard has no test path (medium)

The plan says "Production handler guard must prevent persistence/navigation on rejected removal", but no test covers `team_page_components.cpp` (grep of `ui_shared/tests` finds none), and the handler depends on LVGL/app facades. The kick-confirm test only exercises `confirmKick`. If `effects.accepted == false` is used as the guard, note that invalid selection also yields `accepted == false` silently today (`:94-101`); the guard must distinguish "silently ignore" from "refuse with message" or the invalid-selection case will show a security notice.

**Fix:** Specify the discriminator (e.g., a distinct `TeamPageKickConfirmFailureKind::SecurityUnavailable` checked by the handler), and state that the handler is verified by the Astra integrated-simulator run since it is untestable on host.

### 4. `SendError` enum extension — confirm no `-Werror=switch` exposure (low)

Only one `switch` over `SendError` exists in readable code (`components.cpp:265`, has `default:`). The platform tree (`platform/esp/arduino_common/team/*`) is **not supplied**, so I cannot confirm there is no exhaustive switch there. The plan's GitNexus upstream check should explicitly cover this.

### 5. `test_team_page_runtime_port.cpp` will keep asserting positive `sendKeyDist`/`sendKeyRequest` delegation (low)

`testPortDelegatesSendsAndErrors` (`:231-242`) asserts the port forwards KeyDist/KeyRequest and the fake returns `true`. Since the port is a pure pass-through and the plan keeps signatures, this still passes and is technically correct, but it's another green test that reads as "KeyDist works". Worth a comment or a rename in assignment 2's "affected existing tests".

### 6. Evidence gap — platform sinks not readable

The ESP event-bus bridges that implement `ITeamEventSink` and `ITeamPairingEventSink` live outside the supplied tree. I could not verify whether a platform sink independently calls `TeamController::setKeysFromPsk` or persists to NVS on `onTeamKeyDist`. The plan's approach (drop at `TeamService` before the sink is invoked) makes this moot **for the legacy path**, so this is not blocking, but the plan should state that Astra's trace covers those files.

## Verdict rationale

The plan is well-bounded, honest about what it does not fix, and its containment design is sound against the code. But it omits one live KeyDist producer (`TeamPageKeyRequestAction`), leaves an unstated UX contradiction on the member side, and specifies a production handler guard without a discriminator or verification path. These are cheap to add before workers start and would otherwise surface as rework at the post-implementation review.

VERDICT: REVISE

Astra disposition: accepted findings; exact changes in TEAM-CONTAINMENT-PLAN.md round-1 dispositions. Tests will exercise actual handler with substitutes; simulator build is not a UI interaction proof.

Plan round 2: same Fable/session, APPROVED. Raw artifact /home/zaidm/reviews/trailmate-team-crucible-11x4s6nt/plan-round-2.jsonl

I'll re-read the revised PLAN.md and locate the newly supplied hostlink/Arduino/IDF sources.

I have what I need; no further reads required.

# Round-2 plan-gate review: PLAN.md with round-1 dispositions

## Round-1 findings — verified closed against the revised plan and new sources

| # | Round-1 finding | Disposition | Verified |
|---|---|---|---|
| 1 | `TeamPageKeyRequestAction` missed | Added to UI assignment; refuse before PSK copy; `TeamPageRequestKeysAction` also added with `accepted=true/send_failed=true/error=SecurityUnavailable`, no controller call | Yes — makes the fake-controller tests truthful; `handle_team_key_request`'s "Sent team keys" branch becomes unreachable by construction, not just by ingress drop |
| 2 | Member recovery UI steers into refused path | Labels replaced ("Key recovery unavailable", "Key Recovery"), "Security: OK" → neutral "Key round"; Pair Member not presented as secure | Yes — no test asserts those strings (`ui_shared/tests` grep), `test_team_page_read_model.cpp:102/105` only checks `management_actions_enabled`, which is untouched |
| 3 | Handler guard had no discriminator/test path | Uses existing `failures` vector: empty = invalid/silent, `SendFailedDetail/SecurityUnavailable` = blocked/message; returns before `apply_command_state_to_page`/`save_state_to_store`/`nav_reset`; host harness compiles extracted handler body | Yes — no new enum, reuses existing shape |
| 4 | Unread platform `SendError` switches | `hostlink_service.cpp:158-175` `map_team_send_error` has `default:` → `Internal`; plan maps `SecurityUnavailable` → `ErrorCode::Unsupported`, enumerator appended | Yes — confirmed the switch, and that hostlink `CMD_TX_APP_DATA` (`:236-256`) is a host-driven `onKeyDist/onKeyDistPlain/onKeyRequest` caller that the service-level refusal now covers with a sane ACK code |
| 5 | Runtime-port test reads as "KeyDist works" | Comment added | Yes |
| 6 | Platform sinks unreadable | Supplied. Arduino `TeamEventBusSink` and IDF `IdfTeamEventBusSink` are pure EventBus forwarders (no independent `setKeysFromPsk`); pairing enters via `onTeamPairingKeyDist` → same `sys::TeamKeyDistEvent` shape | Yes — drop-before-sink is sufficient for TEAM_MGMT; pairing correctly left open |

## Additional checks on the newly supplied sources

- **IDF boot key restore** (`idf_chat_facade.cpp:295-306`) uses explicit `TeamController::setKeysFromPsk` from the NVS snapshot — preserved by the plan's "explicit local setKeysFromPsk remains supported". Key/roster persistence across reboot is not broken.
- **IDF LoRa pairing** runs on `TEAM_PAIR_APP` via the unhandled-app-data observer (`:319, :330-345`) and `IdfLoraTeamPairingTransport` (`idf_lora_pairing_transport.cpp:126`) — never `TEAM_MGMT_APP`. Refusing TEAM_MGMT `KeyDist` ingress does not break pairing. The "no passphrase" clear-PSK branch (`idf_lora_pairing_service.cpp:153-157, :265-271`) is the open pairing vulnerability the plan explicitly disclaims.
- **Hostlink bridge** (`hostlink_bridge_radio.cpp:318-344`) re-serializes received `TeamKeyDist` events to the USB host; with ingress refused this only fires for pairing-sourced events. The plan's "no hostlink confidentiality/authentication claim" is the correct disclaimer.
- No production `SecurityUnavailable` symbol exists yet (only planning docs) — consistent with "no production changes or worker dispatch yet".

## Remaining non-blocking notes (not material flaws)

1. **Harness mechanism should be stated concretely.** "Compiling the actual extracted handler body" is the right intent; to prevent it degrading into a copied body, the plan should say the handler is moved into a single TU/inline unit (with injected persistence/nav/notify callbacks) that both `team_page_components.cpp` and the harness compile. One sentence suffices.
2. **Invalid-selection behaviour change.** Returning on `!accepted` also drops the previous `nav_reset(StatusInTeam)` for the invalid-selection edge case, leaving the user on the confirm page. Practically unreachable (the page requires a selection), but the harness should assert this deliberately rather than by accident.
3. **Worker ordering for the shared enumerator.** UI worker tests reference `SendError::SecurityUnavailable`, which lives in `team_service.h` owned by worker 1. Since worktrees must not touch each other's files, either the parent pre-lands the appended enumerator or worker 2's tests cannot compile until merge. Worth stating explicitly.
4. `ErrorCode::Unsupported` now covers both `UnsupportedByProtocol` and `SecurityUnavailable` on the hostlink wire; acceptable given "no wire schema change", but operator notes should mention it.

None of these affect the security containment, persistence, or the truthfulness of the UI outcome, and each is a one-line clarification the parent can apply at dispatch.

VERDICT: APPROVED

Astra acceptance: independent source trace confirms bounded plan and existing user authority. Handler harness extracts the live source by balanced braces at test runtime, as existing photo/radio checks do; it will not embed a copied body or introduce a new production abstraction just for tests. Invalid-selection early return is intentional and checked. Parent will append the shared enumerator in main and copy exactly that header to both worktrees before dispatch; workers do not own that parent-provided header. HIGH class blast radius (18 imports; 58 total dependants) was reported to the user before edits; runtime effects remain checked manually and by integrated builds.


Completed-work gate round 1: claude-fable-5-1, fresh session d25158de-59b2-41a8-9ff8-fb8f1ee31b6b, APPROVED. Raw artifact /home/zaidm/reviews/trailmate-team-crucible-11x4s6nt/final-round-1.jsonl. Five rounds maximum, 600 seconds per call.

I'll start by mapping the review package.

Let me read the gate metadata, the plan/review docs, and the remaining file list all at once.

Now the diff itself, plus the new core test and the UI check script (all independent).

Two last checks before I write up: whether any other UI path still performs key rotation via the command reducer, and whether hostlink logs host-supplied KeyDist payloads before the service refuses.

# Completed-work review: Team legacy KeyDist/KeyRequest containment (vs bebf9a8)

## Evidence examined

`final.diff` (1544 lines, 20 files — matches `final-detect-changes.txt` and the 9 production hashes in `build-source-manifest.json` / `team-containment-verification.json`), all 7 files in `new-files.txt`, the current production sources they touch, both plan-gate documents, `ctest.log` (72/72, containment test is #26), `baseline-regression.txt`, `tft-build.log`/`amoled-build.log` (both "Project build complete"), and the platform sinks/hostlink sources. I could not run anything; assessments below are from reading.

## Scope claims verified against source

| Claim | Where | Result |
|---|---|---|
| Core ingress refuses KeyDist/KeyRequest before decode/sink/roster/key install, plaintext or group‑encrypted | `team_service.cpp:427-433` — both cases fall to a bare `break` before `decodeTeamKeyDist`, `sink_.onTeamKeyDist/onTeamKeyRequest`, `rememberTeamMember`, `setKeysFromPsk` | Holds. Observers still called (`:303-309`). |
| All three sends refuse before encoding/logging/crypto/mesh | `team_service.cpp:627-649` — set `SecurityUnavailable`, return false; `logTeamKeyDist`/`logTeamKeyRequest` (the PSK‑hex loggers) deleted outright | Holds. |
| Hostlink maps refusal to existing `Unsupported`; no wire change | `hostlink_service.cpp:168-170`; `send_team_mgmt_wire` → `controller->onKeyDist*` → refused; no payload logging in that file | Holds. |
| Kick refuses before state/key/RNG/reducer/queue/port effects | `team_page_kick_confirm_action.cpp:23-52` — only reads `self_is_leader` and index; all rotation/fan‑out helpers removed | Holds. Test asserts `controller.calls==0 && random.calls==0 && deferred.calls==0` and state unchanged, with and without runtime. |
| Handler reports then returns before apply/save/nav; invalid silent | `team_page_components.cpp:1602-1609`; `kick_confirm_failure_action_text` yields `"Kick"` → `"Kick: secure removal unavailable"` (`:283-285, :300`) | Holds; harness compiles the live bodies and the guard‑removal mutation must fail. |
| Both recovery actions refuse independently of service | `team_page_key_request_action.cpp:44-48` (no PSK copy, no port call); `team_page_request_keys_action.cpp:25-28`; `handle_request_keydist` shows the notice (`:1645-1648`); `"Sent team keys"` branch (`:1348`) unreachable by construction | Holds. |
| Stale KeyDist retries clear without PSK read; status retries preserved | `team_page_deferred_dispatch.cpp:99-120` vs unchanged `processStatusBroadcasts` | Holds; test checks `team_psk` untouched and `keydist_send_count==0`. |
| Neutral labels | `team_page_lvgl_renderer.cpp:384, 395, 403, 430` | Holds. |
| Enum appended, no exhaustiveness break | `team_service.h:32-41`; `notify_send_failed_detail` has an explicit case | Holds. |
| Local install / boot restore / pairing untouched | `setKeysFromPsk` unchanged (`:255-296`); `idf_chat_facade.cpp:300`; pairing enters via `onTeamPairingKeyDist` → `reduceKeyDist` (`event_reducer.cpp:333`) | Holds. |

Core regression test quality: it exercises plaintext same/foreign team, encrypted same/foreign/mismatched‑inner, malformed payloads, duplicates, and no‑keys, asserting zero derivations and zero sink events. The "original key remains selected / no roster‑added recipient" claim is real but implicit: `FakeMesh` sets `supports_broadcast_appdata=false`, so `sendChat(dest=0)` fans out over `team_member_ids_` (`team_service.cpp:1119-1153`); `sent_destinations.back()==0` therefore proves no refused packet reached `rememberTeamMember`, and `last_encrypt_key == expectedDerived(psk,'c')` proves the original key. `baseline-regression.txt` shows the old body failing on the very first `derive_count == 0` assertion, confirming the test discriminates.

## Findings

No material defects found within the stated scope. Non‑blocking notes:

1. **Implicit roster assertion (test readability, low).** The "no roster‑added recipient" property in `test_team_key_distribution_containment.cpp:315-317` depends on the fake's `supports_broadcast_appdata=false` fan‑out behaviour. A one‑line comment (or asserting `mesh.send_count` equals exactly 1 for the chat send) would keep a future fake change from silently weakening it.

2. **`confirmKeyDist` lost its only direct test (low).** The rewritten deferred‑dispatch test no longer calls `confirmKeyDist`; it is still invoked in production from `TeamPageEventEffectSink::applyEffects:35-40` on `keydist_confirmed`. Trivial function; only worth a line in the existing test.

3. **Firmware warnings are pre‑existing.** `mgmtTypeName/teamPortName/teamErrorName` "defined but not used" in both IDF logs come from `TEAM_LOG_ENABLE 0`; the diff does not introduce them.

4. **Residual behaviour worth naming in operator notes (out of scope, not a defect of this patch).** Plaintext `Status` ingress is still accepted (`team_service.cpp:434-447` has no `decoded_encrypted` guard) and `reduceStatus` (`event_reducer.cpp:201-209`) sets `waiting_new_keys=true` on any higher `key_id`. With recovery now refused, a forged Status pins a member at "Key recovery unavailable" with management disabled until re‑pair. This was already true at bebf9a8 and is covered by "ordinary message paths remain", but it is the most likely field symptom to be misattributed to this containment.

5. **Evidence gap (non‑blocking).** The Linux platform sink (`platform/linux/common/src/app/linux_app_services.cpp`) was not supplied; the IDF sink comment states it is a direct port, and drop‑at‑service makes any pure forwarder moot for TEAM_MGMT. The 72‑test simulator run passed with it linked.

6. **Docs.** `TEAM-CONTAINMENT-STATUS.md` / `AUDIT.md` say "final review pending"; the parent should update those after recording this gate. The `ErrorCode::Unsupported` dual mapping is documented in STATUS as intended.

## Assessment

The implementation is the minimal correct shape: refusal at the service boundary plus refusal at every UI producer, with retained signatures, no new abstractions, no fake success paths, and truthful tests wired into the existing CTest/CI. Documentation stays inside the authorized bounded claim and explicitly keeps pairing, leader authentication, replay and rotation open. Reviewer approval here does not extend user authorization beyond the stated implementation/commit/push scope.

VERDICT: APPROVED

Astra disposition: read the entire critique and independently confirmed final production/test state matches the reviewed packet and both builds. No implementation changes after review. Optional test readability and coverage of the now-unused key retry confirmation are deferred; existing tests discriminate the actual containment failures. Record unauthenticated Status as an open protocol risk, but do not adopt the reviewer's unproven "until re-pair" recovery claim: reduceStatus can clear waiting_new_keys when a later status reports the current key ID. Pairing is not a secure workaround. No hardware validation or full Team security claim. Existing user commit/push authorization confirmed.
