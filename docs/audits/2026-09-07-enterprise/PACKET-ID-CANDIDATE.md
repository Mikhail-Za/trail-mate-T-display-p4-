# Packet-ID allocator candidate — not integrated

Spark task20260907-161812-79f361 attempt01 completed417.68seconds/19808local generated tokens. Frozen native verifier verify-cfb62137 PASS, unchanged tests. Astra reviewed uint64 bounds, reserve-before-use ordering, callback failure, reboot skipping, explicit-ID advancement/rejection and exhaustion; no ID0 or wrap. Artifact: /home/zaidm/projects/spark-worker/runs/20260907-161812-79f361/attempt-01/snapshot/modules/core_chat/include/chat/domain/persistent_packet_id_allocator.h.

A handoff-only correction (attempt02,19.4seconds/392tokens) obtained required explicit Ponytail acknowledgement and corrected inaccurate prose about skipping reserved IDs on reboot and the valid-but-exhausted2^32 marker. Source/tests byte-identical. Code passed first implementation check, but two native attempts are recorded; no task-level first-pass-success claim or inferred cost. No private keys, provider changes or cloud workers.

Accepted as the bounded allocation component, retained in the immutable local candidate, NOT copied into main as an unused abstraction. Native adapter must still own serialization/concurrency, durable NVS load/commit, failed/missing-storage behavior, explicit packet-ID integration, and migration from historical key/nonce use. Until integrated and verified, existing radio counter behavior remains vulnerable.

User confirmed: private channel keys are used only in Trail Mate, not the other firmware booted on the same device. A Trail Mate-owned persistent counter therefore does not need cross-firmware coordination for the current private keys. This assumption must remain true for newly provisioned keys. Upgrade and storage-loss policy must require an appropriate fresh-key/identity domain; no random-start or timestamp-XOR uniqueness claim. Team per-peer authenticated pairing/rotation design remains separate and open.

Git checkpoint: the accepted header and frozen verifier are also preserved under packet-id-candidate/ in this audit directory. They remain outside the production build.
