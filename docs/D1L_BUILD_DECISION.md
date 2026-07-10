# D1L Build Framework Decision

Date: 2026-06-29
Revised: 2026-07-10 for issue #63

Decision: select ESP-IDF v5.5.4 as the D1L migration target. It becomes the qualified production baseline only after every issue #63 acceptance stage below passes. GitHub Actions must use the version-pinned `espressif/idf:v5.5.4` image; `latest` and moving `release-vX.Y` tags are not release-acceptable.

## Support Decision

The original ESP-IDF v5.1.x baseline is end of life. Espressif's [v5.1.7 release notice](https://github.com/espressif/esp-idf/releases/tag/v5.1.7) says v5.1 reached end of life in December 2025 and no longer receives feature, bug, or security fixes, and recommends moving to a newer release such as v5.5.x. Espressif publishes [v5.5.4 as a bug-fix release](https://github.com/espressif/esp-idf/releases/tag/v5.5.4).

Espressif's [official Docker-image documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-guides/tools/idf-docker-image.html) distinguishes version tags such as `v5.5.4` from moving release-branch tags such as `release-v5.5`. The official registry publishes `espressif/idf:v5.5.4`, so issue #63 uses that exact version tag to avoid intentional release-branch drift. A tag is not a content-immutable image identity and does not, by itself, make the build reproducible; retain the Actions run metadata and resolved image identity when available.

## Rationale

The Seeed SenseCAP Indicator ESP32 SDK is ESP-IDF based and explicitly says to use ESP-IDF `v5.1.x`. Its examples select `CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L=y` and include D1L paths for RGB LCD, touch, IO expander, RP2040 reset, and SX1262 LoRa bring-up. The repository tracks that BSP as the `third_party/sensecap_indicator_esp32` git submodule at commit `77edb8d2b9a92fc67965c1b2d4a838f0d09a1800` (`v1.1.0`). It remains a pinned compatibility and board-support reference while its v5.5.4 behavior is qualified; it is not an untracked dependency and does not determine the production ESP-IDF version.

ESP-IDF also has official `esp_lcd` RGB panel support, I2C master APIs, SPI master APIs, NVS, timers, GPIO/LEDC, and LVGL integration patterns suitable for a 480x480 ESP32-S3 panel.

SigurdOS remains the primary application architecture reference, but its current firmware path is PlatformIO/Arduino for LilyGO T-Deck with ST7789 display, keyboard/trackball assumptions, and T-Deck SX1262 pins/TCXO settings. That makes it the wrong HAL base for Seeed D1L bring-up.

## PlatformIO Assessment

PlatformIO remains useful as a future wrapper if it can call the ESP-IDF project cleanly, but it is not the initial hardware bring-up framework. The D1L hardware stack is closer to Seeed's ESP-IDF SDK than to SigurdOS's Arduino/LovyanGFX T-Deck stack.

## Issue #63 Staged Acceptance

The SDK migration is release-blocking and is accepted only in these stages:

1. Selected target and compatibility: inventory the pinned Seeed BSP/API/Kconfig differences, pin Actions to `espressif/idf:v5.5.4`, and migrate source/configuration without weakening TLS, certificate-date, Wi-Fi, radio, SD, or no-format safeguards.
2. Generated dependency state: let ESP-IDF Component Manager generate `dependencies.lock` in the version-pinned Actions environment; do not hand-edit its generated hash. Archive and review that exact output, commit it, then require the qualifying Actions run to leave `dependencies.lock` unchanged. Unexpected generated configuration drift must also be reviewed rather than silently accepted.
3. Build evidence: pass the complete host suite and the version-pinned Actions firmware/package build, verify package checksums, and retain the run, commit, image, lock, and artifact metadata together.
4. Exact hardware qualification: flash only the matching Actions artifact to exact COM12. The serial `version` response must report `"idf":"v5.5.4"`; then repeat board, display/touch, Wi-Fi, RF, RP2040/SD, Map, health, reboot, and post-power-cycle smoke. COM16 remains the RP2040 side only when that proof explicitly requires it.
5. Release evidence: repeat the relevant commit-matched release-gate evidence and keep `supported_sdk_baseline` plus every other P0 green before cutting a tag.

The workflow pin and fail-closed audit checks for the selected tag plus committed lock target establish only configuration policy; they do not prove how the lock was generated, qualify the SDK, or close issue #63. Actions-generated lock provenance, a clean repeat build/package run, exact COM12 `version.idf` and behavior proof, and refreshed release evidence remain mandatory.

If a vendor incompatibility blocks migration, document the exact failing API/Kconfig/source path and obtain explicit approval for a maintained security-patch plan. The release gate must not silently waive the SDK risk.

## Initial Hardware Acceptance

The decision is considered provisional until real hardware validation confirms:

- RGB display color bars and LVGL screen.
- Touch coordinates from the capacitive panel.
- I2C expander/touch scan.
- SX1262 status read without DIO3 TCXO control.
- Repeatable clean build from checkout.

Without a connected D1L, this repo can currently validate only the source contracts, scripts, and host tests.

## Guardrails

- Firmware builds are GitHub-Actions-only; do not build the firmware locally on the workstation.
- `scripts/release_gate_audit_d1l.py` must fail closed unless the firmware job uses the exact selected SDK image and the committed component lock targets v5.5.4. Its `supported_sdk_baseline` check does not prove lock provenance or qualification; issue #63 remains open until the generated-lock, clean-build, hardware, and release-evidence stages pass.
- Migration changes must not weaken TLS trust, certificate-date validation, Wi-Fi credential handling, radio safety, SD bounds, or the no-device-format policy.
- Default radio profile is Canada/USA MeshCore: 910.525 MHz, BW 62.5 kHz, SF7, CR5, TX power 20 dBm.
- D1L SX1262 TCXO default is `NONE`. Seeed sample code detects a `VER` pin and may use 2.4V TCXO control for some boards; MeshCore DeskOS D1L must not enable that unless a board revision is explicitly proven to require it.
- Scripts must require `D1L_PORT` or an explicit `--port`/`-Port`.
