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
- Added serial-only diagnostic export and map-tile cache canaries. Diagnostic exports commit under `exports/diagnostics/`; map-tile cache canary commits a synthetic tile under `map/tiles/`. Both use temp write/read plus `rename replace=1`, leave the final artifact present for inspection, and do not send Public RF or format.
- Kept settings, identity, contacts, read-state, crashlog, general non-diagnostic exports, and the full map page/tile download policy on onboard/fallback storage or pending; no card-dependent boot state is claimed in this slice.
- Matched the Seeed D1L ESP32/RP2040 UART contract at ESP32 UART2 GPIO19/GPIO20 and 921600 baud, enabled the RP2040 SD/sensor rail on GPIO18, added a raw SdFat card probe plus SPI1-aware formatter, and kept the ESP32 runtime SD probe window long enough for slow card status replies while boot probing stays short.

## Validation Rules

- Do not run firmware builds on the Windows host. Firmware artifacts must come from GitHub Actions.
- Local verification for this slice is limited to host tests, simulator generation, dry-run smoke, diff checks, and GitHub Actions status.
- Do not test RF on Public channel for this slice. Current D1L hardware validation uses COM12 serial only; do not use reserved bot/OpenClaw serial ports during this SD bridge slice.
- After the RP2040 bridge is flashed, use `storage filecanary` or `python .\scripts\sd_file_canary_d1l.py --port COM12` for the SD file-operation proof, then `python .\scripts\sd_map_tile_canary_d1l.py --port COM12 --token map1` for the map-tile cache proof once the ready file gate is available. These canaries are serial-only and do not send Public RF or issue `DESKOS_SD_FORMAT`.

## Hardware Evidence

- GitHub Actions run `28478756887` for commit `d26f345dd02e5debfb33e42db0111a7fa663efb4` passed host checks, ESP32 firmware build, and RP2040 SD bridge build. Its checksum manifests were verified locally before flashing.
- The D1L ESP32 on COM12 and RP2040 bridge on COM16 were flashed only from that Actions run. RP2040 UF2 SHA-256 was `E81196310680F4CA282574E6EBB9608542575C44EEDB32BAD866AFF576AAB4A2`.
- `artifacts/hardware/com12/rp2040_sd_bridge_preflight_after_sd_bus_harden.json` proves the ESP32/RP2040 protocol is live: `rp2040_protocol_supported=true`, UART2 GPIO19/GPIO20 at 921600 baud, file protocol limits are advertised, and no Public RF, formatting, or UF2 copying occurred during preflight.
- The same preflight reports `sd.state=no_card`, `present=false`, `mounted=false`, and `format_supported=false`. `artifacts/hardware/com12/storage_setup_after_sd_bus_harden.json` confirms `storage setup confirm FORMAT-DESKOS-SD` was refused with `ESP_ERR_NOT_FOUND` and `format_performed=false`.
- Result: protocol/no-card fallback is hardware-validated, but SD acceptance cannot be completed until the RP2040 reports an electrically present card. The next hardware step is to reseat or replace the card, then rerun preflight and only use the guarded format command if status reports `setup_required` with `format_supported=true`.

## Remaining SD Work

- Reseat or replace the D1L microSD card until the RP2040 bridge reports `present=true`; then validate the guarded format path if setup is required.
- Complete SD card acceptance so `storage status` reports the ready file-operation gates and `storage filecanary` proves temp write, read, rename replace, stat, final read, and cleanup against a real card.
- Complete hardware proof for retained Public/DM history, route history, packet history, diagnostic exports, and map-tile cache once a card is electrically detected.
- Implement the full map page/tile download policy and general non-diagnostic exports on top of the SD cache path.
- Keep settings, identity, and minimum boot-critical state on onboard storage.
