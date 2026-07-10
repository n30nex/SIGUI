# D1L Build Framework Decision

Date: 2026-06-29

Decision: use ESP-IDF v5.1.x as the primary D1L firmware framework.

Status review (2026-07-10): this remains the vendor-compatible bring-up baseline, not the final production baseline. [Espressif's final v5.1.7 notice](https://github.com/espressif/esp-idf/releases/tag/v5.1.7) states that the v5.1 branch reached end of life in December 2025 and no longer receives bug or security fixes. Public release therefore requires qualification on a supported ESP-IDF branch (target v5.5.x unless the Seeed BSP dictates another supported branch), followed by the full Actions and hardware gate matrix. If vendor compatibility prevents migration, a maintained security-patch plan must be documented and approved explicitly.

## Rationale

The Seeed SenseCAP Indicator ESP32 SDK is ESP-IDF based and explicitly says to use ESP-IDF `v5.1.x`. Its examples already select `CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L=y` and include D1L paths for RGB LCD, touch, IO expander, RP2040 reset, and SX1262 LoRa bring-up.

ESP-IDF also has official `esp_lcd` RGB panel support, I2C master APIs, SPI master APIs, NVS, timers, GPIO/LEDC, and LVGL integration patterns suitable for a 480x480 ESP32-S3 panel.

SigurdOS remains the primary application architecture reference, but its current firmware path is PlatformIO/Arduino for LilyGO T-Deck with ST7789 display, keyboard/trackball assumptions, and T-Deck SX1262 pins/TCXO settings. That makes it the wrong HAL base for Seeed D1L bring-up.

## PlatformIO Assessment

PlatformIO remains useful as a future wrapper if it can call the ESP-IDF project cleanly, but it is not the initial hardware bring-up framework. The D1L hardware stack is closer to Seeed's ESP-IDF SDK than to SigurdOS's Arduino/LovyanGFX T-Deck stack.

## Phase 1 Acceptance

The decision is considered provisional until real hardware validation confirms:

- RGB display color bars and LVGL screen.
- Touch coordinates from the capacitive panel.
- I2C expander/touch scan.
- SX1262 status read without DIO3 TCXO control.
- Repeatable clean build from checkout.

Without a connected D1L, this repo can currently validate only the source contracts, scripts, and host tests.

## Guardrails

- Default radio profile is Canada/USA MeshCore: 910.525 MHz, BW 62.5 kHz, SF7, CR5, TX power 20 dBm.
- D1L SX1262 TCXO default is `NONE`. Seeed sample code detects a `VER` pin and may use 2.4V TCXO control for some boards; MeshCore DeskOS D1L must not enable that unless a board revision is explicitly proven to require it.
- Scripts must require `D1L_PORT` or an explicit `--port`/`-Port`.
