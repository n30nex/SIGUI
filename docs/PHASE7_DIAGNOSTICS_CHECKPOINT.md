# Phase 7 Diagnostics Checkpoint

Date: 2026-06-29

## Completed

- Added a bounded NVS-backed crash/reset ring that records each boot reset reason, early heap/PSRAM watermarks, and whether the reset was crash-like.
- Replaced the `crashlog` stub with machine-readable `crashlog` and `crashlog clear` commands.
- Expanded `health` with largest heap block, PSRAM minimum/largest block, current console task stack watermark, UI task stack watermark, and LVGL memory usage.
- Registered the LVGL/UI task with the health monitor so boot-loop and stack-pressure checks have direct evidence.
- Added Settings/Home UI reset/stack visibility through the app snapshot model.
- Added smoke coverage for `crashlog`.

## Commands Run

```powershell
python -m pytest -q
python .\scripts\smoke_d1l.py --dry-run --out artifacts\smoke\d1l-smoke-diagnostics-dry-run.json
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 8 --out artifacts\smoke\d1l-smoke-diagnostics-local-COM7.json
```

## Results

- Host tests: `66 passed`.
- Smoke dry run: passed and lists `crashlog`.
- Podman ESP-IDF build passed with app size `0xa43f0`, leaving 36% free.
- Final local app SHA-256: `168C23770D470079BD887849A45E308163F6DF16FF7D9C7A94704CD19FA43620`.
- Final local bootloader SHA-256: `79F09BCC926DEC8B1EF5BB989DDE171C326F5BDF111C459938923B7B21420350`.
- Final local partition-table SHA-256: `7F00B6C042A89B15B0CAC534F82ED988CAF29278FF5700B0C511EB1B5BB7C820`.
- `COM7` flash passed with esptool hash verification and no backup/read step.
- Hardware smoke passed: `artifacts/smoke/d1l-smoke-diagnostics-local-COM7.json`.
  - `crashlog` returned two persisted `POWERON` reset entries after flash/reset.
  - `health` reported `heap_largest_free=7208960`, `current_task_stack_free_words=1136`, `ui_task_stack_free_words=1292`, `lvgl_used_pct=58`, `board_ready=true`, and `ui_ready=true`.
- Reboot-ring proof passed: `artifacts/smoke/d1l-diagnostics-crashlog-reboot-local-COM7.json`.
  - `crashlog clear` produced `count=0`.
  - After `reboot`, `crashlog` contained a fresh `SW` reset entry with `crash_like=false`.
  - Post-reboot `health` reported `reset_reason=SW`, nonzero stack watermarks, `board_ready=true`, and `ui_ready=true`.
- Public `test` RF regression passed: `artifacts/smoke/d1l-diagnostics-public-test-regression-final-local-COM7.json`.
  - Packet log total increased by 1 with a fresh TX `public_text` row.
  - COM11 Meshcorebot counters moved `rx_channel_total +2`, `relay_success_total +2`, `discord_send_success_total +2`, `rx_log_total +2`, and `rx_duplicate_total +2`.
  - Final `health` stayed at `board_ready=true`, `ui_ready=true`, and nonzero stack watermarks.

## Pending

- Later Phase 7 chunks still need a long idle/listening soak and an active messaging soak.
