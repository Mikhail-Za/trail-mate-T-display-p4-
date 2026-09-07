# Upstream checks — 2026-09-07
Five public GitHub API requests (read-only, no credentials) stored as JSON in this directory.

- IDF public repository advisories: https://api.github.com/repos/espressif/esp-idf/security-advisories?per_page=100
- IDF latest release: https://github.com/espressif/esp-idf/releases/tag/v6.1 (2026-08-27).
- IDF patch candidate: https://github.com/espressif/esp-idf/releases/tag/v5.5.5 (2026-07-17).
- LVGL latest: https://github.com/lvgl/lvgl/releases/tag/v9.5.0 (2026-02-18), matches dependencies.lock:149.
- LVGL public repository security-advisory list returned []; this is not evidence of no vulnerabilities or complete vulnerability-database coverage.

Actual dependencies.lock:142 is ESP-IDF5.5.4. Official advisories list it for esp_driver_jpeg (GHSA-v6r2-f6p2-88cj), ESP-TEE, protocomm BLE, HTTP WebSocket and lwIP DHCP; 5.5.5 is listed patched for those entries. P4 defaults explicitly enable LVGL TJPGD, and source search found no direct esp_driver_jpeg/esp_tee/protocomm/httpd_ws call. Thus version match alone does not establish reachable exposure. New September Classic Bluetooth advisories also exist; P4/C6 architecture differs from Classic-capable ESP32 targets. Assess each configured target with generated build config before applicability claims.

Recommendation: validate a patch-level5.5.5 migration in isolated TFT/AMOLED builds and bench checks before considering IDF6.x. Recheck advisories at migration time; latest tag is not a security guarantee. No version changed during this audit. Advisory text may retain stale yet-to-release notes; the v5.5.5 release API confirms actual publication.
