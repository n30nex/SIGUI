# Phase 4 Touch DM Compose Checkpoint

Date: 2026-06-29

## Completed

- Reused the LVGL compose sheet for both Public and direct-message compose flows.
- Added contact-row `DM` actions for contacts with retained public keys.
- Added DM compose state that copies the selected contact into UI-owned state before opening the keyboard.
- Routed DM keyboard send through `d1l_app_model_send_dm_text()`, so touch DM sends use the same MeshCore backend as the serial command.
- Updated contact rows to show whether the DM path is currently flood-only or has a learned direct path.

## Results

- Host tests: passed locally with 51 tests.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0xa0d60`, 37% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification and no flash backup, per operator instruction.
- Hardware smoke: passed; `artifacts/smoke/d1l-smoke-touch-dm-compose-local-COM7.json`.
- Touch-DM backend precondition/probe: passed; `artifacts/smoke/d1l-touch-dm-compose-backend-local-COM7.json` verified that `YKF Corebot` fingerprint `0BF0A701D5AE2DB6` is present as a promoted contact with a full public key, `mesh send dm` still queues, and `messages dm` persisted a new TX row for `touch dm compose backend` with ACK hash `2588389861`.
- Boot-loop check: no loop observed after this image; `health` showed uptime increasing from `28865` ms to `36358` ms during the focused probe, with `board_ready=true` and `ui_ready=true`.

## Remaining Gaps

- Manual physical touch review of the new DM button and keyboard flow is still pending.
- Full threaded DM detail view, controlled inbound DM proof, controlled ACK/PATH RF proof, and direct-route RF proof are still pending.
