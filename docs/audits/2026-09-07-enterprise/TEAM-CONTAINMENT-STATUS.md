# Team key-distribution containment — verified and independently reviewed

Scope: the legacy Team management KeyDist/KeyRequest path, not the full Team protocol. User-authorized GPT workers implement in isolated worktrees; Astra integrates and verifies; Anthropic Fable reviews. Spark is excluded.

## Intended operator behavior

- Existing locally stored Team keys and ordinary messaging continue to work. Explicit local key installation, boot restore and the separate pairing path are preserved.
- Legacy management KeyDist packets are rejected whether plaintext or protected by the old shared group key. Neither form proves an authorized, confidential update from the leader.
- Member removal and key recovery are unavailable until authenticated per-peer key establishment and rotation are implemented. A rejected removal must preserve the complete current team/key state; it must not claim success, transmit new keys or schedule retries.
- The member's Key Recovery action explains that secure updates are unavailable. Hostlink returns its existing Unsupported error for denied legacy key updates; no wire schema changes.
- Older peers cannot restore this feature by sending their old KeyDist messages. There is no automatic downgrade or plaintext fallback for this management path.

## Security limits and next work

Unauthenticated Status messages remain accepted and can trigger misleading key-recovery warnings or change roster/leader UI state. A later current-key status can clear the waiting flag; no secure recovery claim is made.

This containment does not make pairing authenticated. The current pairing protocol can still transmit real keys when no passphrase is set; passphrase mode lacks key confirmation and currently has a fail-open derivation-error branch. Do not interpret Pair Member as a secure recovery workaround. These are the next implementation issues, alongside leader authentication, persistent replay state and removed-member exclusion.

A group member knows the shared key. Encrypting replacement keys under it, even to another recipient's radio address, cannot exclude that member. The replacement protocol must use independently authenticated per-peer protection and verified key/epoch installation before success.

Team group keys are distinct from native Meshtastic channel keys. The previously delivered native packet-counter migration still requires fresh private channel keys according to NONCE-MIGRATION.md; this containment does not replace that procedure.

No device flashed. The integrated simulator build and 72/72 tests pass. TFT/AMOLED builds pass with the same production source. The live-source UI handler check passes, including deliberate guard-removal failure. Fresh Fable completed-work review approved in one round; the plan gate took two rounds. Physical pairing/RF/storage/UI tests remain required before deployment. See TEAM-CONTAINMENT-PLAN.md and TEAM-CRUCIBLE-REVIEW.md for exact acceptance scope.
