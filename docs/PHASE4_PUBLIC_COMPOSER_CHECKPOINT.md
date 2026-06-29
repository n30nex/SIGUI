# Phase 4 Public Composer Checkpoint

Date: 2026-06-29

## Completed

- Added an app-model wrapper for dynamic Public text sends.
- Added a Public composer sheet with LVGL textarea, on-screen keyboard, Send, Clear, and Close actions.
- Kept the fixed Public `test` quick action beside the composer for known local bot responder checks.
- Routed keyboard ready/cancel events through the composer send/close flow.
- Added host contract coverage for the composer wiring.
- Updated roadmap, UI spec, test plan, limitations, release checklist, and hardware validation notes.

## Commands Run

```powershell
python -m pytest tests
git diff --check
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --timeout 3 --out artifacts\smoke\d1l-smoke-phase4-public-composer-local-COM7.json
```

The local Public responder probe is archived at `artifacts/smoke/d1l-public-composer-rf-local-COM7.json`.

## Results

- Host tests: 35 passed.
- Diff whitespace check: passed.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0x9c7b0`, 39% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification.
- Hardware smoke on `COM7`: passed; `health` reported `reset_reason=POWERON`, `board_ready=true`, and `ui_ready=true`.
- Public RF regression: passed; local Meshcorebot on `COM11` saw `rx_channel_total +2`, `relay_success_total +2`, and `discord_send_success_total +2`.
- D1L persisted the Public TX `test` row and RX rows including `Krabs Node: Test OK CH0.`.

## Still Pending

- Manual physical touch entry review on the Public composer keyboard.
- DMs.
- Persistent contacts, nodes, routes, packet detail, touch radio editing, onboarding, and simulator/screenshot capture.
