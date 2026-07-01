# Attributions

This project builds on open hardware/software work from upstream firmware projects. Keep this file with public source archives, release notes, and binary firmware releases.

## Seeed SenseCAP Indicator

- Project: Seeed SenseCAP Indicator ESP32 SDK and examples
- Repository: https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32
- Use in this project: board support package, LCD/touch integration patterns, D1L/GX display configuration, and firmware build baseline.

## SigurdOS-TDeck

- Project: SigurdOS-TDeck
- Repository: https://github.com/hermes-gadget/SigurdOS-tdeck
- Source commit reviewed: `784b1fb26e8c4b733581ca1617f1a627778f3577`
- Upstream copyright headers reviewed: `Copyright (C) 2025 Ben`
- Upstream license headers reviewed: `GPL-3.0-or-later`
- Permission: the maintainers gave this project permission to use the work with attribution.
- Use in this project: reference architecture for a production touch-first MeshCore desk firmware, especially cached touch state, coordinate validation/clamping, input/display separation, and hardware test coverage ideas.
- Upstream files reviewed for this touch slice: `src/hal/tdeck_pins.h`, `src/hal/touch.h`, `src/hal/touch.cpp`, `src/hal/display.cpp`, `src/hal/tdeck_board.h`, `src/main.cpp`, and `test/test_touch/test_touch.cpp`.

SigurdOS-TDeck targets the LilyGo T-Deck GT911 touch controller, while this firmware targets the Seeed SenseCAP Indicator D1L FT5x06/GX panel. D1L touch controller code therefore stays on the Seeed BSP/FT5x06 path, with SigurdOS-TDeck used as an attributed reference for input architecture and validation behavior rather than blindly copying the GT911 driver.
