# Third Party Notices

Keep this notice with source archives and public firmware release artifacts.

## SigurdOS-TDeck

Portions of this project's architecture, touch-input behavior, and test strategy are informed by SigurdOS-TDeck.

- Copyright: Copyright (C) 2025 Ben
- License: GPL-3.0-or-later
- Source: https://github.com/hermes-gadget/SigurdOS-tdeck
- Source commit reviewed: `e7e7b12ee771edd2b15e64e1a6569b7b18f84423`
- Upstream metadata reviewed: default branch `dev`, root `LICENSE`, README license statement, `LICENSES/DejaVu-Fonts.txt`, and MeshCore submodule pin `60ea4a91bf14363e837037a79ce1bff7fa37483f`
- Permission: used with maintainer permission and attribution.

MeshCore DeskOS D1L is informed by and may adapt concepts from SigurdOS T-Deck by hermes-gadget, GPL-3.0-or-later, reviewed at commit `e7e7b12ee771edd2b15e64e1a6569b7b18f84423`: https://github.com/hermes-gadget/SigurdOS-tdeck. SigurdOS T-Deck builds on MeshCore by meshcore-dev, MIT, reviewed as submodule commit `60ea4a91bf14363e837037a79ce1bff7fa37483f`: https://github.com/meshcore-dev/MeshCore.

The reviewed upstream implementation targets LilyGo T-Deck hardware. MeshCore DeskOS D1L keeps the D1L touch path on the Seeed SenseCAP Indicator FT5x06/GX BSP driver and uses SigurdOS-TDeck as an attributed reference for product architecture, navigation, storage/map concepts, packet visibility, and validation behavior rather than copying device-specific code.

## Seeed SenseCAP Indicator

- Source: https://github.com/Seeed-Solution/sensecap_indicator_esp32
- License: Apache-2.0
- Pinned submodule commit: `77edb8d2b9a92fc67965c1b2d4a838f0d09a1800` (`v1.1.0`)
- Use: tracked compatibility/BSP reference for Seeed SenseCAP Indicator D1L display, touch, radio, and board support. Its ESP-IDF v5.1.x example baseline does not determine the separately selected v5.5.4 migration target.

## MeshCore

- Source: https://github.com/meshcore-dev/MeshCore
- License: MIT-style upstream license text
- Use: MeshCore protocol/library base and feature compatibility target.
