# Phase 6 SD Bridge Setup Checkpoint

This checkpoint advances optional SD-card data storage without making boot or retained data depend on the card.

## Implemented

- Added an ESP32-side RP2040 SD status probe using the line protocol `DESKOS_SD_STATUS`.
- Added the guarded ESP32 command path for `DESKOS_SD_FORMAT FORMAT-DESKOS-SD`; it is sent only after the bridge has reported a present card, `format_supported=true`, setup is required, and the user typed the exact confirmation phrase.
- Extended `storage status` with RP2040 protocol, card, filesystem, DeskOS root, capacity/free, setup, and format action fields.
- Added non-destructive `storage setup`.
- Kept `storage setup confirm FORMAT-DESKOS-SD` non-destructive on the current D1L bridge, which still does not answer the SD protocol or advertise format support.
- Added a Settings-tab Storage Setup sheet and simulator coverage.
- Added `tools/rp2040_sd_protocol.py` as the host reference simulator for the RP2040 line protocol.
- Added `firmware/rp2040_sd_bridge/deskos_sd_bridge` as an original Arduino RP2040 SD bridge target for the D1L internal UART and SD pins.
- Added a GitHub Actions-only RP2040 bridge build job that compiles with Arduino CLI and uploads checksummed `rp2040-sd-bridge-firmware` artifacts.
- Added bounded generic `DESKOS_SD_FILE v=1` file operations to the simulator, RP2040 bridge target, and ESP32 bridge API: `stat`, `read`, `write`, `append`, `delete`, and `rename` with sanitized relative paths, base64url payloads, CRC32 checks, 512-byte lines, and 192-byte decoded chunks.
- Added an SD-capable retained blob-store provider for the packet-log canary. It enables SD only when the RP2040 reports ready data, file operations, matching line/path/chunk limits, and atomic rename; writes use `stores/packet_log/ring.tmp` plus `rename replace=1` to commit `stores/packet_log/ring.bin`.
- Kept an onboard NVS mirror for the packet-log canary and retained NVS fallback on no-card, timeout, missing file support, missing atomic rename, insufficient limits, and invalid SD blob cases.
- Kept messages, routes, settings, identity, contacts, read-state, diagnostics, exports, and map tiles on onboard/fallback storage; no broad SD-backed store migration is claimed in this slice.

## Validation Rules

- Do not run firmware builds on the Windows host. Firmware artifacts must come from GitHub Actions.
- Local verification for this slice is limited to host tests, simulator generation, dry-run smoke, diff checks, and GitHub Actions status.
- Do not test RF on Public channel for this slice. Current D1L hardware validation uses COM12 serial only; do not use reserved bot/OpenClaw serial ports during this SD bridge slice.

## Remaining SD Work

- Validate the RP2040 SD bridge artifact on hardware after a safe RP2040 flashing procedure is documented and the correct RP2040 programming path is identified.
- Safely flash and validate the RP2040 SD bridge artifact on hardware so the packet-log SD canary can be proven against a real card.
- Move larger retained stores to SD when configured: Public/DM history, route history, exports, and future map tiles.
- Keep settings, identity, and minimum boot-critical state on onboard storage.
