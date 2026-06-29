# Phase 4 DM Thread Checkpoint

Date: 2026-06-29

## Completed

- Added a first bounded DM thread/detail sheet opened from recent DM preview rows.
- Made recent DM rows clickable and copied the selected fingerprint/alias into UI-owned state before rendering the sheet.
- Added a `Reply` action that looks up the persisted contact through `d1l_app_model_find_contact()` before reusing the existing DM compose sheet.
- Expanded the app snapshot DM preview from 3 to 5 rows so the thread sheet can show more recent context without unbounded UI work.

## Results

- Host tests: passed locally with 52 tests.
- `git diff --check`: passed.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0xa10d0`, 37% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification and no flash backup, per operator instruction.
- Hardware smoke after flash: passed; `artifacts/smoke/d1l-smoke-dm-thread-local-COM7.json`.
- NVS cleanup: the long validation session filled the 24 KB NVS partition, so only the NVS region at `0x9000`/`0x6000` was erased. Post-erase smoke passed in `artifacts/smoke/d1l-smoke-dm-thread-after-nvs-erase-local-COM7.json`.
- DM thread backend precondition: passed; `artifacts/smoke/d1l-dm-thread-ui-backend-local-COM7.json` shows `YKF Corebot` as a signed heard node and promoted contact with full public key, plus a persisted DM TX row for `thread ui local` with ACK hash `558288271`.
- Public RF regression after the DM-thread slice: passed; `artifacts/smoke/d1l-public-test-after-dm-thread-local-COM7.json` shows COM11 Meshcorebot counter deltas of `rx_channel_total +2`, `relay_success_total +2`, `discord_send_success_total +2`, `rx_log_total +4`, and `rx_duplicate_total +2`. D1L stayed up past 162 seconds with `board_ready=true` and `ui_ready=true`.

## Remaining Gaps

- Manual physical touch review of tapping a DM preview row, opening the thread sheet, and using `Reply` is still pending.
- Controlled inbound DM proof, controlled ACK/PATH RF proof, and direct-route RF proof are still pending.
