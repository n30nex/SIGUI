# Phase 4 Contact Store Checkpoint

Date: 2026-06-29

## Completed

- Added a bounded NVS-backed contact store with 16 rows.
- Added `contacts`, `contacts add <fingerprint> [alias]`, and `contacts clear` serial diagnostics.
- Promoted heard nodes into contacts by 16-hex public-key fingerprint.
- Preserved alias, heard name, type, RSSI/SNR, path metadata, favorite, and mute fields.
- Exposed a bounded recent contact preview through `d1l_app_snapshot_t`.
- Updated the Nodes tab to show contact count and newest contact rows when present.
- Added `contacts` to smoke coverage.

## Commands Run

```powershell
$env:PYTHONPATH='.'; pytest -q
python .\scripts\smoke_d1l.py --dry-run --out artifacts\smoke\d1l-smoke-dryrun-phase4-contact-store.json
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 5 --out artifacts\smoke\d1l-smoke-phase4-contact-store-local-COM7.json
```

## Results

- Host tests: 42 passed.
- Smoke dry run: passed and includes `contacts`.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0x9dce0`, 38% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification.
- Hardware smoke on `COM7`: passed; evidence is archived at `artifacts/smoke/d1l-smoke-phase4-contact-store-local-COM7.json`.
- Contact promotion and reboot persistence: passed; `artifacts/smoke/d1l-contact-store-persistence-local-COM7.json` promoted fingerprint `937D290883817CBD` from heard node `Krabs Lagoon` and retained the row after reboot.
- Post-reboot stability: passed; `artifacts/smoke/d1l-stability-phase4-contact-store-local-COM7.json` showed uptime increasing from `89402` to `134479` ms with the contact row retained.
- Public RF regression: passed; `artifacts/smoke/d1l-public-rf-phase4-contact-store-local-COM7.json` moved local Meshcorebot counters by `rx_channel_total +6`, `relay_success_total +6`, and `discord_send_success_total +6`, and the D1L received `Krabs Node: Test OK CH0.` replies.

## Still Pending

- Contact detail cards and edit/favorite/mute actions.
- Routes, DMs, packet detail, touch radio editing, onboarding, and simulator/screenshot capture.
