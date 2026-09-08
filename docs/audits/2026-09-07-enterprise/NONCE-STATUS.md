# Native Meshtastic nonce repair — verified and independently reviewed

Current routing: user explicitly selected GPT execution workers and Anthropic review. Spark is reserved for benchmarks; do not access it. Astra integrated three parallel Sol assignments after the two-round Fable plan gate passed. Earlier Spark attempts and pending task packets are historical, not current dispatch instructions.

## Implementation and verification

- One mutex-protected process-wide owner reserves 1024 packet IDs durably before use. Reboots skip the previous reservation; uncertain storage commits, malformed state and hash errors stop transmission. No wrap; native caller ID hints are ignored and failed transmissions consume IDs.
- First upgrade records effective private keys from every saved slot, including disabled slots, and blocks transmission with those keys. Provision fresh private keys. Public/default keys and open channels remain supported. Channel-load errors stop Meshtastic startup before migration; unrelated MeshCore startup remains supported.
- Astra independently reran all three production-source host harnesses: 29 storage scenarios, native send/wire nonce checks, and channel loader/facade checks. Both photo scripts, 71 existing simulator tests, UI boundaries, root-artifact check and whitespace check pass.
- TFT and AMOLED builds pass in official ESP-IDF 5.5.4. Changed production files match the isolated build copy by SHA256; dependencies.lock is unchanged. The host substitutes test control flow, not real cryptographic or NVS power-loss behavior.
- Fresh Fable completed-work review passed in two rounds: missing settings-store evidence supplied, then APPROVED. Plan review also passed in two rounds. See NONCE-CRUCIBLE-REVIEW.md for complete critiques and dispositions.

## Deployment and remaining work

No hardware flashed. Follow NONCE-MIGRATION.md and BENCH_TEST_CHECKLIST.md before deployment. Restoring old NVS, erasing state, downgrade/re-upgrade, or importing historically used keys requires fresh private keys: software cannot reconstruct those historical IDs or reliably detect complete rollback. Private keys remain exclusive to Trail Mate per the user.

Next security work is Team key authorization, authenticated rotation/recovery and replay protection. This repair addresses native Meshtastic packet counters, not those separate Team defects or every firmware/protocol.
