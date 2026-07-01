# Source Audit and Attribution

Date: 2026-06-29

This project uses references for architecture and feature parity, but Phase 1 source is newly written except for git submodules.

## Included Submodules

- Seeed SenseCAP Indicator ESP32 SDK: https://github.com/Seeed-Solution/sensecap_indicator_esp32
  - License: Apache-2.0.
  - Use: D1L ESP-IDF components for BSP, RGB LCD, touch, IO expander, and SX1262 wiring.
  - Risk: Seeed LoRa sample code can enable SX1262 DIO3 TCXO control when its board-detect pin suggests TCXO. MeshCore DeskOS D1L currently treats TCXO as `NONE` by default and avoids calling TCXO setup during Phase 1 radio hardware probe.

- MeshCore upstream: https://github.com/meshcore-dev/MeshCore
  - License: MIT-style `license.txt` in the upstream repo, with bundled third-party notices.
  - Use: Protocol/library base for Phase 2 MeshCore service integration.
  - Risk: Upstream is Arduino/PlatformIO oriented in many paths; ESP-IDF integration may need an adapter layer rather than direct reuse. Optional web stack dependencies must be reviewed before Wi-Fi management is added.

## Reference Repositories

- SigurdOS T-Deck: https://github.com/hermes-gadget/SigurdOS-tdeck
  - Source commit reviewed: `784b1fb26e8c4b733581ca1617f1a627778f3577` on `dev`.
  - License: GPL-3.0-or-later.
  - Upstream copyright headers reviewed: `Copyright (C) 2025 Ben`.
  - Permission: maintainers gave this project permission to use the work with attribution.
  - Files reviewed for the D1L touch/input slice: `src/hal/tdeck_pins.h`, `src/hal/tdeck_board.h`, `src/hal/touch.h`, `src/hal/touch.cpp`, `src/hal/display.cpp`, `src/main.cpp`, and `test/test_touch/test_touch.cpp`.
  - Use: primary production firmware reference for touch-first MeshCore UX and input architecture. The current D1L implementation adapts its cached-state, validation/clamping, deterministic-release, and test-coverage ideas onto the Seeed BSP/FT5x06 path.
  - Risk: copying/adapting GPL source would impose GPL obligations on distributed firmware/source unless separate written terms grant otherwise. The current D1L touch work keeps controller code on the Seeed FT5x06 BSP path instead of porting SigurdOS's GT911 driver.

- LimitlezzOS: https://github.com/ItsLimitlezz/LimitlezzOS
  - License: no top-level license found in the shallow audit. Treat as all-rights-reserved unless proven otherwise.
  - Use: UX ideas only: onboarding, dark mobile layout, virtualized lists, unread badges.
  - Risk: Do not copy source or assets without a license review.

- MeshCoreTerm: https://github.com/dabeani/meshcoreterm
  - License: MIT text in `license.txt`.
  - Use: Feature checklist for contacts, DMs, packet metadata, radio settings, telemetry, and route tools.
  - Risk: Mostly host/terminal UI concepts; firmware memory constraints require bounded stores.

- realtag meshcore-tdeck-plus-lvgl: https://github.com/realtag-github/meshcore-tdeck-plus-lvgl
  - License: no actual project license found in the shallow audit; `LICENSE_NOTES.md` recommends MIT or Apache-2.0 and warns against proprietary assets.
  - Use: Planning/scaffold reference only.
  - Risk: Experimental scaffold, not a production base.

- Seeed SenseCAP Indicator RP2040 examples: https://github.com/Seeed-Solution/SenseCAP_Indicator_RP2040
  - License: GPL-3.0.
  - Use: Hardware reference only for RP2040 SD SPI pins and ESP32/RP2040 UART pins.
  - Risk: Do not copy/vendor sketches wholesale unless the project intentionally accepts GPL obligations. The DeskOS RP2040 SD bridge is an original minimal implementation that uses the documented Arduino APIs and pin facts.

- Earle Philhower Arduino-Pico core: https://github.com/earlephilhower/arduino-pico
  - License: LGPL-2.1-or-later core/library components plus bundled third-party licenses.
  - Use: GitHub Actions build target for the Seeed Indicator RP2040 board package and Arduino SD/SDFS APIs.
  - Risk: Keep distributed binary/source notices accurate if RP2040 bridge binaries ship with releases.

## Attribution Policy

Keep third-party code in submodules or clearly attributed components. New MeshCore DeskOS files should use original implementations and avoid proprietary icons, Apple assets, untracked copied SigurdOS GPL code, and unlicensed LimitlezzOS source.

If this firmware later copies/adapts SigurdOS source code rather than architecture/behavior, the project should be treated as GPL-compatible and release obligations must be updated, including per-file SPDX/modification notices and public source availability for distributed binaries.
