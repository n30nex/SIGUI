# Phase 4 Route Store Checkpoint

Date: 2026-06-29

## Completed

- Added a bounded NVS-backed route store with 16 rows.
- Added `routes` and `routes clear` serial diagnostics.
- Learned route observations from MeshCore Public RX/TX and signed advert RX/TX path metadata.
- Stored target, label, packet kind, route name, direction, RSSI/SNR, path hash bytes, hops, confidence, payload length, first/last seen uptime, and seen count.
- Exposed route count and a bounded recent route preview through `d1l_app_snapshot_t`.
- Updated the Home RF card to include route count.
- Added `routes` to smoke coverage.
- Moved large USB console preview buffers out of the main task stack after a route diagnostic repro exposed stack pressure during Public RF testing.

## Commands Run

```powershell
$env:PYTHONPATH='.'; pytest -q
python .\scripts\smoke_d1l.py --dry-run --out artifacts\smoke\d1l-smoke-dryrun-phase4-route-store.json
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 6 --out artifacts\smoke\d1l-smoke-phase4-route-store-local-COM7.json
```

## Results

- Host tests: 46 passed.
- Smoke dry run: passed and includes `routes`.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0x9e790`, 38% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification.
- Hardware smoke on `COM7`: passed; evidence is archived at `artifacts/smoke/d1l-smoke-phase4-route-store-local-COM7.json`.
- Route-store persistence: passed in `artifacts/smoke/d1l-route-store-persistence-window4-local-COM7.json`; Public TX/RX route rows and the local bot `Test OK` reply survived reboot with `reset_reason=SW`.
- Post-reboot stability: passed; `artifacts/smoke/d1l-stability-phase4-route-store-local-COM7.json` showed uptime increasing from `44852` to `90352` ms with route rows retained and `reset_reason=SW`.
- Public RF regression: passed; `artifacts/smoke/d1l-public-rf-phase4-route-store-local-COM7.json` moved local Meshcorebot counters by `rx_channel_total +4`, `relay_success_total +4`, and `discord_send_success_total +4`, and the D1L received `Krabs Node: Test OK CH0.` replies.

## Still Pending

- GitHub Actions artifact flash and revalidation.
- Route detail views, trace/ping helpers, DMs, packet detail, touch radio editing, onboarding, and simulator/screenshot capture.
