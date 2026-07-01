# Third Party Notices

Keep this notice with source archives and public firmware release artifacts.

## SigurdOS-TDeck

Portions of this project's architecture, touch-input behavior, and test strategy are informed by SigurdOS-TDeck.

- Copyright: Copyright (C) 2025 Ben
- License: GPL-3.0-or-later
- Source: https://github.com/hermes-gadget/SigurdOS-tdeck
- Source commit reviewed: `784b1fb26e8c4b733581ca1617f1a627778f3577`
- Permission: used with maintainer permission and attribution.

The reviewed upstream touch implementation targets LilyGo T-Deck GT911 hardware. MeshCore DeskOS D1L keeps the D1L touch path on the Seeed SenseCAP Indicator FT5x06/GX BSP driver and adapts the proven input architecture concepts to the D1L hardware.

## Seeed SenseCAP Indicator

- Source: https://github.com/Seeed-Solution/sensecap_indicator_esp32
- License: Apache-2.0
- Use: Seeed SenseCAP Indicator D1L BSP, display/touch/radio hardware support, and ESP-IDF build baseline.

## MeshCore

- Source: https://github.com/meshcore-dev/MeshCore
- License: MIT-style upstream license text
- Use: MeshCore protocol/library base and feature compatibility target.
