# D1L Hardware Validation - 2026-06-29

## Device

- Port: `COM7`
- USB identity: `USB-SERIAL CH340`
- ESP32-S3 MAC: `d8:3b:da:75:72:6c`
- Chip: ESP32-S3 QFN56 rev v0.2, 8MB embedded PSRAM

Do not use `COM11` or `COM29` for this D1L target.

## Firmware And CI

- Branch: `feature/meshcore-deskos-d1l`
- Firmware commit flashed for baseline smoke: `f99defe`
- GitHub Actions run: `28358816656`
- Run URL: `https://github.com/n30nex/SIGUI/actions/runs/28358816656`
- Firmware artifact: `artifacts/github/28358816656/d1l-firmware-artifacts/build/meshcore_deskos_d1l.bin`
- SHA256 manifest: `artifacts/github/28358816656/d1l-firmware-artifacts/SHA256SUMS.txt`

## Passing Hardware Evidence

- Boot/idle log: `artifacts/logs/d1l-idle-github-28358816656-COM7-20260629T083219Z.log`
  - App version `f99defe`
  - Board initialized
  - No panic, watchdog, or repeated reset observed during the capture window
- Hardware smoke: `artifacts/smoke/d1l-smoke-github-28358816656-COM7.json`
  - 15 commands passed
  - Board and UI ready
  - I2C addresses `0x20,0x48`
  - SX1262 present
  - RP2040 UART ready
- Settings persistence smoke: `artifacts/smoke/d1l-smoke-persistence-COM7.json`
  - Set temporary node name `D1L Smoke Persist`
  - Set `path_hash_bytes=3`
  - Rebooted and verified both values survived NVS
  - Reset settings and rebooted back to defaults
- Backlight command smoke: `artifacts/smoke/d1l-backlight-COM7.json`
  - `backlight 20` returned OK
  - `backlight 70` returned OK and restored the normal bring-up brightness

## Still Pending

- Manual visual confirmation of display bars and touch target movement by a human looking at the device.
- MeshCore C++ local identity generation/storage.
- Applying saved radio settings to the live SX1262 runtime.
- MeshCore advert TX/RX and public message TX/RX with a second known-good MeshCore node.
- Flash backup was intentionally skipped per operator instruction.
