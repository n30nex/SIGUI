# Phase 7 Soak Harness Checkpoint

Date: 2026-06-29

## Scope

- Added `scripts/soak_d1l.py` for repeatable D1L stability windows after normal smoke validation.
- The runner samples `health`, `mesh status`, `signal`, `messages unread`, `packets`, and `crashlog`.
- Optional active mode sends `mesh send public <text>` at a fixed interval, so local MeshCore bots that answer Public `test` can prove fresh RF movement during the run.
- Reports summarize command failures, bounded command retries, uptime monotonicity, board/UI/mesh readiness, TX/RX packet deltas, packet-log deltas, heap/PSRAM deltas and floors, stack-watermark floors, LVGL peak usage, signal sample peak, and crash-like reset entries from the reset ring.

## Commands

Host/dry-run:

```powershell
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start
```

Short active hardware probe:

```powershell
$env:D1L_PORT = "COM7"
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 180 --sample-interval-sec 45 --active-public-text test --active-interval-sec 60 --require-rx-delta --min-tx-delta 1 --out artifacts\soak\d1l-soak-active-short-local-COM7.json
```

Full acceptance windows still require explicit operator scheduling:

```powershell
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 43200 --sample-interval-sec 300 --out artifacts\soak\d1l-soak-idle-12h-COM7.json
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 3600 --sample-interval-sec 60 --active-public-text test --active-interval-sec 120 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start --out artifacts\soak\d1l-soak-active-1h-COM7.json
```

## Validation

- `python -m pytest tests\test_soak_d1l.py -q` passed.
- `python scripts\soak_d1l.py --dry-run ...` passed and wrote `artifacts\soak\d1l-soak-dry-run.json`.
- `python -m pytest -q` passed with 82 tests after the crash/reset guard update.
- Local Podman ESP-IDF build passed with `meshcore_deskos_d1l.bin` size `0xa5440`; smallest app partition free space remained `0x5abc0` bytes / 35%.
- Short active hardware soak on `COM7` passed and wrote `artifacts\soak\d1l-soak-active-short-local-COM7.json`.
  - 6 samples, 3 active Public `test` TX events, 0 command failures, and 0 threshold failures.
  - `mesh_tx_packet_delta=3`, `mesh_rx_packet_delta=8`, and `packet_total_written_delta=11`.
  - `board_ready_all=true`, `ui_ready_all=true`, `mesh_ready_all=true`, and `uptime_monotonic=true`.
  - `heap_free_delta=0`, `psram_free_delta=0`, stack floors were `current=1120` and `ui=1352` words, `lvgl_used_pct_peak=59`, and `signal_sample_count_peak=18`.
- Crash/reset guard update:
  - `python -m pytest tests\test_soak_d1l.py -q` passed with 5 tests.
  - `python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start` passed.
  - The runner now clears `crashlog` at the start when requested, fails on crash-like reset entries, and records recovered serial-command retries instead of silently hiding them.
- First 1-hour active acceptance attempt: `artifacts\soak\d1l-soak-active-1h-crashguard-local-COM7-20260629T192241.json`.
  - Result was intentionally **not** accepted: 30 active Public `test` TX events, `mesh_tx_packet_delta=30`, `mesh_rx_packet_delta=43`, `packet_total_written_delta=73`, monotonic uptime, and empty crashlog, but one `health` command timed out at sample 59.
  - Post-attempt smoke passed in `artifacts\smoke\d1l-smoke-post-failed-1h-soak-local-COM7.json`, confirming the device recovered and remained responsive.
- Retry-aware 1-hour active acceptance soak passed: `artifacts\soak\d1l-soak-active-1h-crashguard-retry-local-COM7-20260629T202558.json`.
  - 62 samples, 30 active Public `test` TX events, 0 command failures, 0 threshold failures, 0 retries needed, and monotonic uptime.
  - `mesh_tx_packet_delta=30`, `mesh_rx_packet_delta=37`, and `packet_total_written_delta=67`.
  - `board_ready_all=true`, `ui_ready_all=true`, `mesh_ready_all=true`.
  - `crashlog_total_written_delta=0`, `crashlog_count_peak=0`, and `crashlog_crash_like_count=0` after start clear.
  - `heap_free_delta=0`, `psram_free_delta=0`, stack floors were `current=1000` and `ui=1204` words, `lvgl_used_pct_peak=80`, and `signal_sample_count_peak=25`.
  - Post-soak hardware smoke also passed in `artifacts\smoke\d1l-smoke-post-active-1h-crashguard-retry-local-COM7.json`.

## Still Pending

- Full 12-hour idle/listening soak.
- Manual physical display/touch review remains separate from soak telemetry.
