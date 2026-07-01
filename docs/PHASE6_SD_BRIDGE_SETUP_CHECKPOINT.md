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
- Added serial-only diagnostic export, sampled data export, and map-tile cache canaries. Diagnostic exports commit under `exports/diagnostics/`, sampled data exports commit under `exports/data/`, and the map-tile cache canary commits a synthetic tile under `map/tiles/`. They use temp write/read plus `rename replace=1`, leave the final artifact present for inspection, and do not send Public RF or format.
- Kept settings, identity, contacts, read-state, crashlog, and the full map page/tile download policy on onboard/fallback storage or pending; no card-dependent boot state is claimed in this slice.
- Matched the Seeed D1L ESP32/RP2040 UART contract at ESP32 UART2 GPIO19/GPIO20 and 921600 baud, enabled the RP2040 SD/sensor rail on GPIO18, added a raw SdFat card probe plus SPI1-aware formatter, and kept the ESP32 runtime SD probe window long enough for slow card status replies while boot probing stays short.
- Follow-up pending hardware proof: the RP2040 bridge now has `DESKOS_SD_DIAG`, and ESP32 exposes `storage diag` plus additive `storage status` probe fields. This is intended to diagnose physically inserted cards that still report `no_card` by comparing high/dedicated, high/shared, low/dedicated, and low/shared non-formatting raw probes.

## Validation Rules

- Do not run firmware builds on the Windows host. Firmware artifacts must come from GitHub Actions.
- Local verification for this slice is limited to host tests, simulator generation, dry-run smoke, diff checks, and GitHub Actions status.
- Do not test RF on Public channel for this slice. Current D1L hardware validation uses COM12 serial only; do not use reserved bot/OpenClaw serial ports during this SD bridge slice.
- After the RP2040 bridge is flashed, run `storage diag` and preflight first, then use `storage filecanary` or `python .\scripts\sd_file_canary_d1l.py --port COM12` for the SD file-operation proof, then `python .\scripts\sd_map_tile_canary_d1l.py --port COM12 --token map1` for the map-tile cache proof once the ready file gate is available. These canaries are serial-only and do not send Public RF or issue `DESKOS_SD_FORMAT`.

## Hardware Evidence

- GitHub Actions run `28490556715` for commit `1afd4043ce2473dddeaa66f23fafb667d2a98b88` passed host checks, ESP32 firmware build, and RP2040 SD bridge build. Its checksum manifests were verified locally before hardware use.
- The D1L ESP32 was flashed on COM12 only from that Actions release package. COM12 smoke passed after flashing.
- The Actions-built RP2040 UF2 artifact is verified and ready, with SHA-256 `05A2D728EC5EC59875C67EC2EC926B31F6032DAB1CBCD88DCA19AF54E655CC94`, but it has not been copied because Windows did not expose an RP2040 UF2/BOOTSEL mass-storage volume.
- `artifacts/hardware/com12/rp2040_sd_preflight_continue_20260701.json` is the current non-destructive hardware evidence. It reports `state="sd_card_not_present_diag_pending"`, `next_action="put_rp2040_in_uf2_bootloader"`, `artifact_ready=true`, `uf2_volume_available=false`, `rp2040_uart_ready=true`, `rp2040_protocol_supported=true`, `rp2040_diag_supported=false`, `storage_file_gate_ready=false`, and `sd_state="no_card"`.
- The same preflight proves no Public RF, no formatting, and no UF2 copy occurred. `storage diag` timed out with `diag_supported=false`, which is expected until the RP2040 bridge UF2 is flashed.
- Result: ESP32-side firmware and host acceptance gates are current, but physical SD acceptance cannot be completed until the RP2040 is put into UF2/BOOTSEL mode and the verified bridge UF2 is copied. After that flash, rerun COM12 preflight and only use the guarded format command if status reports `setup_required` with `format_supported=true`.

## Remaining SD Work

- Put the D1L RP2040 into UF2/BOOTSEL mass-storage mode, copy the verified GitHub Actions UF2 with `scripts/flash_rp2040_sd_bridge_uf2.py`, and rerun COM12 preflight.
- With the inserted D1L microSD card, use `storage diag` after the RP2040 flash to prove which power/SPI probe sees the card; only validate the guarded format path if setup is required and the bridge reports `format_supported=true`.
- Complete SD card acceptance so `storage status` reports the ready file-operation gates and `storage filecanary` proves temp write, read, rename replace, stat, final read, and cleanup against a real card.
- Complete hardware proof for retained Public/DM history, route history, packet history, diagnostic exports, sampled data exports, and map-tile cache once a card is electrically detected.
- Implement the full map page/tile download policy on top of the SD cache path.
- Keep settings, identity, and minimum boot-critical state on onboard storage.
