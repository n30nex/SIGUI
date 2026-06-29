# Phase 6 Mesh Visibility Checkpoint

Date: 2026-06-29

## Scope

- Added a read-only mesh inspector over existing bounded packet, route, and heard-node stores.
- Added serial diagnostics:
  - `signal`
  - `roomservers`
  - `repeaters`
- Added Home and Packet-tab summary cards for latest RSSI/SNR, room-server count, repeater-candidate count, and evidence sample count.
- Added smoke coverage for the new commands.

## Data Model

- `signal` summarizes recent RX packet, route, and heard-node RSSI/SNR evidence.
- `roomservers` reports signed heard-node adverts whose stored role is `room`.
- `repeaters` infers candidates from nonzero path-hop route evidence, excluding Public route rows, plus heard-node rows with nonzero path hops.
- No new NVS namespace or persistent write path was added in this slice.

## Hardware Validation Plan

1. Build with ESP-IDF in Podman.
2. Flash `COM7` without a backup/readback per operator instruction.
3. Run the normal smoke script and verify `signal`, `roomservers`, and `repeaters` all return parseable JSON.
4. Send Public `test`, wait for local MeshCore bot replies, and verify:
   - D1L packet count increases.
   - `signal.sample_count > 0`.
   - `roomservers.total_known >= 1` when local room adverts are present.
   - `repeaters.total_known >= 1` when path-hop evidence is present.
   - COM11 Meshcorebot counters move for `rx_channel_total`, `relay_success_total`, and `discord_send_success_total`.
5. Confirm `health` remains `board_ready=true`, `ui_ready=true`, and reports nonzero stack watermarks.

## Validation Results

- Host tests: `python -m pytest -q` passed with 69 tests.
- Diff hygiene: `git diff --check` passed.
- Dry-run smoke: `artifacts/smoke/d1l-smoke-mesh-visibility-dry-run.json`.
- Local Podman ESP-IDF build passed; final app image size was `0xa4ff0`, leaving 36% app partition free.
- Flash: `COM7` was flashed without backup/readback per operator instruction; esptool verified written hashes.
- Initial stack regression found and fixed before commit:
  - Raw serial captured a boot loop at `vTaskGenericNotifyGiveFromISR` during initial UI creation.
  - Root cause was the mesh inspector allocating route/node scratch buffers on the caller stack during the first app snapshot.
  - Fix moved inspector scratch buffers to file-scope static storage and added a host contract assertion.
- Hardware smoke: `artifacts/smoke/d1l-smoke-mesh-visibility-local-COM7.json`.
  - `signal.sample_count=17`.
  - `roomservers.total_known=4`.
  - `repeaters.total_known=6`.
  - `health` reported `board_ready=true`, `ui_ready=true`, and nonzero stack watermarks.
- Boot-loop regression proof: `artifacts/smoke/d1l-mesh-visibility-crashlog-clear-reboot-local-COM7.json`.
  - After `crashlog clear` and `reboot`, `crashlog` contained one `SW` reset entry with `crash_like=false`.
  - Post-reboot `health` reported `board_ready=true`, `ui_ready=true`, `current_task_stack_free_words=1120`, and `ui_task_stack_free_words=1256`.
- Public `test` RF regression: `artifacts/smoke/d1l-mesh-visibility-public-test-regression-local-COM7.json`.
  - D1L added TX packet seq `11` and RX Public packet seq `12` for `test` at RSSI `-41`, SNR `30`, and `path_hops=1`.
  - COM11 Meshcorebot counters moved `rx_channel_total +2`, `relay_success_total +2`, `discord_send_success_total +2`, `rx_log_total +4`, and `rx_duplicate_total +2`.
  - `signal`, `roomservers`, `repeaters`, and `health` stayed healthy after the RF probe.

## Current Limits

- Dedicated full-screen room-server and repeater browsers are not built yet.
- Repeater inference is evidence-based and conservative; it is not a MeshCore admin/management action.
- Trace/ping helpers remain pending.
