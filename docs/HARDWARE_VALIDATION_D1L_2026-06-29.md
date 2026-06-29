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

- Post-change smoke after LCD direct-mode fix: `artifacts/smoke/d1l-smoke-public-rf-COM7-after-lcd-config.json`
  - 15 commands passed
  - `mesh status` reported `phase2_public_rf`, state `ready`, `radio_ready=true`
  - `health` reported `reset_reason=POWERON`, `board_ready=true`, `ui_ready=true`
- Controlled MeshCore Public RF test: `artifacts/smoke/d1l-public-test-rf-COM7.json`
  - D1L sent exact Public text `test`
  - Local Meshcorebot on `COM11` observed fresh counter movement: `rx_channel_total +4`, `relay_success_total +4`, `discord_send_success_total +4`
  - D1L packet ring logged TX note `test`
  - D1L packet ring decoded Public replies including `Krabs Node: Test OK CH0.`
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
- MeshCore advert TX/RX.
- The validated Public message path is currently a serial-console RF slice, not the final touch UI workflow.
- Flash backup was intentionally skipped per operator instruction.
