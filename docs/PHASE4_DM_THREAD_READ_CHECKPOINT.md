# Phase 4 DM Thread Read Checkpoint

Date: 2026-06-30

## Scope

- Added bounded per-thread DM read cursors to `read_state`.
- Migrated the persisted read-state blob from schema v1 to schema v2 without dropping the existing Public or global DM read cursors.
- Kept `messages read dm` as the global DM read action and added `messages read dm <fingerprint>` for a single DM thread.
- Extended `messages unread` with bounded `dm_threads` summaries that include each thread fingerprint, newest RX sequence, last read sequence, unread count, and mute state.
- Added per-row DM unread flags to the app snapshot so the Messages tab and DM thread sheet use per-thread cursors.
- Added a `Read` action to the DM thread sheet and simulator coverage.

## Validation

```powershell
python -m pytest tests\test_read_state_contract.py tests\test_ui_shell_contract.py tests\test_ui_simulator.py -q
python -m pytest -q
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\meshcore_deskos_d1l.bin
python .\scripts\smoke_d1l.py --port COM7 --out artifacts\smoke\d1l-smoke-dm-thread-read-local-COM7.json
python .\scripts\soak_d1l.py --port COM7 --duration-sec 180 --sample-interval-sec 30 --active-public-text test --active-interval-sec 60 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start --out artifacts\soak\d1l-soak-dm-thread-read-rf-local-COM7.json
```

Result:

- Focused host contract tests: `20 passed`.
- Full host test suite: `83 passed`.
- Default simulator and `large-mesh` simulator both returned `ok=true` with zero overflow.
- Local Podman ESP-IDF build passed; `meshcore_deskos_d1l.bin` size was `0xa85f0`, leaving 34% free in the smallest app partition.
- Local app SHA256: `F86FBDDB2DA2D818E62BE85AEF143F6C89C80AF122E8A13541E7FF504D9B5241`.
- COM7 project flash passed; esptool verified written hashes. No backup/readback was taken.
- COM7 smoke passed with 32 commands; `mesh status` reported `ready`, `health` reported `board_ready=true` and `ui_ready=true`, and `messages unread` included `dm_threads`.
- Targeted serial proof passed: `messages read dm 0BF0A701D5AE2DB6` returned `ok=true`, `target="dm_thread"`, and the help command advertised `messages read <public|dm|dm <fingerprint>|all>`.
- 3-minute active Public `test` RF regression passed with 8 samples, 3 queued Public TX events, `mesh_tx_packet_delta=3`, `mesh_rx_packet_delta=4`, zero command failures, zero retries, monotonic uptime, and `crashlog_count_peak=0` after start clear.

## Still Pending

- Controlled inbound DM proof on real RF.
- Manual physical touch review of the DM thread `Read` action.
- Richer full-thread history beyond the bounded recent preview.
