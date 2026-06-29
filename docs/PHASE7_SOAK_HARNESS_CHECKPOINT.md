# Phase 7 Soak Harness Checkpoint

Date: 2026-06-29

## Scope

- Added `scripts/soak_d1l.py` for repeatable D1L stability windows after normal smoke validation.
- The runner samples `health`, `mesh status`, `signal`, `messages unread`, `packets`, and `crashlog`.
- Optional active mode sends `mesh send public <text>` at a fixed interval, so local MeshCore bots that answer Public `test` can prove fresh RF movement during the run.
- Reports summarize command failures, uptime monotonicity, board/UI/mesh readiness, TX/RX packet deltas, packet-log deltas, heap/PSRAM deltas and floors, stack-watermark floors, LVGL peak usage, and signal sample peak.

## Commands

Host/dry-run:

```powershell
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1
```

Short active hardware probe:

```powershell
$env:D1L_PORT = "COM7"
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 180 --sample-interval-sec 45 --active-public-text test --active-interval-sec 60 --require-rx-delta --min-tx-delta 1 --out artifacts\soak\d1l-soak-active-short-local-COM7.json
```

Full acceptance windows still require explicit operator scheduling:

```powershell
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 43200 --sample-interval-sec 300 --out artifacts\soak\d1l-soak-idle-12h-COM7.json
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 3600 --sample-interval-sec 60 --active-public-text test --active-interval-sec 120 --require-rx-delta --min-tx-delta 1 --out artifacts\soak\d1l-soak-active-1h-COM7.json
```

## Validation

- `python -m pytest tests\test_soak_d1l.py -q` passed.
- `python scripts\soak_d1l.py --dry-run ...` passed and wrote `artifacts\soak\d1l-soak-dry-run.json`.
- `python -m pytest -q` passed with 72 tests.
- Local Podman ESP-IDF build passed with `meshcore_deskos_d1l.bin` size `0xa5440`; smallest app partition free space remained `0x5abc0` bytes / 35%.
- Short active hardware soak on `COM7` passed and wrote `artifacts\soak\d1l-soak-active-short-local-COM7.json`.
  - 6 samples, 3 active Public `test` TX events, 0 command failures, and 0 threshold failures.
  - `mesh_tx_packet_delta=3`, `mesh_rx_packet_delta=8`, and `packet_total_written_delta=11`.
  - `board_ready_all=true`, `ui_ready_all=true`, `mesh_ready_all=true`, and `uptime_monotonic=true`.
  - `heap_free_delta=0`, `psram_free_delta=0`, stack floors were `current=1120` and `ui=1352` words, `lvgl_used_pct_peak=59`, and `signal_sample_count_peak=18`.

## Still Pending

- Full 12-hour idle/listening soak.
- Full 1-hour active messaging soak.
- Manual physical display/touch review remains separate from soak telemetry.
