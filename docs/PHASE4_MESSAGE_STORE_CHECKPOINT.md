# Phase 4 Public Message Store Checkpoint

Date: 2026-06-29

## Completed

- Added a bounded NVS-backed Public message store with 16 recent rows.
- Added persisted message fields for direction, author, text, RSSI/SNR, path metadata, and queued/received state.
- Wired MeshCore Public TX/RX into the store.
- Deferred TX persistence until radio TX-done so the serial command returns cleanly.
- Exposed recent Public rows through `d1l_app_snapshot_t`.
- Updated the Messages tab to render persisted Public rows instead of only volatile packet-log rows.
- Added `messages public` and `messages clear` serial diagnostics.
- Added `messages public` to smoke coverage.

## Commands Run

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 8 --out artifacts\smoke\d1l-smoke-phase4-message-store-local-COM7.json
```

The reboot persistence probe is archived at `artifacts/smoke/d1l-message-store-persistence-local-COM7.json`.

## Results

- Host tests: 34 passed.
- Smoke dry run: passed and includes `messages public`.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0x9c450`, 39% free in the app partition.
- Hardware smoke on `COM7`: passed; evidence is archived at `artifacts/smoke/d1l-smoke-phase4-message-store-local-COM7.json`.
- Public message persistence on `COM7`: passed; Public `test` TX and `Krabs Node: Test OK CH0.` RX rows remained in `messages public` after reboot, while volatile `packets` reset from `count=2` to `count=0`.
- Local Meshcorebot observed fresh Public counter movement during persistence validation: `rx_channel_total +2`, `relay_success_total +2`, and `discord_send_success_total +2`.

## Still Pending

- Free-text Public composer.
- DMs.
- Persistent contacts, nodes, routes, packet detail, touch radio editing, onboarding, and simulator/screenshot capture.
