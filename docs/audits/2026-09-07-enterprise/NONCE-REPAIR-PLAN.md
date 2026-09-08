# Native Meshtastic nonce repair

Authority: user authorizes fixes, local Tiel implementation, Astra planning/review, and incremental commits/pushes. Anthropic review remains overridden; no cloud worker/reviewer is invoked. Baseline ff349a5.

## Observed facts and decisions

The native adapter uses stable eFuse sender identity and an in-memory counter beginning at 1. sendText allocates independently; sendEncodedPayload allocates or accepts caller IDs. Existing wire nonce is little-endian packet ID (8 bytes), sender (4), block counter (4). All four direct sendEncodedPayload callers must use one allocator. User confirms private keys are exclusive to Trail Mate. Channel configuration loads before adapter applyConfig. Eight slots include disabled channels.

Use the accepted Spark reserve-before-use allocator, with a single process-wide serialized owner in the native radio layer. Persist one versioned NVS blob in a dedicated tm_pkt_ids/state_v1 key before issuing IDs. Record magic/version, sender identity, exclusive uint64 high-water (1..2^32), and fingerprints of up to eight legacy explicit keys. Never write raw keys. Initialization captures all normalized 16/32-byte keys, INCLUDING disabled slots, before any TX when the record is absent. SHA-256 errors, malformed records, wrong sender, open/read/set/commit errors fail closed. Latch storage/hash failures for this boot; never silently reinitialize a malformed existing record. A reboot skips the whole previously reserved range. Reject zero/wrap/exhaustion and previously consumed explicit IDs. A failed transmission consumes its allocated ID.

Migration: block outgoing payloads encrypted with captured legacy keys, regardless of channel slot, name, enabled state, or reimport. Receive remains available. Operators must provision fresh private channel keys on all participating Trail Mate units; changing a name or passphrase spelling without changing the derived key is insufficient. Keys imported after migration must be fresh with respect to this sender's pre-upgrade traffic. Historic removed keys cannot be reconstructed.

NVS erasure and old backup restore require fresh private keys. Full backup rollback is not reliably detectable without a separate monotonic hardware authority; this repair does not claim anti-rollback protection. Do not flash an older counter implementation with these new keys. Public/default PSKs provide no confidentiality. These are deployment constraints, not claims that existing recorded traffic becomes secure.

## Implementation assignments

1. Spark: new native packet-ID storage header/implementation only, reusing the immutable allocator. Parent supplies frozen tests with observable NVS/SHA substitutes. Two functions initializePacketIds(node, config) and allocatePacketId(node, psk, len, requested, out). One static mutex and owner; no public constructible store or test reset APIs. Fixed 273-byte blob: TPI1 (4), node LE32 (4), high-water LE64 (8), count (1), eight SHA256 fingerprints (256). Validate zero unused fingerprint bytes; successful reservations update only the bound.
2. Spark: adapter integration in sendText, sendEncodedPayload and applyConfig; remove next_packet_id_. Allocate only inside sendEncodedPayload; give it optional output-ID pointer. Text payload encoder's unused ID argument can be zero. Reject disabled/out-of-range channels before encryption. No wire format or crypto algorithm changes. Parent integrates source list and CI check as small chores.

## Failable acceptance

Host tests: initialize missing state captures disabled legacy keys; malformed version/length/bounds/count/unused bytes and wrong sender rejected; all NVS and SHA errors issue no ID, even after the error clears this boot; same-node repeated initialization cannot reset state; multiple threads allocate unique IDs; explicit IDs, final ID, exhaustion, uncertain commits and reboot range skips; legacy key denial survives reload while fresh key succeeds. Actual production store is compiled, not a rewritten allocator model. Adapter tests execute actual changed bodies and observe every wire ID, text output ID, default/explicit allocations, rejected channels, failed TX and failed reservations. Inspect actual wire nonce formation separately; full TFT and AMOLED builds verify SDK integration. Physical reboot/power-loss/RF interoperability remains a hardware bench requirement.

## Astra plan review under user override

Material pitfalls resolved in the plan: capture disabled keys; key identity rather than slot/name; reserve before any TX; a single owner across adapter instances; no double allocation for text; output ID zero on allocation failure; latch uncertain storage errors; no claim that software-only NVS detects full restore rollback. State is not reset by channel changes. The stored high-water never resets on rekey or exhaustion.
