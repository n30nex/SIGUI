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
- The RP2040 UF2 volume later mounted as `RPI-RP2` at `G:\`, and Actions artifact `28494746866` was copied with the guarded helper. Copy evidence is `artifacts/rp2040-flash/rp2040-sd-bridge-uf2-copy-28494746866.json`; its UF2 SHA-256 was `05A2D728EC5EC59875C67EC2EC926B31F6032DAB1CBCD88DCA19AF54E655CC94`.
- Post-copy COM12 proof in `artifacts/hardware/com12/rp2040_sd_preflight_after_uf2_copy_28494746866.json` failed safely with `state="rp2040_protocol_pending"`: ESP32 UART2 to RP2040 was ready, but `storage status` and `storage diag` timed out. No Public RF or formatting occurred.
- The likely bridge-side cause was fixed by commit `e05264098a106f1ba0adcf766a1262d12e73448c`: the RP2040 status path now runs raw SdFat probes before any FAT mount attempt and can answer the same line protocol on USB serial for debugging.
- GitHub Actions run `28495545520` passed for that commit. The current verified RP2040 UF2 SHA-256 is `AFB6B12EE3518C48811F6C2876717B9BFAF43C1ABFE02E9BF693D95F977E16E5`.
- The fixed UF2 has not been copied yet. Current evidence `artifacts/hardware/com12/rp2040_sd_preflight_mount_blocked_28495545520.json` reports `state="rp2040_protocol_pending"`, `next_action="put_rp2040_in_uf2_bootloader"`, `artifact_ready=true`, `uf2_volume_available=false`, `rp2040_uart_ready=true`, `rp2040_protocol_supported=false`, `rp2040_diag_supported=false`, and `storage_file_gate_ready=false`.
- Host-side mount attempts found `COM16` as the RP2040 CDC device but no UF2 volume. Bounded 1200-baud touch attempts did not mount the volume; `COM16` then returned `Access is denied` or hung on open, and non-elevated USB device reset failed. The next hardware step is physical RP2040 BOOTSEL/replug/power-cycle to expose the UF2 mass-storage volume, then copy the verified `28495545520` UF2 and rerun COM12 preflight. Only use the guarded format command if status reports a present setup-required card with `format_supported=true`.

## Remaining SD Work

- Put the D1L RP2040 back into UF2/BOOTSEL mass-storage mode, copy the verified GitHub Actions run `28495545520` UF2 with `scripts/flash_rp2040_sd_bridge_uf2.py`, and rerun COM12 preflight.
- With the inserted D1L microSD card, use `storage diag` after the RP2040 flash to prove which power/SPI probe sees the card; only validate the guarded format path if setup is required and the bridge reports `format_supported=true`.
- Complete SD card acceptance so `storage status` reports the ready file-operation gates and `storage filecanary` proves temp write, read, rename replace, stat, final read, and cleanup against a real card.
- Complete hardware proof for retained Public/DM history, route history, packet history, diagnostic exports, sampled data exports, and map-tile cache once a card is electrically detected.
- Implement the full map page/tile download policy on top of the SD cache path.
- Keep settings, identity, and minimum boot-critical state on onboard storage.
