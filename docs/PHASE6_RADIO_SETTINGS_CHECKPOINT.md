# Phase 6 Radio Settings Checkpoint

Date: 2026-06-29

## Implemented

- Added persisted radio profile fields to the app snapshot:
  - frequency
  - bandwidth
  - spreading factor
  - coding rate
  - TX power
  - RX boost
  - TCXO label
- Added an app-model radio profile save path with validation for the Canada/USA operating range and D1L TX-power limit.
- Added `settings get` radio profile output.
- Added serial commands:
  - `radio set txpower <dbm>`
  - `radio set rxboost <0|1>`
- Added a touch Radio Settings sheet from the Settings tab.
- The sheet stages frequency/BW/SF/CR/TX/RX-boost edits, offers `US/CAN`, requires `Save`, and warns that saved settings need reboot/apply before live RF changes.
- Extended the host UI simulator to cover the Radio Settings sheet.

## Host Validation

```powershell
python .\tools\ui_simulator.py --out artifacts\ui-sim
python -m pytest tests\test_ui_shell_contract.py tests\test_ui_simulator.py tests\test_settings_contract.py tests\test_smoke_d1l_parser.py -q
```

Results:

- Simulator: 16 views, `ok=true`, no required-label misses or text overflow.
- Focused tests: 22 passed.
- Full tests: 80 passed.

## Hardware Validation

Build:

- Podman ESP-IDF build passed in `build/radio-settings`.
- App image: `build/radio-settings/meshcore_deskos_d1l.bin`
- App size: `696384` bytes.
- App SHA256: `00752E6B570C6030DFB79A8C02E93D97F44AC7308AB59EDB785D94A9178DF5A0`.

Flash:

- Flashed `COM7` directly with `esptool.py` at 460800 baud.
- No backup/readback was performed, per operator instruction.
- Bootloader, partition table, and app hashes were verified by esptool.

Smoke:

- `artifacts/smoke/d1l-smoke-radio-settings-local-COM7.json`
- Standard smoke passed with 32 commands.
- `settings get` reported the nested `radio` object with default US/CAN profile fields.
- `health` reported `board_ready=true`, `ui_ready=true`, reset reason `POWERON`, current stack `1112` words, UI stack `1252` words, and `lvgl_used_pct=80`.

Targeted radio settings proof:

- `artifacts/smoke/d1l-radio-settings-target-local-COM7.json`
- `radio set txpower 19` persisted `tx_power_dbm=19` with `applied_to_radio=false`.
- `radio set rxboost 0` persisted `radio.rx_boost=false` in `settings get`.
- `radio set preset uscan` restored 910.525 MHz, BW62.5, SF7, CR5, 20 dBm, RX boost enabled.
- Final `health` stayed `board_ready=true` and `ui_ready=true`.

RF regression:

- `artifacts/soak/d1l-soak-radio-settings-active-local-COM7.json`
- 90 second active Public `test` soak passed.
- 3 queued Public TX events, `mesh_tx_packet_delta=3`, `mesh_rx_packet_delta=4`, `packet_total_written_delta=7`.
- No command failures or threshold failures; uptime stayed monotonic.
- Stack floors stayed nonzero: current task `1032` words, UI task `1204` words.
- `lvgl_used_pct_peak=80`, `heap_free_delta=-16`, `psram_free_delta=0`.

Package:

- `artifacts/release/d1l-release-radio-settings-local`
- `python .\scripts\verify_checksums.py artifacts\release\d1l-release-radio-settings-local` passed.

## Still Pending

- Manual touch open/change/defaults/save/close review of the Radio Settings sheet.
