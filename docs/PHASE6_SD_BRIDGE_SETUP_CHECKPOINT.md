# Phase 6 SD Bridge Setup Checkpoint

This checkpoint advances optional SD-card data storage without making boot or retained data depend on the card.

## Implemented

- Added an ESP32-side RP2040 SD status probe using the line protocol `DESKOS_SD_STATUS`.
- Superseded 2026-07-02: the guarded ESP32/RP2040 SD format path has been removed. Users must provide FAT32 cards prepared on a computer.
- Extended `storage status` with RP2040 protocol, card, filesystem, DeskOS root, capacity/free, setup, and `needs_fat32` fields.
- Added non-destructive `storage setup`.
- Kept `storage setup` non-destructive and policy-only.
- Added a Settings-tab Storage Setup sheet and simulator coverage.
- Added `tools/rp2040_sd_protocol.py` as the host reference simulator for the RP2040 line protocol.
- Added `firmware/rp2040_sd_bridge/deskos_sd_bridge` as an original Arduino RP2040 SD bridge target for the D1L internal UART and SD pins.
- Added a GitHub Actions-only RP2040 bridge build job that compiles with Arduino CLI and uploads checksummed `rp2040-sd-bridge-firmware` artifacts.
- Added bounded generic `DESKOS_SD_FILE v=1` file operations to the simulator, RP2040 bridge target, and ESP32 bridge API: `stat`, `read`, `write`, `append`, `delete`, and `rename` with sanitized relative paths, base64url payloads, CRC32 checks, 512-byte lines, and 192-byte decoded chunks.
- Added an SD-capable retained blob-store provider for the packet-log canary. It enables SD only when the RP2040 reports ready data, file operations, matching line/path/chunk limits, and atomic rename; writes use `stores/packet_log/ring.tmp` plus `rename replace=1` to commit `stores/packet_log/ring.bin`.
- Kept an onboard NVS mirror for the packet-log canary and retained NVS fallback on no-card, timeout, missing file support, missing atomic rename, insufficient limits, and invalid SD blob cases.
- Added serial-only diagnostic export, sampled data export, and map-tile cache canaries. Diagnostic exports commit under `exports/diagnostics/`, sampled data exports commit under `exports/data/`, and the map-tile cache canary commits a synthetic tile under `map/tiles/`. They use temp write/read plus `rename replace=1`, leave the final artifact present for inspection, and do not send Public RF or format.
- Kept settings, identity, contacts, read-state, crashlog, and the full map page/tile download policy on onboard/fallback storage or pending; no card-dependent boot state is claimed in this slice.
- Matched the Seeed D1L ESP32/RP2040 UART contract at ESP32 UART2 GPIO19/GPIO20 and 921600 baud, enabled the RP2040 SD/sensor rail on GPIO18, added safe/cached `DESKOS_SD_STATUS`, explicit `DESKOS_SD_MOUNT`, and raw SdFat diagnostics.
- Follow-up pending hardware proof: the RP2040 bridge now has `DESKOS_SD_PING`, safe `DESKOS_SD_STATUS`, explicit async `DESKOS_SD_MOUNT`, async-safe `DESKOS_SD_DIAG`, and a bounded raw SPI presence probe before Arduino/SdFat mount attempts; ESP32 exposes `rp2040 ping`, `storage status`, `storage mount`, and `storage diag`. This is intended to keep boot/UI polling non-blocking while giving the operator a deliberate mount path for physically inserted cards.

## Validation Rules

- Do not run firmware builds on the Windows host. Firmware artifacts must come from GitHub Actions.
- Local verification for this slice is limited to host tests, simulator generation, dry-run smoke, diff checks, and GitHub Actions status.
- Do not test RF on Public channel for this slice. Current D1L hardware validation uses COM12 serial only; do not use reserved bot/OpenClaw serial ports during this SD bridge slice.
- After the RP2040 bridge is flashed, run preflight first so it captures `rp2040 ping`, safe `storage status`, explicit `storage mount`, bounded safe-status polling while `state="mount_pending"`, optional `storage diag`, and `health`. Use `storage filecanary` or `python .\scripts\sd_file_canary_d1l.py --port COM12` only once the ready file gate is available, then `python .\scripts\sd_map_tile_canary_d1l.py --port COM12 --token map1` for the map-tile cache proof. These canaries are serial-only and do not send Public RF or format SD.

## Hardware Evidence

- GitHub Actions run `28490556715` for commit `1afd4043ce2473dddeaa66f23fafb667d2a98b88` passed host checks, ESP32 firmware build, and RP2040 SD bridge build. Its checksum manifests were verified locally before hardware use.
- The D1L ESP32 was flashed on COM12 only from that Actions release package. COM12 smoke passed after flashing.
- The RP2040 UF2 volume later mounted as `RPI-RP2` at `G:\`, and Actions artifact `28494746866` was copied with the guarded helper. Copy evidence is `artifacts/rp2040-flash/rp2040-sd-bridge-uf2-copy-28494746866.json`; its UF2 SHA-256 was `05A2D728EC5EC59875C67EC2EC926B31F6032DAB1CBCD88DCA19AF54E655CC94`.
- Post-copy COM12 proof in `artifacts/hardware/com12/rp2040_sd_preflight_after_uf2_copy_28494746866.json` failed safely with `state="rp2040_protocol_pending"`: ESP32 UART2 to RP2040 was ready, but `storage status` and `storage diag` timed out. No Public RF or formatting occurred.
- The likely bridge-side cause was fixed by commit `e05264098a106f1ba0adcf766a1262d12e73448c`: the RP2040 status path now runs raw SdFat probes before any FAT mount attempt and can answer the same line protocol on USB serial for debugging.
- GitHub Actions run `28495545520` passed for that commit. The current verified RP2040 UF2 SHA-256 is `AFB6B12EE3518C48811F6C2876717B9BFAF43C1ABFE02E9BF693D95F977E16E5`.
- Actions run `28499319258` for commit `a8268073ae290567e26f27491c8ffa167f6f8d57` flashed the ESP32 on COM12 and copied the RP2040 UF2 with SHA-256 `AA71CD32C9433F1D57B1C3F243ABB2A7535728E6A3905C6A424A1E77D5F3E57E`.
- COM12 evidence `artifacts/hardware/com12/rp2040_sd_preflight_ping_status_28499319258.json` proves `rp2040 ping` works with `protocol_supported=true`, `sd_touched=false`, file limits, and `atomic_rename=true`; the same run still timed out on SD-touching `storage status` and `storage diag`.
- Actions run `28500532119` for commit `cfb99620ee691097d845fb0e1920c06bbd02d1a5` passed host checks, ESP32 firmware build, and RP2040 SD bridge build. The release package and RP2040 checksum manifests verified locally, then the ESP32 was flashed on COM12 and the RP2040 UF2 with SHA-256 `44F2D152818C3EDDEA60165B268234742D2EAD1281F345F7B059ADBB472E3B9C` was copied through the guarded helper.
- COM12 evidence `artifacts/hardware/com12/rp2040_sd_preflight_mount_split_28500532119.json` proves `rp2040 ping` works, initial safe `storage status` returns `state="mount_required"` with NVS fallback, and no Public RF or formatting occurred. The same run showed explicit `storage mount` still timed out and made later SD-touching commands unresponsive, so the follow-up implementation moves mount/diag work to the RP2040 core1 worker and reports `state="mount_pending"` while probing.
- Actions run `28501529179` for commit `0697d61cac2fcf7bb67a54312ec591e91b0ce363` passed host checks, ESP32 firmware build, and RP2040 SD bridge build. COM12 evidence `artifacts/hardware/com12/rp2040_sd_preflight_async_mount_28501529179.json` proves `storage mount` now returns `state="mount_pending"` without formatting or Public RF, but the later Arduino/SdFat mount path still stalled RP2040 status replies; the current follow-up adds a bounded raw SPI presence probe before any filesystem mount attempt.
- Actions run `28502095977` for commit `552a59df3a5810c5004f496584020d31f5f1dea4` passed host checks, ESP32 firmware build, and RP2040 SD bridge build. COM12 evidence proved the bounded raw probe kept the bridge responsive and detected the inserted card without Public RF or formatting; the file-operation gate was still not ready.
- Actions run `28502609315` for commit `b8d374246310ac761cbd965ae59a0bd0e1b6883f` is retained as historical evidence only. Its guarded-format path has been superseded by the FAT32-only no-format policy.

## Remaining SD Work

- Current follow-up: the direct high/dedicated SdFat mount did not produce a ready FAT32 filesystem even though the user-confirmed FAT32 card was inserted and the bounded raw SPI probe detected the card (`probe_error=0`, `probe_data=192`). The bridge now tries filesystem mount on every raw-present high/low, dedicated/shared candidate before declaring the card unmountable, the Actions RP2040 build enables SdFat SPI command CRCs with `USE_SD_CRC=1`, and each explicit SdFat mount/probe force-cycles the selected SD rail before card init because the latest COM12/COM16 proof moved from `mount_error=23` / `mount_data=8` to `mount_error=1` / `mount_data=0` after CRC was enabled. Use `mount_error`/`mount_data` as firmware-side diagnostics; do not treat reformatting the card as the fix.
- Keep using COM12 preflight after RP2040/ESP32 changes; only run SD file/export/map canaries once `storage status` reports `state="ready"`, `file_ops=true`, and `atomic_rename=true`.
- Complete SD card acceptance so `storage status` reports the ready file-operation gates and `storage filecanary` proves temp write, read, rename replace, stat, final read, and cleanup against a real card.
- Complete hardware proof for retained Public/DM history, route history, packet history, diagnostic exports, sampled data exports, and map-tile cache once a card is electrically detected.
- Implement the full map page/tile download policy on top of the SD cache path.
- Keep settings, identity, and minimum boot-critical state on onboard storage.
