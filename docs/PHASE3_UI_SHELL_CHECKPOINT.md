# Phase 3 UI Shell Checkpoint

Date: 2026-06-29

## Completed

- Replaced the diagnostic tile home with a dark 480x480 MeshCore DeskOS shell.
- Added an app snapshot contract, `d1l_app_snapshot_t`, so the UI reads bounded state from the app model instead of calling MeshCore or HAL services directly.
- Added Home, Messages/Public, Nodes, Packets, and Settings tabs behind the bottom dock.
- Added top status, RF counters, identity display, advert modal sheet, touch feedback toasts, and lock overlay.
- Routed touch actions for Public `test` and advert zero/flood through app model wrappers.
- Kept serial diagnostics intact for hardware smoke and recovery.

## Commands Run

```powershell
python -m pytest tests
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 8 --out artifacts\smoke\d1l-smoke-phase3-ui-shell-COM7.json
```

The controlled RF probe window was archived as `artifacts/smoke/d1l-rf-phase3-ui-shell-COM7.json`.

## Results

- Host tests: 31 passed.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0x9baf0`, 39% free in the app partition.
- Hardware smoke on `COM7`: passed; evidence is archived at `artifacts/smoke/d1l-smoke-phase3-ui-shell-COM7.json`.
- Controlled Public RF regression: passed; the local Meshcorebot response path moved `rx_channel_total +2`, `relay_success_total +2`, and `discord_send_success_total +2`, and the D1L decoded Public replies including `Krabs Node: Test OK CH0.` Evidence is archived at `artifacts/smoke/d1l-rf-phase3-ui-shell-COM7.json`.

## Still Pending

- Manual visual confirmation of the physical shell on the D1L screen.
- Full free-text Public composer, DMs, persistent contacts/nodes/routes, packet detail, touch radio editing, onboarding, and simulator/screenshot capture.
