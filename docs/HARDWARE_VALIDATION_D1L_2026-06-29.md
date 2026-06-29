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
- Latest local hardware image: `build/meshcore_deskos_d1l.bin`
- Latest local build size after the Phase 3 UI shell slice: `0x9baf0`, 39% free in the app partition

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
- Identity, advert TX/RX, and Public RF regression: `artifacts/smoke/d1l-advert-public-rf-mesh-ts-COM7.json`
  - Ed25519 identity fingerprint `0E1EE649EF5371E0` survived reboot.
  - D1L packet ring logged a signed advert TX entry: `zero 0E1EE649`.
  - Retained MeshCore TX timestamp fixed repeated-boot duplicate filtering seen in the earlier CI artifact probe.
  - Local Meshcorebot on `COM11` observed fresh advert movement: `rx_advert_total +1`.
  - Public `test` regression still passed with `rx_channel_total +3`, `relay_success_total +3`, and `discord_send_success_total +3`.
- Fresh hardware smoke after identity/advert image: `artifacts/smoke/d1l-smoke-mesh-ts-COM7.json`
  - 15 commands passed
  - `identity status` reported `stored_nvs_ed25519`, `public_key_ready=true`, fingerprint `0E1EE649EF5371E0`
  - `mesh status` reported `identity_ready=true`, `radio_ready=true`, and `rx_adverts=0` after reboot
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
- Phase 3 UI shell local smoke: `artifacts/smoke/d1l-smoke-phase3-ui-shell-COM7.json`
  - 15 commands passed
  - `ui_ready=true`, `board_ready=true`, and `radiohw` reported the SX1262 path OK
- Phase 3 UI shell local RF regression: `artifacts/smoke/d1l-rf-phase3-ui-shell-COM7.json`
  - The Phase 3 shell build retained the controlled Public `test` RF path.
  - Local Meshcorebot observed fresh Public counter movement: `rx_channel_total +2`, `relay_success_total +2`, and `discord_send_success_total +2`.
  - D1L packet log decoded local bot replies including `Krabs Node: Test OK CH0.`

## Still Pending

- Manual visual confirmation of display bars and touch target movement by a human looking at the device.
- The validated touch Public workflow is currently a fixed `test` action, not the final free-text composer or DM workflow.
- Flash backup was intentionally skipped per operator instruction.
