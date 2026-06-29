# Phase 4 Route Detail Checkpoint

Date: 2026-06-29

## Implemented

- Added `d1l_route_store_find_by_seq()` for bounded route lookup by sequence.
- Added `routes detail <seq>` JSONL serial diagnostics.
- Added touchable route rows to the Packet tab route section.
- Added a first route detail sheet showing target, kind, route, direction, RSSI/SNR, path metadata, confidence, payload length, and seen timestamps.

## Local Validation

- `python -m pytest -q` passed with 57 tests.
- `git diff --check` passed.
- Podman ESP-IDF build passed with app size `0xa2940`, 36% free.
- Flashed `COM7` without a backup per operator instruction.
- Standard smoke passed: `artifacts/smoke/d1l-smoke-route-detail-local-COM7.json`.

## Hardware Proof

Focused probe: `artifacts/smoke/d1l-route-detail-public-window2-local-COM7.json`.

- `mesh send public test` produced fresh persisted rows:
  - message seq `13`: relayed Public `test`, RSSI `-44`, SNR `30`.
  - route seq `18`: Public TX route row.
  - route seq `19`: Public RX route row.
- `routes detail 19` returned the same Public RX route row with `route="flood"`, `direction="rx"`, `path_hash_bytes=1`, `path_hops=1`, `confidence=80`, and `payload_len=22`.
- COM11 Meshcorebot observed the RF path: `rx_channel_total +2`, `relay_success_total +2`, `discord_send_success_total +2`, `rx_log_total +4`, and `rx_duplicate_total +2`.
- `health` stayed up from `143641` ms to `146448` ms with `reset_reason=POWERON`, `board_ready=true`, and `ui_ready=true`.

## Still Pending

- Manual touch review of tapping route rows and closing the route detail sheet on the physical screen.
- Richer trace/ping helpers and controlled PATH RF proof.
