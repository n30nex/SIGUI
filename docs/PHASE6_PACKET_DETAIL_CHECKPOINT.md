# Phase 6 Packet Detail Checkpoint

Date: 2026-06-29

## Implemented

- Converted the packet log from RAM-only to a bounded NVS-backed evidence store.
- Kept the live packet ring at 32 rows and persisted the newest 8 rows to stay within the current D1L NVS budget.
- Added `packets detail <seq>` and `packets clear` JSONL serial diagnostics.
- Added touchable Packet-tab rows and a first packet detail sheet with direction, kind, payload length, RSSI/SNR, path hash bytes, hops, uptime, and note text.

## Local Validation

- `python -m pytest -q` passed with 60 tests.
- `git diff --check` passed.
- Podman ESP-IDF build passed with app size `0xa3100`, 36% free.
- Flashed `COM7` without a backup per operator instruction.
- Final standard smoke passed: `artifacts/smoke/d1l-smoke-packet-detail-final-local-COM7.json`.

## Hardware Proof

Focused probe: `artifacts/smoke/d1l-packet-detail-persistence-final-local-COM7.json`.

- `packets clear` reset the persisted packet log to `count=0`.
- `mesh send public test` produced packet rows:
  - seq `1`: Public TX `test`.
  - seq `2`: second Public TX `test`.
  - seq `3`: relayed Public RX `test`, RSSI `-44`, SNR `30`, `path_hash_bytes=1`, `path_hops=1`, `payload_len=22`.
- `packets detail 3` returned the same RX row.
- After `reboot`, `packets` retained all three rows and `health` reported `reset_reason=SW`, `board_ready=true`, and `ui_ready=true`.
- COM11 Meshcorebot observed the RF path: `rx_channel_total +4`, `relay_success_total +4`, `discord_send_success_total +4`, `rx_log_total +6`, and `rx_duplicate_total +4`.

## Note

An earlier full-32-row NVS blob build listed packets before reboot but reloaded empty after reboot on the already-used NVS partition. The final implementation persists the newest 8 packet rows and passed reboot validation.

## Still Pending

- Manual touch review of tapping packet rows and closing the packet detail sheet on the physical screen.
- Richer packet filtering/search and raw packet hex developer mode were implemented and validated later in `docs/PHASE6_PACKET_FILTER_CHECKPOINT.md`.
