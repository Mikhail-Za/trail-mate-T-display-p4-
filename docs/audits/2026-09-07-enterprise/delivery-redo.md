I now have sufficient evidence. Compiling the addendum:

---

# Audit Addendum — Corrections and Evidence

**Ponytail:** ✅ Still active, full intensity. No drift.
**Source of truth:** Working tree as checked out — branch `channel-key-passphrase`, not main `e3c0fbd`. The branch diverges from main at 98609d5. Files read include `idf_lora_pairing_service.cpp` and several `channel-key-passphrase`-era additions that may not exist at `e3c0fbd`. All findings below cite the file and line actually read; any finding touching branch-only code is flagged.

---

## Correction 1 — Test numbering: phone_core_smoke is #15 of 69, not the last test

**Original report stated:** "68/69 tests pass, one cannot run" — this is correct as stated, but the implication of ordering was wrong. I described the test infrastructure as if `phone_core_smoke` were the final test.

**Correction from source:**
Counting `add_test()` calls in `apps/linux_sim_shell/CMakeLists.txt`:
1–8: sim shell + ASCII runtime smokes (lines 48–98)
9–10: legacy chat delivery alias smokes (lines 110, 122)
11: chat_presentation_source (line 150)
12: mesh_adapter_router_core (line 161)
13: meshcore_protocol_strategy (line 186)
14: receive_packet_service (line 200)
**15: trailmate_phone_core_smoke (line 238)**
16–48: key_verification, map_overlay, timezone, battery, team page (30+ smokes)
49–69: 21 ux_pack tests (foreach block, lines 730–758)

`phone_core_smoke` is test **#15 of 69**. The remaining 54 tests (16–69) pass without it. This does not change the BUG-001 fix required, but the report misrepresented the structural position of the failing test.

---

## Correction 2 — BUG-002 severity retracted; real callers proven; channel_persist.cpp is YAGNI

**Original report stated:** "BUG-002 — Severity HIGH (latent linker bomb for all three Linux app targets)"

**Correction:** Astra confirmed 69/69 pass after only the BUG-001 source-list fix. The four missing domain files in `TRAIL_MATE_LINUX_COMMON_SOURCES` do NOT currently break any test. Severity HIGH was wrong. What follows is the grounded caller analysis.

### Proven real callers (from source)

**`channel_hash.cpp`:**
- Caller: `modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp:8` → `#include "chat/domain/channel_hash.h"` *(confirmed by grep)*
- `mt_protocol_helpers.cpp` IS in `TRAIL_MATE_LINUX_COMMON_SOURCES` at `cmake/TrailMateLinuxSources.cmake:209`
- The Linux common library therefore has a live undefined-symbol reference to `channel_hash.cpp` functions. The linker does not resolve it for current tests because none pulls in `mt_protocol_helpers.o`; but any future executable that exercises the meshtastic hash path through the common library will fail to link.

**`channel_key.cpp` + `chat_sha256.cpp`:**
- Caller: `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp:16` → `#include "chat/domain/channel_key.h"` *(confirmed by grep)*
- Call site: `settings_page_components.cpp:1566` → `chat::deriveChannelKey(...)` *(confirmed by grep)*
- `settings_page_components.cpp` IS in `TRAIL_MATE_LINUX_UI_SHELL_SOURCES` at `cmake/TrailMateLinuxSources.cmake:436`
- `channel_key.cpp` calls `detail::sha256()` from `chat_sha256.cpp` — both are needed together.

**`channel_persist.cpp`:**
- Callers found by grep: `idf_chat_factory.cpp` (IDF only), `ESP_IDF_COMPONENT_SOURCES.cmake`, and `channel_persist.cpp`/`.h` themselves.
- **No Linux consumer identified.** This is YAGNI — do not add to the Linux lists.

### Corrected BUG-002 entry

**Status:** Newly identified, latent — NOT a current confirmed failure (69/69 pass after BUG-001 fix)
**Severity:** LOW-MEDIUM (will surface as linker errors the moment any Linux app or test exercises these paths)
**Files affected:**
- `cmake/TrailMateLinuxSources.cmake:209` — `mt_protocol_helpers.cpp` pulls in `channel_hash.cpp`; fix by adding `channel_hash.cpp` to `TRAIL_MATE_LINUX_COMMON_SOURCES` after `chat_model.cpp` (~line 184)
- `cmake/TrailMateLinuxSources.cmake:436` — `settings_page_components.cpp` pulls in `channel_key.cpp` + `chat_sha256.cpp`; fix by adding both to `TRAIL_MATE_LINUX_UI_SHELL_SOURCES` (near the settings block ~line 435)
- `channel_persist.cpp` — **YAGNI, no Linux caller proven. Do not add.**

**Minimal fix (Astra applies — do not apply speculatively):** Add exactly three files across two list blocks, only after confirming no existing test that links these targets was already passing with wrong symbols (all 69 currently pass, so additions are additions, not changes to currently-linked symbols).

**Failable check:** After adding the three files, build all three Linux targets and confirm no duplicate-symbol errors; then confirm 69/69 still pass.

---

## Correction 3 — BUG-003 retracted (YAGNI)

**Original:** "BUG-003: `mt_dedup.cpp` missing from Linux meshtastic source block."

**Retraction:** No current Linux consumer of `mt_dedup.cpp` or `MtDedup` class is reachable from any Linux source in `TRAIL_MATE_LINUX_COMMON_SOURCES` or `TRAIL_MATE_LINUX_UI_SHELL_SOURCES`. The `linux_raw_lora_mesh_adapter.cpp` link path does not exercise dedup in the current test suite. Adding it speculatively violates YAGNI. **BUG-003 is removed from findings.**

---

## Correction 4 — POS-002 retracted; new finding SEC-NEW-1: `std::memset` is not guaranteed scrubbing

**Original (POS-002):** "`std::memset(buf, 0, sizeof(buf))` after deriving PSK correctly scrubs the passphrase from the stack."

**Retraction:** This is incorrect. C++ compilers may legally elide a `std::memset` (or any write) to a local variable that is not subsequently read — this is the "dead store elimination" optimization. The passphrase buffer at `idf_lora_pairing_service.cpp:79` is `uint8_t buf[kMaxPassphraseLen + kTeamIdSize]` on the stack; after `crypto_.deriveKey()` reads it, the `memset` is a dead store and may be removed by the optimizer. This is a real and documented risk in security-sensitive code.

**Confirmed no safe alternative in use:** Grep for `mbedtls_platform_zeroize`, `explicit_bzero`, `SecureZeroMemory` across the entire repo returned **zero matches**. The codebase has no safe-zeroing primitive anywhere.

**Evidence:**
- `idf_lora_pairing_service.cpp:73–79` (branch code, may not be at main `e3c0fbd`):
  ```cpp
  uint8_t buf[kMaxPassphraseLen + ::team::proto::kTeamIdSize];
  ...
  const bool ok = crypto_.deriveKey(buf, ...);
  std::memset(buf, 0, sizeof(buf));  // ← may be elided
  return ok;
  ```
- Repo-wide grep: zero hits for any safe-zeroing function

**New finding SEC-NEW-1:**
- **Severity:** MEDIUM (passphrase material on stack; ESP-IDF RISC-V/Xtensa toolchain may or may not elide; cannot assert either way without inspecting the compiled binary)
- **Caller scope:** `idf_lora_pairing_service.cpp:derivePsk()` — called from both `startLeader()` and `onTeamPairingKeyDist()`, so both pairing roles are affected
- **Minimal fix:** Replace `std::memset(buf, 0, sizeof(buf))` with `mbedtls_platform_zeroize(buf, sizeof(buf))` — already available since mbedtls is in main's REQUIRES. One function call substitution, same semantics, guaranteed not optimized away by ESP-IDF's mbedtls port
- **Failable check:** Inspect the compiled object (`objdump -d` or `xtensa-esp-elf-objdump`) for the `derivePsk` function; confirm the zero-fill instructions are present in the output. This requires Astra (Windows/Axiom) with the toolchain; not claimable from source alone.

---

## Correction 5 — SEC-002: Remove the "10000 iterations" PBKDF2 recommendation

**Original:** "Replace bare SHA-256 with `mbedtls_pkcs5_pbkdf2_hmac(SHA256, passphrase, salt=team_id, iterations=10000, dklen=16)`"

**Correction:** "10000" is an arbitrary number not grounded in any hardware profiling of the ESP32-P4's SHA-256 throughput or the acceptable key-derivation latency for a pairing flow. Publishing an arbitrary iteration count as a production recommendation is wrong. The correct approach is to benchmark `mbedtls_pkcs5_pbkdf2_hmac` on the actual hardware, choose the maximum iteration count that keeps pairing latency acceptable (e.g., < 1 second), and document the measurement. This is a Astra/Axiom job — requires hardware execution.

The structural finding remains correct and stands: `SHA256(passphrase || team_id)[:16]` is not a key-stretching function; a short passphrase can be brute-forced offline at GPU speeds once the team_id is known. The fix direction is correct (use a key-stretching KDF); the iteration count must be determined empirically, not stated here.

---

## Correction 6 — RF proximity does not make weak passphrases safe

**Original SEC-002 stated:** "Severity: Medium-Low (off-grid LoRa device, physically proximate attacker required)"

**Correction:** LoRa SX1262 at default settings reaches multiple km. A directional antenna extends this further. The team_id is a function of the MAC address and is observable in beacon frames. An adversary does not need to be in handshake range — they only need to capture the SX1262 beacon or join/key frames at distance. "RF proximity" is not a meaningful security boundary for a LoRa device.

The severity should be assessed on the weakness of the derivation function alone, not mitigated by a physical proximity assumption that doesn't hold for LoRa. Retain SEC-002 but remove "off-grid / physically proximate" as a mitigation claim.

---

## Correction 7 — SEC-003: Font binfont files are not harmless input

**Original:** "Severity: LOW (fonts on SD; device renders glyphs, does not execute font data)"

**Correction:** Font binfont files are loaded and parsed by `lv_binfont_create()` in LVGL. A maliciously crafted binfont (or a substituted font via the download pipeline) could exploit a vulnerability in LVGL's binary font parser — triggering memory corruption rather than wrong glyphs. This is an indirect code execution risk path, not just an aesthetics risk. The severity cannot be dismissed as "device renders glyphs." No claim is made here about whether a current exploitable vulnerability exists in `lv_binfont_create` (no advisory evidence available); the claim is that the download-without-integrity pipeline is the trust boundary and must not be dismissed.

The finding SEC-003 (no hash verification on font downloads) remains valid and should be severity MEDIUM, not LOW.

---

## Correction 8 — CI-001 clarification: espressif32 platform IS pinned; PlatformIO CLI is not

**Original:** "CI-001: PlatformIO installed unpinned in CI"

**Correction (from source):**
- `platformio.ini:33` — `platform = espressif32@6.10.0` — the ESP32 Arduino framework/platform IS hard-pinned to an exact version
- `.github/workflows/ci.yml:86` — `python -m pip install --upgrade platformio` — the PlatformIO CLI tool itself (the tool that runs `pio run`) is NOT pinned

These are distinct. The framework is reproducible; the CLI executing it is not. A PlatformIO CLI version bump could change how `platformio.ini` is parsed or how dependencies are resolved. The finding is narrowed: the risk is specifically the PlatformIO CLI, not the ESP32 platform or library dependencies.

---

## Correction 9 — CI-003 retracted: linux-simulator correctly omits libsqlite3/libcurl

**Original CI-003:** "linux-simulator.yml installs only `ninja-build`; no explicit libcurl/sqlite."

**Retraction from source:**
- `uconsole-linux.yml:35` installs `libsqlite3-dev libcurl4-openssl-dev` — because the uconsole target links `trailmate_linux_common` which has `find_package(CURL REQUIRED)` and `find_package(SQLite3 REQUIRED)`
- `cardputer-zero-linux.yml:38` installs `libsqlite3-dev libcurl4-openssl-dev libssl-dev` — same reason
- `linux-simulator.yml:51` installs only `ninja-build` — because `apps/linux_sim_shell/CMakeLists.txt` creates `trailmate_linux_sim_shell` which links against `trailmate_ui_lvgl_ux_packs` only; it does NOT call `trailmate_add_linux_common()` and does NOT link the common library. No CURL or SQLite dependency.

The patterns are consistent and correct. **CI-003 is retracted.** The simulator workflow correctly installs only what it needs.

---

## New Finding: Dependency Version Inventory Corrections

### Confirmed version data (from source, not assumed)

| Dependency | IDF/ESP-IDF build | PlatformIO/Arduino build | Notes |
|---|---|---|---|
| ESP-IDF | **5.5.4** (`dependencies.lock:142`) | N/A | Astra: v5.5.5 released 2026-07-17; latest 6.1 — one patch behind |
| LVGL | **9.5.0** (`dependencies.lock:149`) | **9.4.0** (`platformio.ini:74`) | **Version divergence between build systems** |
| RadioLib | **7.6.0** (vendored; `BuildOpt.h:627–629`) | **7.4.0** (`platformio.ini:75`) | **Version divergence; IDF vendored copy is 2 minor versions newer** |
| nanopb | `"nanopb-1.0.0-dev"` (vendored; `pb.h:70`) | `^0.4.8` (semver range; `platformio.ini:82`) | Vendored copy is a **development snapshot with no stable version tag**; PlatformIO uses a semver range (not exact pin) |
| espressif32 | N/A | **6.10.0** (`platformio.ini:33`) | Pinned |
| PlatformIO CLI | N/A | **unpinned** (`ci.yml:86`) | `--upgrade` installs latest |
| `espressif/esp_codec_dev` | **1.5.4** (`dependencies.lock:21`) | N/A | Locked by hash |
| `espressif/esp_lvgl_port` | **2.7.2** (`dependencies.lock:105`) | N/A | Locked by hash |

**Key risks:**
- **LVGL divergence (9.4.0 vs 9.5.0):** The IDF build and the Arduino/PlatformIO builds run on different LVGL versions. Behavior differences between 9.4 and 9.5 (particularly in image cache, PPA draw, and binfont handling) may produce different results on the same hardware depending on build path. This is not tracked in any manifest comment.
- **RadioLib divergence (7.4.0 vs 7.6.0):** The IDF vendored copy is newer. If a LoRa bug was fixed between 7.4.0 and 7.6.0, the PlatformIO targets (tdeck, tlora_pager) would have the older behavior. Conversely, if 7.6.0 introduced a regression, only the IDF build has it. Divergence should be intentional and documented, not accidental.
- **nanopb `1.0.0-dev`:** A development snapshot carries no stability guarantee and is not a release artifact. The PlatformIO `^0.4.8` range resolves to a different version series. These nanopb forks may have incompatible wire formats for edge cases.
- **ESP-IDF 5.5.4 vs 5.5.5:** Astra reports v5.5.5 was released 2026-07-17. Per Astra's context, IDF 5.5.5 is one patch release ahead. No ESP-IDF security advisory claims are made here (no advisory evidence provided). Upgrading a patch release requires the sdkconfig apply recipe (MAINTAINERS §4) and a full `tdisplayp4_tft` build verify.

---

## New Finding CI-NEW-1: `ci.yml` triggers on every branch push with no path filter

**Status:** Newly identified — reproducibility / cost concern
**Severity:** LOW (no security impact; operational/cost concern)

**Evidence:**
- `ci.yml:3–7`:
  ```yaml
  on:
    push:
      branches: ["**"]
      tags:
        - "v*"
    pull_request:
  ```
  No `paths:` filter. Every push to any branch — including feature branches, hotfix branches, draft branches — runs the full 5-environment PlatformIO matrix build.

- Compare: `linux-simulator.yml:5–19` has explicit `paths:` filters on affected directories. `uconsole-linux.yml` and `cardputer-zero-linux.yml` also have path filters.

**Impact:** The PlatformIO build matrix is 5 environments (`tlora_pager_sx1262`, `tlora_pager_lr1121`, `tdeck`, `lilygo_twatch_s3`, `gat562_mesh_evb_pro`). A documentation-only commit or README edit triggers all five. No security risk; pure build efficiency and cost.

**Minimal fix:** Add `paths:` filters to the `build` job (or as a top-level workflow filter) to trigger only on changes under `src/`, `modules/`, `boards/`, `platform/esp/`, `platformio.ini`, and workflow files — mirroring the pattern in the Linux workflows.

---

## New Finding CI-NEW-2: No top-level `permissions:` block in `ci.yml`

**Status:** Newly identified — least-privilege gap
**Severity:** LOW-MEDIUM

**Evidence:**
- `ci.yml:1–9` — no `permissions:` block at workflow level
- Without a workflow-level `permissions:` block, all jobs run with the repository's default GITHUB_TOKEN permissions
- For a public repository: defaults are `read` for most scopes, which is appropriate for `boundary`, `format`, and `build` jobs
- The `release` job at `ci.yml:138–140` explicitly grants `permissions: actions: write, contents: write` — scoped correctly to that job
- `linux-simulator.yml`, `uconsole-linux.yml`, `cardputer-zero-linux.yml` also have no top-level `permissions:` block

**Risk:** Without an explicit `permissions: read-all` (or equivalent minimum) at the workflow level, if GitHub changes its default permission model, or if the repo is changed from public to private (different defaults apply), the `boundary`/`format`/`build` jobs could run with write permissions unintentionally.

**Minimal fix:** Add to the top of `ci.yml` (and all other workflows without a top-level block):
```yaml
permissions:
  contents: read
```
This explicitly limits all jobs to read-only unless overridden at the job level (as `release` already does). One line per workflow file.

---

## Revised Dependency Upgrade Recommendation (IDF patch)

**Original:** The report noted IDF 5.5.4 with no specific guidance.

**Corrected guidance from Astra's evidence:**
- IDF **5.5.4** → **5.5.5** (one patch release, 2026-07-17). No security advisory evidence is available here; Astra provided the version fact only.
- No claim is made about whether 5.5.5 contains security fixes — that requires reading the official IDF 5.5.5 release notes, which are outside this read-only scope.
- The upgrade path is known (sdkconfig apply recipe, MAINTAINERS §4, compile-verify on `tdisplayp4_tft`) and is low-risk for a patch release.
- IDF 6.1 (latest per Astra) is a major version jump; upgrading would require significantly more verification effort and is out of scope for a patch-level safety cycle.

---

## Content Pipeline Path/Download Bounds (previously unread)

**Font download (zip path extraction):**
- `build_sd_content.py:141–143`: The zip member `member` is found by `n.endswith("/" + fname)` where `fname` is a controlled constant from `FONT_SOURCES`. The extracted content is written to `cache = os.path.join(FONT_CACHE, fname)` — a fully controlled path, NOT derived from the zip member path. **No zip-slip vulnerability:** the zip member name is only used as a key for `zf.open()`, not as a filesystem path.
- `build_sd_content.py:145`: `urllib.request.urlretrieve(url, cache)` — `url` and `cache` are both from controlled constants. No user input reaches these parameters.

**Photo staging:**
- `build_sd_content.py:247–251`: `section` and `stem` come from `os.listdir(raw_root)` and `os.listdir(sdir)`. These become path components via `os.path.join(gdir, "photos", section, stem)`. If a directory in `photos_raw/` were named `../something`, it could produce a path outside `gdir`. This is developer-controlled content (not external/user input), so the risk is restricted to intentional or accidental naming in the developer's own working tree. **Assess severity LOW** — no remote attacker can inject this.

**`npm.cmd` subprocess:**
- `build_sd_content.py:155–172`: arguments are passed as a list, not as a shell string. No shell injection is possible regardless of what the font symbols string contains (the comment at line 153 explicitly notes the `<>&|` concern is about `cmd.exe` mangling, which is correctly avoided by using `subprocess.run` with a list).

---

## Revised Priority Table

| Priority | ID | Correction status | Action |
|---|---|---|---|
| 🔴 P0 | BUG-001 | Confirmed unchanged | Add `channel_hash.cpp` to `phone_core_smoke` source list (`apps/linux_sim_shell/CMakeLists.txt:~209`) |
| 🟠 P1 | BUG-002 (revised) | Latent, not confirmed current | Add `channel_hash.cpp` to `TRAIL_MATE_LINUX_COMMON_SOURCES`; add `channel_key.cpp`+`chat_sha256.cpp` to `TRAIL_MATE_LINUX_UI_SHELL_SOURCES`. Do NOT add `channel_persist.cpp` (no Linux consumer) |
| 🟠 P1 | SEC-NEW-1 | New — replaces retracted POS-002 | Replace `std::memset(buf, 0, sizeof(buf))` with `mbedtls_platform_zeroize(buf, sizeof(buf))` in `derivePsk()` |
| 🟠 P1 | SEC-001 | Unchanged (known-open) | Enforce passphrase before Create/Join in team UI |
| 🟡 P2 | SEC-002 (revised) | Remove iteration count claim | PSK derivation not key-stretched; upgrade to PBKDF2 with hardware-profiled iteration count when determined on device |
| 🟡 P2 | SEC-003 (revised) | Severity raised to MEDIUM | Add hash verification for font downloads; risk is parser memory corruption, not just wrong glyphs |
| 🟡 P2 | DEP-NEW-1 | New | LVGL 9.4.0 (PlatformIO) vs 9.5.0 (IDF) — document the divergence or align |
| 🟡 P2 | DEP-NEW-2 | New | RadioLib 7.4.0 (PlatformIO) vs 7.6.0 (IDF vendored) — document divergence or align |
| 🟡 P2 | DEP-NEW-3 | New | nanopb vendored as `1.0.0-dev`; PlatformIO range `^0.4.8` — document version policy |
| 🟡 P2 | CI-001 (clarified) | PlatformIO CLI unpinned (not framework) | Pin PlatformIO CLI in CI |
| 🟡 P2 | CI-NEW-2 | New | Add `permissions: contents: read` at workflow level for all CI workflows |
| 🟢 P3 | CI-NEW-1 | New | Add `paths:` filter to `ci.yml` `build` job to avoid builds on non-code changes |
| 🟢 P3 | PIPE-001 | Unchanged | Fix bare `next()` zip member lookup |
| 🟢 P3 | PIPE-002 | Unchanged | ASCII-validate `guides_regions.tsv` |
| 🔵 P4 | DEP-IDF-5.5.5 | New | Evaluate IDF 5.5.4 → 5.5.5 patch upgrade; read official release notes first |
| ~~BUG-003~~ | RETRACTED | YAGNI — no Linux mt_dedup consumer | |
| ~~CI-003~~ | RETRACTED | Simulator correctly omits libsqlite/libcurl | |
| ~~POS-002~~ | RETRACTED; converted to SEC-NEW-1 | memset is not guaranteed scrubbing | |
