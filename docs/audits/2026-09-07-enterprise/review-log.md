# Trail Mate audit review log
Scope: plan, max 5 rounds. Baseline e3c0fbd.

Round 1: plan-round-1.jsonl
I have everything the readable directory offers: `PLAN.md` (8 lines), `review-log.md` (header only), `plan-prompt.txt` (identical to the prompt), and `plan-round-1.jsonl` (empty). There is no repo checkout under `/tmp/trailmate-audit-20260907`, so the baseline commit, branch, and host test inventory cannot be inspected here; I note that gap below and review the plan against the evidence supplied.

## Assessment

**What is sound.** The plan matches user scope (audit + fixes + updates + recommendations; Sonnet workers; Crucible stays Fable; native OAuth only). Safety rails are correct and specific: no push/deploy/flash, no SD overwrite, no medical-content change, captions preserved, no blind dependency bumps, ≤5 external fetches, TFT compile before firmware commits, config regen diff only for authorized changes. Three-domain split, coverage/gap reporting, "no speculative CVE", "no perf claim without measurement", and the fixed/confirmed-open/unverified triage are all appropriate. Read-only Phase A with restricted tools and no recursive delegation is the right shape.

## Material flaws

**1. "Retain uncommitted fixes" contradicts the owner's standing rules and risks silent loss (PLAN.md:7).**
MAINTAINERS §3 (lines 83–84): "Commit incrementally … never let uncommitted work pile up. Goalkeeper-style tools revert to the last commit." §9.1 requires TFT compile before commit. With no IDF present (PLAN.md:3), both rules cannot be met, and the plan resolves it the unsafe way — leaving work in the tree where a revert tool destroys it. Fix: when the toolchain is absent, emit fixes as `git format-patch` files (and/or an explicitly named `audit/2026-09-07-unverified` branch with "FIRMWARE UNVERIFIED — no IDF" in each message) stored under the audit output dir; never leave firmware edits uncommitted in the main worktree.

**2. Baseline identity is under-specified (PLAN.md:1).**
The plan says "baseline e3c0fbd; clean checkout" but names no branch. MAINTAINERS says all current work lives on `channel-key-passphrase` (line 78) and lists HEAD as `98609d5` (line 450); `e3c0fbd` appears nowhere in the supplied evidence and no repo is available to me to confirm it descends from `98609d5`. If the checkout is on `main`/upstream, the audit targets the wrong code and the "findings trace to actual baseline" acceptance check is hollow. Fix: record in the plan and review log the output of `git rev-parse HEAD`, `git branch --show-current`, `git status --porcelain` (must be empty), and `git merge-base --is-ancestor 98609d5 HEAD`.

**3. "Independently reproduce load-bearing findings" is undefined without hardware or IDF (PLAN.md:6).**
Runtime reproduction is impossible for P4-only paths (`platform/esp/**`, `apps/esp32_lvgl/**`). Fix: define reproduction as (a) Astra's own code-trace from the trigger to the sink with file/line, or (b) host-build/test reproduction only when the file is in the Linux compile set (verify via `compile_commands.json` from `builds/linux`). Report which of (a)/(b) applies per finding; never label (a) as "reproduced at runtime."

**4. Local GitNexus indexing is neither permitted nor forbidden (PLAN.md:3,5,6).**
The plan blocks symbol edits until impact tooling exists, tasks Astra with finding the CLI, and forbids "installations or global configuration changes" — but running `gitnexus analyze` creates a repo-local `.gitnexus/` index, which is neither. This ambiguity can either stall the whole fix path or lead to an unauthorized-looking write. Fix: state explicitly that local indexing in the audit worktree is permitted if the CLI already exists, that `.gitnexus/` must not be committed, and that if the CLI is absent Phase B is recommendations/patches only (no worktrees, no Sonnet implementers). Given the plan already knows both IDF and GitNexus are absent, make that the pre-declared expected path with a single decision point after Astra's tooling check, rather than leaving it to runtime judgment.

**5. Known-open items and in-repo companion docs aren't pre-seeded for workers (PLAN.md:4).**
MAINTAINERS already records open items: Team PSK in the clear and no NVS key persistence (lines 215–217), walkie blocking `startTransmit` (218), tab5/tdeck/tlora_pager pre-existing build breaks (119–125), un-done map follow-ups (428), and `MAP_UX_ROADMAP.md §3` code-review follow-ups (300). Also a real prior crash class: corrupt `LASTFIX.DAT` → ±Inf → crash loop (209–211). Fix: give each worker MAINTAINERS.md, `BENCH_TEST_CHECKLIST.md`, `MAP_UX_ROADMAP.md`, `AGENTS.md` as required reading; list the known-open items so they're classified "confirmed-open (previously known)"; explicitly put all SD-sourced parsers (`index.tsv`, `regions.tsv`, `CREDITS.tsv`, `waypoints.tsv`, `LASTFIX.DAT`, binfonts, translate TSVs) in the security/runtime scope; declare the boot launcher/partition layout out of scope (MAINTAINERS lines 32–36).

**6. Conductor is requested by the user but never assigned (PLAN.md:2 vs 6).**
Either state that Conductor manages the Phase B worktrees, or state that Conductor is not invoked because Phase B yields no implementations under current tooling. Silence on an explicitly requested tool is a scope gap.

## Minor fixes
- Pin the exact Sonnet model ID string in the plan so "pinned model" is auditable (PLAN.md:4).
- Host tests: the Linux entrypoint has `include(CTest)` but no visible `add_test`; GTK/SDL deps are unknown on this host. Have Astra run `ctest -N` after configure and disable unbuildable shells via the existing `TRAIL_MATE_BUILD_LINUX_*` options; record exactly which shells/tests ran. Do not present a Linux pass as firmware verification.
- If the delivery worker/Astra runs `build_sd_content.py`, pass an explicit temp staging root (default is `C:\osm-tiles\sd-staging`, MAINTAINERS line 161) and only if Pillow/node are already present.
- Name the output locations (worker reports, consolidated report, patches) under `/tmp/trailmate-audit-20260907/` so the final Crucible pass has fixed artifacts to review.

## Evidence gap
No repo checkout is readable from this location, so I could not verify commit `e3c0fbd`, the branch, or the host test set; item 2 above is the concrete remedy.

VERDICT: REVISE

Astra disposition: accepted 1–5 and minor items with clarifications in PLAN.md. Finding 6 clarified: Astra is Conductor, not a separate tool. Verified baseline branch main descends from 98609d5.

Round 2: plan-round-2.jsonl
I now have what I need: the revised plan on disk matches the supplied text, the review log records round 1 and Astra's disposition, and the build artifacts let me check the new host-baseline claims directly.

## Verification of round-1 dispositions

| Round-1 finding | Revision | Evidence |
|---|---|---|
| 1. Uncommitted fixes in main checkout | PLAN.md:7 now: "written recommendations only; no unverified firmware edits in the main checkout"; :15 "no implementation workers/worktrees without impact tooling" | Resolved |
| 2. Baseline identity | PLAN.md:11 records branch, full SHA, empty porcelain, `merge-base --is-ancestor 98609d5 HEAD` exit 0 | Resolved; captured output is the appropriate evidence for a planning gate |
| 3. "Reproduce" undefined | PLAN.md:14 three-way classification, no conflation | Resolved |
| 4. GitNexus local index ambiguity | PLAN.md:15 explicit: permitted if CLI exists, never committed; expected path = recommendations only | Resolved |
| 5. Seeded known items / docs / SD parsers / launcher out of scope | PLAN.md:12–13 | Resolved; "verify current code before classifying" is the right guard |
| 6. Conductor | PLAN.md:12 Astra performs the role | Acceptable given user instruction as described |
| Minor: model pin, ctest enumeration, pipeline guard, output paths | PLAN.md:12,16,17 | Resolved |

**Host baseline claims checked against artifacts** (`/tmp/trailmate-audit-20260907/build`):
- `CMakeCache.txt`: `CMAKE_GENERATOR=Unix Makefiles`, `SIM_SHELL=ON`, `UCONSOLE_GTK=OFF`, `CARDPUTER_ZERO=OFF`, `BUILD_TESTING=ON` — matches PLAN.md:16.
- `LastTest.log`: 69 tests; `LastTestsFailed.log` lists only `15:trailmate_phone_core_smoke`; log line 213 "Unable to find executable" — consistent with a link failure and the 68/69 claim.
- Build dir is outside the repo, so the "clean checkout" claim is not contradicted by the build.

## Remaining non-blocking notes

1. **Inline "tiny non-symbol build-source-list fixes" (PLAN.md:15).** Specify that Astra applies these in a throwaway worktree or reverts after capturing the diff, so the main checkout stays at the clean baseline; if the touched CMake list is shared with ESP component builds, label it firmware-unverified like any other patch.
2. **`compile_commands.json` not yet exported.** `CMAKE_EXPORT_COMPILE_COMMANDS` is empty in the cache and no `compile_commands.json` exists; the reconfigure for host-membership checks (PLAN.md:14) must pass `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` in this same build dir.
3. **Seed the `phone_core_smoke` link break** (missing `channel_hash.cpp`) as a baseline-open delivery finding so it's classified consistently with the other pre-existing breaks, and note that MAINTAINERS §3's branch name is stale relative to the user-selected `main` — a maintainability item for the report.
4. **Docs copy to `docs/audits` (PLAN.md:17).** Since `detect_changes` gates all commits and GitNexus is absent, that copy would sit uncommitted; either state a docs-only exemption from the symbol-impact gate or deliver it as a recommendation. Optional, as written.

None of these change scope, safety, or the acceptance criteria; they are execution clarifications Astra can apply during Phase A/B without a further plan round.

VERDICT: APPROVED

Astra accepts approved plan under existing user authorization. Inline CMake fix will be tested in isolated worktree and delivered as verified patch. compile_commands export completed. Parallel read-only workers pinned claude-sonnet-4-6.

Execution refinement: Astra applies tiny non-symbol CI token-default hardening inline in isolated worktree under user security-fix authorization. Four permission mappings only, no worker implementation dispatch; YAML before/after semantic equivalence except permissions verified. Release job explicit write scope preserved. Included in final-state review; no firmware config/symbol edits.

PAUSED: user reported Anthropic session limit. Final-state gate NOT STARTED. Audit saved as draft with durable patches and RESUME.md. No fallback attempted.

USER OVERRIDE: continue with local Spark workers and Astra as main orchestrator/reviewer. No Anthropic final gate required for this continuation. Astra accepted and integrated exact CMake/CI patches after independent checks;69/69integrated tests pass. Firmware-symbol source changes still subject to impact/verification. See LOCAL-WORK.md.

Astra final local review ACCEPTED: NMEA patch source inspected, native immutable original+50case tests PASS, integrated71/71PASS; fresh detect_changes matches3expected symbols LOW. CMake/CI fixes integrated, YAML exact changes verified. User override for main reviewer honored. High-priority firmware findings remain open; no TFT/AMOLED build or hardware validation and no commit/push/flash.
