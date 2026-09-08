# Packet-counter migration and recovery

This procedure applies when the native Meshtastic persistent-counter repair is installed. It is not yet a hardware-verified release. Private keys are exclusive to Trail Mate, as confirmed by the owner.

## First upgrade

1. Keep firmware and SD backups, but treat restored NVS/channel keys as old security state. Upgrade all participating Trail Mate units before enabling the new private channel keys.
2. The first configuration load records fingerprints of existing explicit channel keys, including disabled slots. Outgoing packets using those keys are refused. Reception remains available. Changing the channel name, slot, or enable switch does not remove the block.
3. Provision a fresh private channel key on every unit that should communicate. Generate a new strong key or a new strong passphrase supported by the existing channel settings; do not reuse a previous derived key, QR code, or channel export. Never record private keys in Git, audit reports, screenshots, or serial logs.
4. Verify private messages between units, reboot both, and verify messaging again. Confirm the emitted packet IDs advance past the range reserved before reboot. Run the nonce bench checks before field use.

## Storage errors and recovery

The counter reserves 1,024 IDs per durable write; unused IDs are skipped on reboot. Failed sends consume IDs. Storage/hash errors block transmission for the rest of that boot. Fix the storage condition and reboot; do not erase only the counter to regain transmission. Exhaustion blocks transmission rather than wrapping.

After NVS erasure, backup restoration, or using older firmware on the same identity, provision fresh private keys across the group. A software NVS record cannot reliably detect rollback of a complete backup. Removed historic keys and old imports also cannot be reconstructed from current channel configuration. Only fresh keys may be imported after migration. Keep these private keys exclusive to the repaired Trail Mate firmware. Public/default channel keys do not provide confidentiality.

No private key is automatically generated, changed or distributed by the repair. Previously captured traffic is not retroactively protected.
