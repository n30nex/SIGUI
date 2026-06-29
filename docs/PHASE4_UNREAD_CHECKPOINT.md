# Phase 4 Unread State Checkpoint

Date: 2026-06-29

## Implemented

- Added a bounded NVS-backed read-state model for Public and DM message streams.
- Added `messages unread` and `messages read <public|dm|all>` JSONL serial diagnostics.
- Added unread counts to the app snapshot and Messages screen.
- Added a touch `Read` action on the Messages screen.
- Highlighted unread Public/DM RX rows as `new`.
- Counted muted DM unread rows separately from audible DM unread rows.
- Added `messages unread` to the hardware smoke command list.

## Local Validation

- `python -m pytest -q` passed with 56 tests.
- `git diff --check` passed.
- Podman ESP-IDF build passed with app size `0xa2290`, 37% free.
- Flashed `COM7` without a backup per operator instruction.
- Standard smoke passed: `artifacts/smoke/d1l-smoke-unread-local-COM7.json`.

## Hardware Proof

Focused probe: `artifacts/smoke/d1l-unread-public-local-COM7.json`.

- `messages read all` set the Public read cursor to `last_public_read_seq=7`.
- Baseline `messages unread` returned `public_unread=0`, `dm_unread=0`, and `muted_dm_unread=0`.
- `mesh send public test` produced fresh persisted RX rows:
  - seq `9`: relayed Public `test`, RSSI `-43`, SNR `30`.
  - seq `10`: `Krabs Node: Test OK CH0.`, RSSI `-42`, SNR `30`.
- `messages unread` rose to `public_unread=2` with `newest_public_rx_seq=10`.
- `messages read public` cleared Public unread to `0` and persisted `last_public_read_seq=10`.
- After reboot, `messages unread` still reported `public_unread=0`; `health` reported `reset_reason=SW`, `board_ready=true`, and `ui_ready=true`.

## Still Pending

- Per-thread DM read cursors; current DM read state is stream-wide.
- Controlled inbound DM unread proof; current hardware proof covers Public RX unread state.
- Manual touch review of the Messages `Read` button and highlighted rows on the physical screen.
