# Phase 6 Mesh Role Browser Checkpoint

Date: 2026-06-29

## Scope

- Added a first touch Mesh Roles browser sheet opened from the Packet-tab `Mesh Roles` card.
- Increased the app snapshot role previews to four room servers and four repeater candidates.
- The browser lists:
  - Room servers from signed heard-node adverts with role `room`.
  - Repeater candidates inferred from nonzero path-hop route/heard-node evidence.
- The sheet is read-only and scrollable; it does not add a new NVS namespace or RF/admin action.

## Validation Plan

1. Run host tests and diff hygiene.
2. Build with ESP-IDF in Podman.
3. Flash `COM7` without backup/readback per operator instruction.
4. Run normal smoke and verify `signal`, `roomservers`, `repeaters`, and `health`.
5. Run a crashlog clear/reboot proof to guard against the previous UI-start boot-loop class.
6. Run Public `test` against the local COM11 Meshcorebot and verify D1L packet movement plus bot counter deltas.

## Validation Results

- Host tests: `python -m pytest -q` passed with 69 tests.
- Diff hygiene: `git diff --check` passed.
- Local Podman ESP-IDF build passed; app image size was `0xa5440`, leaving 35% app partition free.
- Flash: `COM7` was flashed without backup/readback per operator instruction; esptool verified written hashes.
- Raw boot capture showed no assert, no reboot loop, and the `d1l>` prompt.
- Hardware smoke: `artifacts/smoke/d1l-smoke-mesh-role-browser-local-COM7.json`.
  - 28 commands passed.
  - `signal.sample_count=16`.
  - `roomservers.total_known=4`.
  - `repeaters.total_known=6`.
  - `health` reported `board_ready=true`, `ui_ready=true`, `current_task_stack_free_words=1120`, and `ui_task_stack_free_words=1356`.
- Boot-loop regression proof: `artifacts/smoke/d1l-mesh-role-browser-crashlog-clear-reboot-local-COM7.json`.
  - After `crashlog clear` and `reboot`, `crashlog` contained one `SW` reset entry with `crash_like=false`.
  - Post-reboot `health` reported `board_ready=true`, `ui_ready=true`, `current_task_stack_free_words=1120`, and `ui_task_stack_free_words=1288`.
- Public `test` RF regression: `artifacts/smoke/d1l-mesh-role-browser-public-test-regression-local-COM7.json`.
  - D1L added TX packet seq `13` and RX Public packet seq `14` for `test` at RSSI `-40`, SNR `30`, and `path_hops=1`.
  - COM11 Meshcorebot counters moved `rx_channel_total +3`, `relay_success_total +3`, `discord_send_success_total +3`, `rx_log_total +6`, and `rx_duplicate_total +3`.
  - `signal`, `roomservers`, `repeaters`, and `health` stayed healthy after the RF probe.

## Current Limits

- Manual physical touch confirmation of opening/scrolling/closing the role browser is still pending.
- Trace/ping helpers and richer filters remain pending.
