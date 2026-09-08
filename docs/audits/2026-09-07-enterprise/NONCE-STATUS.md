# Nonce repair continuation — implementation pending

Plan and migration procedure committed/pushed as d2253c6. No production radio/storage source was changed or integrated. The existing reboot-counter vulnerability remains open.

Spark task 20260907-203953-7db477 attempt-01 (local Tiel) timed out after 938.45 seconds including preparation/collection; implementation deadline 900 seconds, output allowance 16,384. It generated 26,806 local tokens, changed zero files, and ran no implementation acceptance check. Rejected as an implementation result; this is not a passing task or a new finite-trial slot. No paid-worker fallback or Anthropic call.

After completion, canonical worker.py status returned exit1: "Worker is busy; start/stop/run share one lock. Retry when it finishes." Per the Spark skill, dispatch halted without retrying or interrupting the lock owner. Current-task metadata still described the finished timeout; it did not identify the new lock holder. No assumption about the lock owner is justified. No correction has been dispatched. Two correction attempts remain for this same task.

## Saved work and verification

- scripts/check_packet_id_storage.py: 23 planned fresh-process storage/migration/concurrency/fault scenarios compiling the future production store with NVS/SHA substitutes. Currently fails to compile because the production store is not implemented.
- scripts/check_radio_packet_ids.py: actual native send bodies plus real wire codec, with observable allocator/AES/board substitutes. In minimal staging with the accepted allocator header, the original adapter compiles, then fails the missing persistent-initialization/allocation assertion. It records actual AES nonce inputs, not encryption correctness. The production allocator header is not integrated yet.
- Parent corrected init_sha fixture to use nonzero key data because normalizeMeshtasticChannelKeyLen treats an all-zero key as absent. The fault assertion was unchanged. Running worker inputs were not modified. The saved correction explicitly authorizes this single fixture correction before immutable verification.
- nonce-tasks contains the initial storage assignment, prepared correction1, adapter assignment, and declared storage API. These are implementation inputs, not an accepted patch. No new harness is wired into CI while implementation is missing. Existing verified checks remain unchanged.
- Native SDK mbedtls_sha256 API confirmed in official ESP-IDF5.5.4. Adapter baseline regression compiled and failed as expected. Public/default keys provide no confidentiality; backup rollback and historic-key reuse constraints are documented in NONCE-MIGRATION.md.

## Resume

Use canonical Spark worker under current local-model authorization; status must permit dispatch. Resume SAME task with nonce-tasks/store-correction-1.txt, deadline900, then inspect snapshot/diff and run native immutable verification with the corrected fixture. Do not reset the failed task or count it as a new first attempt. Original stage /home/zaidm/projects/trailmate-spark-nonce-store-20260907. Adapter stage /home/zaidm/projects/trailmate-spark-nonce-adapter-20260907 is prepared but not dispatched. Private evidence /home/zaidm/reviews/trailmate-nonce-20260907; raw native evidence remains in canonical runner runs/task/attempt-01.

After storage acceptance, dispatch adapter assignment, independently inspect all changed functions, integrate source list and CI checks, run both host harnesses and TFT/AMOLED builds. Then Astra reviews final state under user override, commits/pushes accepted implementation, and hands off physical reboot/power-loss/RF bench checks. Do not claim the nonce fix from an accepted allocator or plan alone.
