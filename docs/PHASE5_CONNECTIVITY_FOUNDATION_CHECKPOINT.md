# Phase 5 Connectivity Foundation Checkpoint

Date: 2026-06-29

## Completed

- Added a `connectivity_manager` that reports USB console readiness, persisted Wi-Fi/BLE/observer intent, build-time Wi-Fi/BLE availability, scan capability, and the safe coexistence policy.
- Kept Wi-Fi/BLE start/connect behavior disabled while replacing Phase 1 stubs with machine-readable Phase 5 status. The current ESP-IDF config compiles Wi-Fi support in, but the DeskOS runtime does not start it yet; BLE remains build-disabled.
- Made `wifi off` and `ble off` persist through the NVS settings model.
- Added `wifi status`, `wifi scan`, `wifi on`, `wifi off`, `ble status`, `ble on`, and `ble off` console handling. In this slice, `wifi on`/`ble on` report runtime-pending/build-disabled state and do not persist an enabled companion radio.
- Made `wifi scan` safe for smoke tests: it reports an empty network list and the exact reason scanning did not start instead of claiming a live scan.
- Added Settings-tab companion status showing Wi-Fi, BLE, USB readiness, and the offline-first coexistence policy.
- Extended the hardware smoke command list with Wi-Fi/BLE status coverage and the persistence test with Wi-Fi/BLE disabled checks.

## Commands Run

```powershell
python -m pytest -q
python .\scripts\smoke_d1l.py --dry-run --out artifacts\smoke\d1l-smoke-connectivity-dry-run.json
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py fullclean build"
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 8 --persistence-test --out artifacts\smoke\d1l-smoke-connectivity-foundation-final-local-COM7.json
```

## Results

- Host tests: `63 passed`.
- Smoke dry run: passed and lists `wifi status`, safe `wifi scan`, and `ble status`.
- Clean Podman ESP-IDF build passed; a follow-up incremental build after the `wifi on` guard passed with app size `0xa3940`, leaving 36% free.
- Final local app SHA-256: `546B1C59606187BD6BEF4F8EF8FC1CDF833A08CCA79559C391E8781EDE7B25DC`.
- Final local bootloader SHA-256: `79F09BCC926DEC8B1EF5BB989DDE171C326F5BDF111C459938923B7B21420350`.
- Final local partition-table SHA-256: `7F00B6C042A89B15B0CAC534F82ED988CAF29278FF5700B0C511EB1B5BB7C820`.
- `COM7` flash passed with esptool hash verification and no backup/read step.
- Hardware smoke passed: `artifacts/smoke/d1l-smoke-connectivity-foundation-final-local-COM7.json`.
  - `companion status` reported USB console ready, MeshCore 3-byte framing ready, Wi-Fi setting off/build available/runtime off, BLE setting off/build disabled/runtime off, and policy `offline_first_one_companion_radio`.
  - `wifi scan` returned `scan_started=false`, `networks=[]`, and reason `disabled_by_setting`.
  - Persistence test verified `wifi_enabled=false` and `ble_companion_enabled=false` before reboot, after reboot, and after cleanup reset.
- Focused guard probe passed: `artifacts/smoke/d1l-connectivity-on-guard-final-local-COM7.json`.
  - `wifi on` returned `WIFI_RUNTIME_PENDING` and left `wifi_enabled=false`.
  - `ble on` returned `BLE_BUILD_DISABLED` and left `ble_companion_enabled=false`.
  - Final `health` reported `reset_reason=SW`, `board_ready=true`, and `ui_ready=true`.

## Pending

- Later Phase 5 chunks can start the ESP Wi-Fi runtime for real scans, then add connection/OTA behavior after heap and coexistence are measured on hardware.
