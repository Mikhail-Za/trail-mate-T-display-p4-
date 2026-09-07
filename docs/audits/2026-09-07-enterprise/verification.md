# Independent verification — Astra

Baseline: e3c0fbd68663bbeef09056a22cbce950065304bc, branch main, clean porcelain; ancestor check against 98609d5 exit 0. No source checkout edits.

- python3 scripts/check_platform_ui_boundaries.py: PASS.
- python3 scripts/check_root_build_artifacts.py: PASS.
- cmake --preset linux-simulator-debug: BLOCKED (Ninja absent).
- Alternate existing generator: cmake -S builds/linux_cmake -B /tmp/trailmate-audit-20260907/build -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DTRAIL_MATE_BUILD_LINUX_SIM_SHELL=ON -DTRAIL_MATE_BUILD_LINUX_CARDPUTER_ZERO=OFF -DTRAIL_MATE_BUILD_LINUX_UCONSOLE_GTK=OFF: PASS. Reconfigured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON.
- cmake --build /tmp/trailmate-audit-20260907/build -j 4: FAIL linking trailmate_phone_core_smoke; two undefined references to expandChannelPsk and computeChannelHashBytes. -- -k build collected all remaining executables.
- ctest --test-dir /tmp/trailmate-audit-20260907/build --output-on-failure: 68 PASS, 1 NOT RUN (missing executable), 69 registered tests.
- Isolated worktree /tmp/trailmate-audit-20260907/fix-worktree changes only apps/linux_sim_shell/CMakeLists.txt source list, adding existing domain/channel_hash.cpp. No function/class/method edited; symbol-impact prerequisite is inapplicable to this list entry. No commit performed (GitNexus detect_changes unavailable).
- Same configure in fixed-build using isolated worktree; cmake --build ... -j 4: PASS.
- ctest --test-dir /tmp/trailmate-audit-20260907/fixed-build --output-on-failure: 69/69 PASS.
- git diff --check in isolated worktree: PASS. Patch saved 0001-link-channel-hash.patch.
- Attempted ASan+UBSan compiler flags: configure BLOCKED at compiler sanity check, missing /usr/lib64/libasan.so.8.0.0. No sanitizer test result claimed.
- P4 TFT/AMOLED firmware build and on-device checks: NOT RUN; no standard-location IDF toolchain or attached-device verification. Linux results do not imply firmware correctness.
- GitNexus MCP/CLI/index unavailable after tool/catalog/local cache searches. Mandatory upstream symbol impact and detect_changes cannot run. Firmware symbol fixes remain recommendations, no symbol edits or commits.

Positive verification: suspected channelHashFromRecord key-length overflow is not currently established as reachable: platform/esp/radio/meshtastic_radio_adapter.cpp:730–759 normalizes length and copies terminated name before calling. Do not elevate an invalid helper input into a remote vulnerability without another caller/ingress path.

## Plaintext KeyDist reproduction
Compiled actual baseline TeamService and all team protocol .cpp files with existing FakeMeshAdapter/FakeCrypto/FakeRuntime fixtures copied into repro_plain_keydist.cpp; no production edits. Command:

    c++ -std=c++17 -I modules/core_team/include -I modules/core_chat/include -I modules/core_sys/include /tmp/trailmate-audit-20260907/repro_plain_keydist.cpp modules/core_team/src/usecase/team_service.cpp modules/core_team/src/protocol/*.cpp -o /tmp/trailmate-audit-20260907/repro_plain_keydist
    /tmp/trailmate-audit-20260907/repro_plain_keydist

Exit 0: CONFIRMED: plaintext foreign-team KeyDist invokes all four key derivations.

Scope: actual service/control-flow and wire codecs on host, fake mesh/crypto/runtime; not RF exploit, crypto proof, or hardware test. Initialize valid team 0x42/key_id7, queue plaintext TEAM_MGMT_APP KeyDist team0x99/key_id88/PSK0xA5, assert four derivations use 0xA5 and keys remain valid. Existing TeamEncrypted decoder rejects the short plaintext envelope before invoking fake decrypt. After remediation a security regression should assert zero derivations for this input instead of the current demonstration assertion.

## Workflow token hardening
Four .github/workflows files (ci, linux-simulator, cardputer-zero-linux, uconsole-linux) gain only top-level permissions: {contents: read}. PyYAML6.0.3 parsed before/after structures; after removing new permissions mappings, every structure equals baseline. Release job explicit {actions: write, contents: write} remains unchanged. git diff --check passed. Patch0002 saved. No hosted GitHub Actions run claimed.

## NMEA nonfinite reproduction
c++ -std=c++17 -I modules/core_gps/include /tmp/trailmate-audit-20260907/repro_nmea_nonfinite.cpp modules/core_gps/src/protocol/nmea/nmea_parser.cpp modules/core_gps/src/protocol/nmea/nmea_sentence.cpp -o /tmp/trailmate-audit-20260907/repro_nmea_nonfinite
Result exit0: CONFIRMED: checksum-valid RMC publishes valid fix with nonfinite coordinates. This host parser observation does not establish P4 parser wiring or hardware crash.
