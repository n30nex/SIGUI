# Phase 4 Heard Node Store Checkpoint

Date: 2026-06-29

## Completed

- Added a bounded NVS-backed heard-node store with 16 rows.
- Upserted verified MeshCore adverts by public-key fingerprint.
- Stored node name or fingerprint fallback, type, RSSI/SNR, path metadata, advert timestamp, first/last heard uptime, and heard count.
- Exposed recent heard nodes through `d1l_app_snapshot_t`.
- Updated the Nodes tab to render newest heard-node rows instead of a count-only placeholder.
- Added `nodes` and `nodes clear` serial diagnostics.
- Added `nodes` to smoke coverage.

## Commands Run

```powershell
python -m pytest tests
git diff --check
python .\scripts\smoke_d1l.py --dry-run
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 3 --out artifacts\smoke\d1l-smoke-phase4-heard-node-store-local-COM7.json
```

## Results

- Host tests: 39 passed.
- Diff whitespace check: passed.
- Smoke dry run: passed and includes `nodes`.
- Initial hardware smoke found a real boot loop: the enlarged app snapshot overflowed the ESP-IDF `main` task stack during initial UI render.
- Boot-loop fix: moved the UI app snapshot to static storage and added host contract coverage.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0x9d050`, 39% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification.
- Hardware smoke on `COM7`: passed after the stack fix; evidence is archived at `artifacts/smoke/d1l-smoke-phase4-heard-node-store-local-COM7.json`.
- Signed advert capture: passed; `artifacts/smoke/d1l-node-store-advert-window2-local-COM7.json` recorded `YKF 1W`, type `room`, fingerprint `9880BF9B9B1DD605`, RSSI `-39`, SNR `30`, and path hash metadata.
- Node store reboot persistence: passed; `artifacts/smoke/d1l-node-store-persistence-local-COM7.json` retained fingerprint `9880BF9B9B1DD605` across reboot.
- Post-reboot stability: passed; `artifacts/smoke/d1l-stability-phase4-heard-node-store-local-COM7.json` showed uptime increasing from `21667` to `66746` ms.
- Public RF regression: passed; `artifacts/smoke/d1l-public-rf-phase4-heard-node-store-local-COM7.json` moved local Meshcorebot counters by `rx_channel_total +6`, `relay_success_total +6`, and `discord_send_success_total +6`.

## Still Pending

- Large simulated mesh stress and full list virtualization.
- Contacts, routes, DMs, packet detail, touch radio editing, onboarding, and simulator/screenshot capture.
