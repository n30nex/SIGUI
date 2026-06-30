# Phase 4 DM Thread History Checkpoint

Date: 2026-06-30

## Scope

- Added `d1l_dm_store_copy_thread()` so firmware can copy all retained rows for one DM fingerprint instead of only the global recent preview.
- Added `d1l_app_model_copy_dm_thread()` to pair retained thread rows with per-row unread state.
- Changed the DM thread sheet to render a bounded scrollable retained-history list for the selected fingerprint while keeping `Reply`, per-thread `Read`, and `Close` fixed on the sheet.
- Added `messages dm <fingerprint>` as a serial diagnostic for one retained thread; `messages dm` still returns the recent overview and `messages dm clear` still clears the store.
- Updated the UI simulator so the large-mesh scenario keeps 32 DM source rows while rendering only the visible bounded thread rows.

## Validation

```powershell
python -m pytest tests\test_dm_store_contract.py tests\test_ui_shell_contract.py tests\test_ui_simulator.py -q
python -m pytest -q
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --out artifacts\smoke\d1l-smoke-dm-thread-history-local-COM7.json
python .\scripts\soak_d1l.py --port COM7 --duration-sec 180 --sample-interval-sec 30 --active-public-text test --active-interval-sec 60 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start --out artifacts\soak\d1l-soak-dm-thread-history-rf-local-COM7.json
python .\scripts\package_release_d1l.py --build-dir build --out-dir artifacts\release --package-name d1l-release-dm-thread-history-local
```

Result:

- Focused host contract tests: `22 passed`.
- Full host test suite: `83 passed`.
- Default simulator returned `ok=true`, zero overflow, and DM thread source/rendered counts of `2/2`.
- `large-mesh` simulator returned `ok=true`, zero overflow, and DM thread source/rendered counts of `32/3`.
- Smoke and soak dry-runs passed.
- Local Podman ESP-IDF build passed; `meshcore_deskos_d1l.bin` size was `0xa88d0`, leaving 34% free in the smallest app partition.
- Local app SHA256: `81EEE3D06D3C2D3E83FDDE4A23EB11E3689000B42A655EC05DB692FE45E96569`.
- COM7 project flash passed; esptool verified written hashes. No backup/readback was taken.
- COM7 smoke passed with `messages dm` returning `filtered=false` and the retained YKF Corebot DM row.
- Targeted serial proof passed: `messages dm 0BF0A701D5AE2DB6` returned `filtered=true`, `thread_count=1`, and only the retained YKF Corebot row; `help` advertised `messages dm [fingerprint]`; `health` reported `board_ready=true` and `ui_ready=true`.
- 3-minute active Public `test` RF regression passed with 8 samples, 3 queued Public TX events, `mesh_tx_packet_delta=3`, `mesh_rx_packet_delta=2`, zero command failures, zero retries, monotonic uptime, and `crashlog_count_peak=0` after start clear.
- Local release package `artifacts\release\d1l-release-dm-thread-history-local` was generated and `SHA256SUMS.txt` verified locally.

## Still Pending

- Manual physical touch review of opening, scrolling, replying, and marking a DM thread read.
- Controlled inbound DM proof on real RF.
- Controlled ACK/PATH RF proof and direct-route RF proof.
