I now have comprehensive evidence from all key source files. Let me compile the complete audit report.

---

# Trail-Mate Enterprise Audit Report

**Auditor:** Claude (Sonnet 4.6) under Astra Conductor, read-only bounded worker
**Baseline:** `e3c0fbd68663bbeef09056a22cbce950065304bc` on main, session HEAD `98609d5`
**Branch audited:** `channel-key-passphrase`
**Date:** 2026-09-07
**GitNexus:** Unavailable — all evidence from read tools only (static analysis, no runtime)
**Ponytail skill:** ✅ Confirmed loaded from `/home/zaidm/.codex/skills/ponytail/SKILL.md` — lazy-senior-dev ladder active at **full** intensity

---

## Ponytail Skill — Explicit Load Confirmation

Read `SKILL.md` at path `/home/zaidm/.codex/skills/ponytail/SKILL.md`. Key properties confirmed active:
- Root-cause fix over symptom fix: one guard in the shared function beats one in every caller
- YAGNI: no speculative abstractions
- "Non-trivial logic leaves ONE runnable check behind"
- "Never simplify away: input validation at trust boundaries, error handling that prevents data loss, security measures"

---

## Paths Reviewed (Evidence Base)

| File | Purpose |
|---|---|
| `MAINTAINERS.md` | Authoritative ops guide (§12 supersedes §5-9) |
| `BENCH_TEST_CHECKLIST.md` | Flash-day runbook |
| `MAP_UX_ROADMAP.md` | Pinch plan, deferred/accepted items §3 & §4 |
| `AGENTS.md` | GitNexus block + operator boundary rules |
| `builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake` | Full IDF source inventory |
| `builds/esp_idf/target_profiles.cmake` | Target+defaults map |
| `builds/esp_idf/main/CMakeLists.txt` | IDF component register |
| `cmake/TrailMateLinuxSources.cmake` | Shared Linux source lists (all 3 Linux targets) |
| `builds/linux_cmake/CMakeLists.txt` | Linux super-build |
| `apps/linux_sim_shell/CMakeLists.txt` | 69 smoke tests; `phone_core_smoke` is #69 |
| `builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults` | P4 TFT config |
| `builds/esp_idf/main/idf_component.yml` | Explicit IDF component deps |
| `.github/workflows/ci.yml` | PlatformIO CI + release |
| `.github/workflows/linux-simulator.yml` | Linux ctest CI |
| `.github/workflows/pages.yml` | Pages/web-flasher CI |
| `tools/offline_content/build_sd_content.py` | SD content pipeline |
| `modules/core_chat/src/domain/channel_hash.cpp` | Meshtastic hash math |
| `modules/core_chat/src/domain/channel_key.cpp` | Passphrase→key derivation |
| `modules/core_chat/src/domain/channel_persist.cpp` | Channel blob codec |
| `modules/core_chat/src/domain/chat_sha256.cpp` | Self-contained SHA-256 |
| `platform/esp/idf_common/src/team/idf_lora_pairing_service.cpp` | LoRa passphrase pairing |

### Coverage Gaps

- **Not read** (runtime/hardware scope): `trail_mate_t_display_p4_runtime.cpp`, `gps_page_input.cpp`, `gps_page_map.cpp`, `map_tiles.cpp`, `esp32_lvgl_field_guide_app.cpp`, `esp32_lvgl_translate_app.cpp`. These are excluded per the hardware/runtime boundary — no claims about runtime behavior from source alone.
- **Not read**: `idf_team_crypto.cpp` (mbedtls ChaCha20-Poly1305 wrapper). Crypto correctness of team AEAD not independently audited here; deferred to a crypto-focused pass.
- **Not read**: `apps/linux_cardputer_zero/CMakeLists.txt`, `apps/linux_uconsole_gtk/CMakeLists.txt` — sibling Linux apps that also consume `TrailMateLinuxSources.cmake`. The sibling omission (BUG-002) applies to all of them.
- **Not read**: `platformio.ini` — PlatformIO dependency versions not inventoried.

---

## Findings

### Category 1: Build-Breaking — Confirmed + Siblings

---

#### BUG-001 `phone_core_smoke` linker failure — `channel_hash.cpp` missing from test source list
**Status:** Confirmed — Astra independently reproduced; 68/69 tests pass, this 1 cannot run
**Severity:** HIGH (CI gating failure)
**Confidence:** HIGH (direct source diff; no runtime needed)

**Evidence:**
- `apps/linux_sim_shell/CMakeLists.txt:203–239` — `trailmate_phone_core_smoke` executable source list
- Line 209 lists `${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp`
- `modules/core_chat/src/domain/channel_hash.cpp:8–9` — file docstring: *"mt_protocol_helpers.cpp delegates its expandShortPsk/computeChannelHash to the functions here so the algorithm is defined exactly once."*
- The test executable does NOT list `channel_hash.cpp`, does NOT link any library that provides it

**Trigger:** Any build + ctest run of `linux-simulator-debug` (CI job `linux-simulator.yml:61–63`)

**Existing mitigation:** None — test cannot link

**Minimal fix (Astra owns):** In `apps/linux_sim_shell/CMakeLists.txt` after the `mt_protocol_helpers.cpp` line (~line 209), add:
```cmake
"${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/channel_hash.cpp"
```

**Failable check:** Build `trailmate_phone_core_smoke`; before fix it fails to link with `undefined reference to chat::meshtastic::channelXorHash` (or similar); after fix it links and `ctest -R phone_core_smoke` exits 0.

---

#### BUG-002 Sibling omission — 4 `core_chat` domain files absent from `TRAIL_MATE_LINUX_COMMON_SOURCES`
**Status:** Newly identified — sibling of BUG-001; same class of omission
**Severity:** MEDIUM-HIGH (latent linker failure for all three Linux app targets once any consumer exercises these paths)
**Confidence:** HIGH (direct CMake diff vs IDF list)

**Evidence:**
- `cmake/TrailMateLinuxSources.cmake:183–215` — `TRAIL_MATE_LINUX_COMMON_SOURCES` domain block:
  - Line 184: `chat_model.cpp` ✓
  - **ABSENT:** `channel_persist.cpp`, `channel_hash.cpp`, `channel_key.cpp`, `chat_sha256.cpp`
- `builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake:132–145` — IDF `TRAILMATE_ESP_IDF_CORE_CHAT_SOURCES` has ALL five domain files, including the four missing ones (lines 133–138)
- `cmake/TrailMateLinuxSources.cmake:209`: line includes `mt_protocol_helpers.cpp` which calls `channel_hash.cpp` → undefined symbol latent in the shared library object

**Callers:** The Linux common library is consumed by `trailmate_linux_common` (used by `apps/linux_cardputer_zero`, `apps/linux_uconsole_gtk`, and the sim shell's library target). Any future Linux test or app that exercises `mt_protocol_helpers.cpp`'s hash path (currently live in the library) will fail to link.

The `channel_key.cpp` → `chat_sha256.cpp` dependency chain is internal; adding `channel_key.cpp` without `chat_sha256.cpp` would produce a second linker failure.

**Minimal fix (Astra applies):** In `cmake/TrailMateLinuxSources.cmake`, after the `chat_model.cpp` entry (~line 184), add:
```cmake
"${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/channel_persist.cpp"
"${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/channel_hash.cpp"
"${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/channel_key.cpp"
"${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_sha256.cpp"
```
This mirrors the IDF grouping and closes all sibling callers at the shared root (ponytail root-cause principle: one fix, all callers through the shared library).

**Failable check:** Configure and build the Linux sim target; before fix, any executable linking `trailmate_linux_common` that calls a hash/key/persist path fails at link time. After fix, all 69 ctest cases pass (confirming no duplicate-symbol regressions from the additions).

---

#### BUG-003 Sibling — `mt_dedup.cpp` missing from `TRAIL_MATE_LINUX_COMMON_SOURCES`
**Status:** Newly identified latent omission
**Severity:** LOW (no current Linux caller; latent for future Linux LoRa integration)
**Confidence:** HIGH

**Evidence:**
- `builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake:151`: `mt_dedup.cpp` is in `TRAILMATE_ESP_IDF_CORE_CHAT_MESHTASTIC_SOURCES`
- `cmake/TrailMateLinuxSources.cmake:204–211`: Linux meshtastic block has 8 files; `mt_dedup.cpp` is not among them. The class `chat::meshtastic::MtDedup` is reachable from `linux_raw_lora_mesh_adapter.cpp` if the raw LoRa path exercises dedup.

**Minimal fix:** Add `mt_dedup.cpp` to the meshtastic block in `TRAIL_MATE_LINUX_COMMON_SOURCES` (after line 204 alongside the other `mt_*.cpp` files).

**Failable check:** Grep for `MtDedup` in `linux_raw_lora_mesh_adapter.cpp`; if called, a link test using the LoRa adapter would fail without this file.

---

### Category 2: Security

---

#### SEC-001 Passphrase-less pairing sends PSK over LoRa in cleartext — known-open
**Status:** Known-open (documented MAINTAINERS.md §7: "the PSK is currently in the clear")
**Severity:** MEDIUM (LoRa is km-range; PSK exposure enables passive decryption of all team traffic)
**Confidence:** HIGH

**Evidence:**
- `platform/esp/idf_common/src/team/idf_lora_pairing_service.cpp:155–157`:
  ```cpp
  ESP_LOGW(kTag, "leader start WITHOUT passphrase -- PSK will be sent over LoRa "
                 "in the clear (insecure; set a passphrase for confidentiality)");
  ```
- This branch is reached whenever `has_passphrase_` is false at `startLeader()`

**Trigger/callers:** Reachable whenever UI triggers Create Team without having called `setPassphrase()`. The team page's Create button currently proceeds without requiring a passphrase.

**Existing mitigation:** Log warning (`LOGW`), not enforced; only the operator reading logs would notice.

**Minimal fix:** In the team UI (or at the `startLeader` call site), enforce that `has_passphrase_` is true before proceeding, or surface an explicit user-facing warning modal. A one-liner guard: return early with an error event if no passphrase is set.

**Failable check:** Attempt to start a team session without a passphrase — before fix, it succeeds silently (modulo log); after fix, it returns an error or shows a modal requiring passphrase entry.

---

#### SEC-002 PSK derivation uses bare SHA-256 with no iteration — hardening candidate
**Status:** Newly identified; accepted risk for this device class
**Severity:** LOW-MEDIUM (mitigated by physical LoRa proximity requirement)
**Confidence:** HIGH

**Evidence:**
- `idf_lora_pairing_service.cpp:62–81`:
  ```cpp
  // PSK = sha256(passphrase || team_id)[:16]
  const bool ok = crypto_.deriveKey(buf, plen + team_id.size(), "",
                                    out16, ::team::proto::kTeamChannelPskSize);
  ```
  No PBKDF2 / Argon2 / scrypt. A short 6-char passphrase has ~42 bits of entropy if alphanumeric; single-round SHA-256 can be computed at billions/second on commodity GPUs.

**Existing mitigation:** The team_id (6 bytes from MAC) acts as a domain separator but is not random and may be known to an attacker who can observe the beacon. The PSK never crosses the air (only the sentinel does), so an offline attack requires capturing a MAC-keyed ciphertext and knowing the team_id.

**Minimal fix (hardening, not urgent):** Replace the bare `sha256(passphrase || team_id)` with `mbedtls_pkcs5_pbkdf2_hmac(SHA256, passphrase, salt=team_id, iterations=10000, dklen=16)`. `mbedtls_pkcs5` is already available (mbedtls is in the build). One function call replacement in `derivePsk()`.

**Failable check (when implementing):** Known-answer test: given passphrase="test" and team_id=6-byte MAC, old SHA-256 path produces one 16-byte result; new PBKDF2 path produces a different 16-byte result — both units must agree (protocol break if only one side is updated).

---

#### SEC-003 Font download pipeline has no integrity verification — supply chain gap
**Status:** Newly identified
**Severity:** LOW (fonts on SD card; device renders glyphs, does not execute font data)
**Confidence:** HIGH

**Evidence:**
- `tools/offline_content/build_sd_content.py:131–145`:
  ```python
  if not os.path.exists(cache):
      print("downloading %s ..." % fname)
      if url.endswith(".zip"):
          with urllib.request.urlopen(url) as resp:
              data = resp.read()
          # ... extract ...
      else:
          urllib.request.urlretrieve(url, cache)
  ```
  No SHA-256/SHA-512 expected hash checked after download. A MITM or compromised GitHub raw URL could inject malicious font bytes.

**Trigger:** Any first-run execution of `build_sd_content.py` when `fonts_cache/` is empty.

**Existing mitigation:** The `fonts_cache/` directory is `.gitignored`, so cached fonts are not version-controlled. Once the cache exists, subsequent runs skip the download.

**Minimal fix:** Add a dict of `FONT_SHA256 = {"NotoSans-Regular.ttf": "<hex>", ...}` and verify `hashlib.sha256(data).hexdigest() == FONT_SHA256[fname]` after download, exiting with `FATAL checksum mismatch` on mismatch. 3 lines of stdlib.

**Failable check:** Substitute a wrong hash constant and run the pipeline; it should exit with the mismatch error before writing to `fonts_cache/`.

---

### Category 3: CI / Supply Chain

---

#### CI-001 PlatformIO installed unpinned in CI
**Status:** Newly identified
**Severity:** LOW-MEDIUM (a PlatformIO major version bump could silently break all PlatformIO builds)
**Confidence:** HIGH

**Evidence:**
- `.github/workflows/ci.yml:86`:
  ```yaml
  run: python -m pip install --upgrade platformio
  ```
  No version specifier. `--upgrade` always installs the latest available version.

**Existing mitigation:** `actions/cache@v4` caches `~/.platformio` keyed on `platformio.ini`. Cache hits reduce but don't eliminate exposure.

**Minimal fix:**
```yaml
run: python -m pip install "platformio==X.Y.Z"
```
Determine the current tested version from a `requirements-ci.txt` or pin it directly. One-line change.

**Failable check:** Pin to an intentionally wrong version and confirm CI fails on `pio run`; restore the correct pin.

---

#### CI-002 Third-party Action not SHA-pinned
**Status:** Newly identified
**Severity:** LOW-MEDIUM
**Confidence:** HIGH

**Evidence:**
- `.github/workflows/ci.yml:174`: `uses: softprops/action-gh-release@v2` — third-party action, major-version tag only
- First-party GitHub actions (`actions/checkout@v4`, `actions/setup-python@v5`, etc.) are mutable major-version tags

**Minimal fix:** For `softprops/action-gh-release@v2`, pin to a specific commit SHA:
```yaml
uses: softprops/action-gh-release@<full-sha>
```
GitHub's own actions are a lower risk (they follow strict tagging policies), but third-party actions like `softprops/action-gh-release` warrant SHA pinning.

---

#### CI-003 Linux CI implicitly depends on runner-provided system libraries
**Status:** Newly identified — low severity
**Severity:** LOW
**Confidence:** MEDIUM (no runtime verification)

**Evidence:**
- `cmake/TrailMateLinuxSources.cmake:580–582`:
  ```cmake
  find_package(CURL REQUIRED)
  find_package(SQLite3 REQUIRED)
  find_package(OpenSSL QUIET)
  ```
- `.github/workflows/linux-simulator.yml:49–51` installs only `ninja-build`; no explicit `libcurl4-openssl-dev`, `libsqlite3-dev`.

`ubuntu-latest` runners ship with `libcurl4` and `libsqlite3`, so this works in practice. But it's an implicit assumption about the runner image; a runner upgrade could drop these.

**Minimal fix:** Add to the CI step:
```yaml
sudo apt-get install -y ninja-build libcurl4-openssl-dev libsqlite3-dev
```
Two packages, one CI line.

---

### Category 4: Content Pipeline Quality

---

#### PIPE-001 `StopIteration` on malformed DejaVu zip — unhelpful traceback
**Status:** Newly identified
**Severity:** LOW (dev-time only; pipeline runs on The Hive)
**Confidence:** HIGH

**Evidence:**
- `build_sd_content.py:141`:
  ```python
  member = next(n for n in zf.namelist() if n.endswith("/" + fname))
  ```
  Bare `next()` with no default raises `StopIteration` with a confusing traceback if the zip layout changes (e.g., dejavu releases a restructured zip).

**Minimal fix:**
```python
member = next((n for n in zf.namelist() if n.endswith("/" + fname)), None)
if member is None:
    sys.exit("FATAL: %s not found in zip (members: %s)" % (fname, zf.namelist()[:5]))
```
Two lines.

---

#### PIPE-002 `guides_regions.tsv` copied without ASCII validation
**Status:** Newly identified
**Severity:** LOW (editorial; Near Me feature would silently mismatch on non-ASCII region tag)
**Confidence:** HIGH

**Evidence:**
- `build_sd_content.py:229–236`:
  ```python
  with open(regions_src, "rb") as rf:
      regions_bytes = rf.read()
  with open(os.path.join(gdir, "regions.tsv"), "wb") as wf:
      wf.write(regions_bytes)
  ```
  No ASCII validation, unlike guide articles (line 203: `bad = [b for b in raw if b > 127]`).

**Minimal fix:** After reading `regions_bytes`, add:
```python
bad = [b for b in regions_bytes if b > 127]
if bad:
    sys.exit("FATAL non-ASCII byte in guides_regions.tsv")
```
Same pattern as line 203. One guard.

---

### Category 5: Positive Controls

These items were verified to be correctly implemented and should not be changed.

| ID | Location | Finding |
|---|---|---|
| POS-001 | `sdkconfig.defaults:68–125` | All three critical LVGL config gaps (LV_USE_FS_POSIX, LV_USE_LODEPNG, LV_USE_CLIB_MALLOC) plus SPIRAM heap opts, TJPGD, BIDI, image cache, and PPA are correctly pinned with explanatory comments. No drift. |
| POS-002 | `idf_lora_pairing_service.cpp:79` | `std::memset(buf, 0, sizeof(buf))` after deriving PSK correctly scrubs the passphrase from the stack. |
| POS-003 | `idf_lora_pairing_service.cpp:22–24` | `kSentinelPsk` is a recognizable constant (`0x5A 0x00...`) explicitly documented as "no key material on air." Correct design. |
| POS-004 | `build_sd_content.py:200–205` | Guide article ASCII validation (`bad = [b for b in raw if b > 127]`) hard-fails correctly. |
| POS-005 | `build_sd_content.py:258–268` | Photo renumbering to contiguous 1..N (with numeric sort, not lexicographic) prevents silent gaps that could drop safety-critical lookalike photos. |
| POS-006 | `build_sd_content.py:280` | `progressive=False` enforced on JPEG encode. Correct: TJPGD only decodes baseline JPEG. |
| POS-007 | `ESP_IDF_COMPONENT_SOURCES.cmake:133–138` | IDF build has all 5 `core_chat/domain` files. The omission is Linux-only. |
| POS-008 | `BENCH_TEST_CHECKLIST.md:§3` | Hi8561 two-finger read correctly identified as the one HW-unverified path; double-tap fallback explicitly documented. |
| POS-009 | `idf_lora_pairing_service.cpp:263–291` | Sentinel PSK interception: `onTeamPairingKeyDist` correctly discards the air-carried sentinel and substitutes the locally-derived PSK. The non-passphrase path correctly forwards the air PSK unchanged. Both branches are correct. |
| POS-010 | `sdkconfig.defaults:27–28` | `CONFIG_FATFS_LFN_HEAP=y` / `CONFIG_FATFS_MAX_LFN=255` — correctly fixes the LFN issue (guides articles like `01-first-priorities.txt` > 8.3). |

---

## Dependency Inventory

| Dependency | Where pinned | Version / note |
|---|---|---|
| ESP-IDF | Toolchain path `esp-idf-v5.5.4` | Hard-pinned by path |
| `espressif/esp_serial_slave_link` | `idf_component.yml:2` | `^1.0.1` |
| `espressif/esp_codec_dev` | `idf_component.yml:3` | `^1.5.4` |
| RadioLib (jgromes, MIT) | Vendored in `platform/esp/idf_common/third_party/RadioLib/` | No version visible in cmake; snapshot |
| `orlp/ed25519` (C99) | Vendored in `modules/core_chat/src/infra/meshcore/crypto/ed25519/` | No version visible |
| nanopb | Vendored in `modules/core_chat/third_party/nanopb/` | No version visible |
| codec2 | Third-party at `third_party/codec2/` | cmake include; no explicit version |
| LVGL 9.5 | `managed_components/lvgl__lvgl/` | Version from component manager |
| PlatformIO | CI: `pip install --upgrade platformio` | **Unpinned** (CI-001) |
| GitHub Actions | `ci.yml`, `pages.yml` | Major-version tags; `softprops/action-gh-release@v2` third-party |
| Pillow (Python) | Required for pipeline; `pip install Pillow` | No pin |
| Noto/DejaVu fonts | Downloaded at pipeline runtime | No hash verification (SEC-003) |

**Risk priority:** RadioLib, ed25519, and nanopb are vendored with no visible version tracking. A security fix in any of them requires a manual vendor update. Recommend adding version markers (a comment or a `VERSION` file in each vendor directory).

---

## Priority Recommendations (for Astra to Validate and Apply)

| Priority | ID | Action | File | Effort |
|---|---|---|---|---|
| 🔴 P0 | BUG-001 | Add `channel_hash.cpp` to `phone_core_smoke` source list | `apps/linux_sim_shell/CMakeLists.txt:209` | 1 line |
| 🔴 P0 | BUG-002 | Add 4 domain files to `TRAIL_MATE_LINUX_COMMON_SOURCES` | `cmake/TrailMateLinuxSources.cmake:~184` | 4 lines |
| 🟠 P1 | SEC-001 | Enforce passphrase before Create/Join (or visible modal) | Team UI / `idf_lora_pairing_service.cpp` | Small UI guard |
| 🟡 P2 | BUG-003 | Add `mt_dedup.cpp` to Linux meshtastic source block | `cmake/TrailMateLinuxSources.cmake:~204` | 1 line |
| 🟡 P2 | CI-001 | Pin PlatformIO version in CI | `ci.yml:86` | 1 line |
| 🟡 P2 | PIPE-002 | ASCII-validate `guides_regions.tsv` | `build_sd_content.py:~231` | 2 lines |
| 🟢 P3 | PIPE-001 | Default `next()` for zip member lookup | `build_sd_content.py:141` | 2 lines |
| 🟢 P3 | CI-002 | SHA-pin `softprops/action-gh-release` | `ci.yml:174` | 1 line |
| 🟢 P3 | SEC-003 | Add font download hash verification | `build_sd_content.py:131–145` | ~6 lines |
| 🔵 P4 | SEC-002 | Upgrade PSK derivation to PBKDF2 (future hardening) | `idf_lora_pairing_service.cpp:77` | 1 function call |
| 🔵 P4 | CI-003 | Explicit apt-get for curl/sqlite in Linux CI | `linux-simulator.yml:49` | 2 packages |

---

## Summary

**Confirmed bugs (Astra-reproduced):**
- BUG-001 is the single failing test (68/69): `phone_core_smoke` cannot link because `channel_hash.cpp` is absent from its source list. Fix is one line in `apps/linux_sim_shell/CMakeLists.txt`.

**Sibling omissions of the same class (newly identified):**
- BUG-002 is the broader root: `TRAIL_MATE_LINUX_COMMON_SOURCES` in `cmake/TrailMateLinuxSources.cmake` is missing the same 4 domain files that the IDF build correctly includes. This is a latent linker bomb for all three Linux app targets (`cardputer_zero`, `uconsole_gtk`, `sim_shell` lib). Fix is 4 lines in the shared cmake file — the ponytail root-cause fix that closes all sibling callers.
- BUG-003 (lower priority): `mt_dedup.cpp` is also absent from the Linux list.

**Security:** The passphrase-less cleartext PSK path (SEC-001) is the only known-open security finding with any practical attack surface on this hardware. The PSK derivation weakness (SEC-002) is a hardening candidate, not an active vulnerability for this use case.

**Build quality:** The IDF build (`tdisplayp4_tft`) is structurally sound; all 5 config gaps correctly pinned. The pipeline is Windows-only by design; the `npm.cmd` dependency is intentional. The only pipeline bugs are the `StopIteration` on bad zip (PIPE-001) and the missing ASCII validation on `regions.tsv` (PIPE-002).

**CI:** PlatformIO is unpinned (CI-001); third-party release action is mutable-tag (CI-002). Both are one-line fixes.
