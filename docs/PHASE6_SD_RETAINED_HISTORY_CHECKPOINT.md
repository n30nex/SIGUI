# Phase 6 SD Retained History Checkpoint

Date: 2026-06-30

## Scope

- Extended the retained blob-store abstraction beyond packet history.
- Public message history, DM message history, route history, and packet history can use the RP2040 SD file protocol when `storage status` reports a ready card, file operations, matching line/path/chunk limits, and atomic rename.
- NVS remains mirrored as fallback for every SD-capable retained history store.
- Settings, identity, contacts, read-state, crashlog, exports, and map tiles remain onboard/fallback-backed or pending.
- No boot-time or incidental formatting is introduced.

## SD Paths

- Public history: `stores/messages/public/public.tmp` -> `stores/messages/public/public.bin`
- DM history: `stores/messages/dm/threads.tmp` -> `stores/messages/dm/threads.bin`
- Route history: `stores/routes/routes.tmp` -> `stores/routes/routes.bin`
- Packet history: `stores/packet_log/ring.tmp` -> `stores/packet_log/ring.bin`

Each commit uses `rename replace=1`.

## Validation Rules

- Firmware builds run in GitHub Actions only.
- Hardware validation uses COM12 only.
- Do not use COM11 or COM29 for this SD slice.
- Do not send Public RF for SD storage validation.
- `storage filecanary` and SD-aware soak validation must report `formats_sd=false`.
- Before the RP2040 bridge is flashed, `storage status` may remain `protocol_pending` and all retained stores must remain NVS.
- After the RP2040 bridge is flashed and a ready card is present, `storage status` should report `message_store_backend="sd"`, `dm_store_backend="sd"`, `route_store_backend="sd"`, `packet_log_backend="sd"`, `data_backend="mixed"`, and `setup_action="retained_history_sd_enabled"`.

## Remaining SD Work

- Safely flash and validate the RP2040 SD bridge artifact on hardware.
- Prove retained Public/DM message, route, and packet history survives reboot/card remount on real SD.
- Add diagnostic export storage.
- Add map-tile cache storage.
