# Phase 6 Contact Export Checkpoint

Date: 2026-06-29

## Implemented

- Added MeshCore-compatible contact export URIs for promoted contacts that have a retained 64-hex public key:
  - `meshcore://contact/add?name=<url-encoded-name>&public_key=<64-hex-key>&type=<type-id>`
- Added `contacts export` as a smoke-safe serial command that lists recent contacts and marks whether each is shareable.
- Added `contacts export <fingerprint>` for exact QR payload export.
- Added a Contact Detail `Export` action and a Contact Export sheet.
- Enabled LVGL QR rendering in `sdkconfig.defaults` and render the export URI as a QR when `LV_USE_QRCODE` is available.
- Extended the host UI simulator to cover the Contact Export sheet.

## Host Validation

```powershell
python .\tools\ui_simulator.py --out artifacts\ui-sim
python -m pytest tests\test_contact_store_contract.py tests\test_ui_shell_contract.py tests\test_ui_simulator.py tests\test_smoke_d1l_parser.py -q
```

Results:

- Simulator: 15 views, `ok=true`, no missing required labels, no text overflow, no text truncation.
- Focused tests: 21 passed.
- Full tests: 79 passed.

## Hardware Validation

Build:

- Podman ESP-IDF build passed in a fresh QR-enabled build directory:
  - `CONFIG_LV_USE_QRCODE=y`
  - `lv_qrcode.c` and `qrcodegen.c` compiled.
  - App image: `build/contact-export/meshcore_deskos_d1l.bin`
  - App size: `693360` bytes.
  - App SHA256: `93E6713967CDA5C45A3E2C8ABC70F4D993485668CD325561C0FA2DEF18884CC8`.

Flash:

- Flashed `COM7` directly with `esptool.py` at 460800 baud.
- No backup/readback was performed, per operator instruction.
- Bootloader, partition table, and app hashes were verified by esptool.

Smoke:

- `artifacts/smoke/d1l-smoke-contact-export-local-COM7.json`
- Standard smoke passed with 32 commands, including `contacts export`.
- `contacts export` returned one shareable `YKF Corebot` entry:
  - fingerprint `0BF0A701D5AE2DB6`
  - `public_key` length 64
  - `type_id=1`
  - URI prefix `meshcore://contact/add?`

Targeted export:

- `artifacts/smoke/d1l-contact-export-target-local-COM7.json`
- `contacts export 0BF0A701D5AE2DB6` passed.
- URI checks passed for prefix, name, public key, type, and 64-hex key length.
- Final `health` stayed `board_ready=true` and `ui_ready=true`.

RF regression:

- `artifacts/soak/d1l-soak-contact-export-active-local-COM7.json`
- 90 second active Public `test` soak passed.
- 3 queued Public TX events, `mesh_tx_packet_delta=3`, `mesh_rx_packet_delta=2`, `packet_total_written_delta=5`.
- No command failures or threshold failures; uptime stayed monotonic.
- Stack floors stayed nonzero: current task `1016` words, UI task `1248` words.

## Still Pending

- Manual touch open/close review of the Contact Export sheet on the physical display.
- Manual scan/import proof with a MeshCore client.
