# Attributions

This project builds on open hardware/software work from upstream firmware projects. Keep this file with public source archives, release notes, and binary firmware releases.

MeshCore DeskOS D1L is released under GPL-3.0-or-later. Release packages must include the top-level `LICENSE` file with these attribution and notice documents.

## Seeed SenseCAP Indicator

- Project: Seeed SenseCAP Indicator ESP32 SDK and examples
- Repository: https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32
- Use in this project: board support package, LCD/touch integration patterns, D1L/GX display configuration, and firmware build baseline.

## SigurdOS-TDeck

- Project: SigurdOS-TDeck
- Repository: https://github.com/hermes-gadget/SigurdOS-tdeck
- Upstream default branch reviewed: `dev`
- Source commit reviewed: `e7e7b12ee771edd2b15e64e1a6569b7b18f84423`
- Root license file reviewed: `LICENSE`
- README license statement reviewed: `GPL-3.0-or-later`
- Bundled notice reviewed: `LICENSES/DejaVu-Fonts.txt`
- MeshCore submodule pin observed upstream: `60ea4a91bf14363e837037a79ce1bff7fa37483f`
- Upstream copyright headers reviewed: `Copyright (C) 2025 Ben`
- Upstream license headers reviewed: `GPL-3.0-or-later`
- Permission: the maintainers gave this project permission to use the work with attribution. Before public release, archive the date, channel/link, and exact scope of that permission with the release evidence.
- Use in this project: reference architecture for a production touch-first MeshCore desk firmware, especially UI shell structure, navigation patterns, cached input state, storage/map concepts, packet/log visibility, release tooling, and hardware test coverage ideas. No literal SigurdOS source is copied into this D1L slice.
- 2026-07-01 production UI pass: reviewed SigurdOS-TDeck Settings, Bluetooth, and Wi-Fi network screens as workflow references for category-style setup/status surfaces. The DeskOS D1L Wi-Fi/BLE setup sheets and packet terminal UI were implemented as original ESP-IDF/LVGL code in this repository.
- Upstream files/docs reviewed for this production slice: `docs/HOME_SCREEN.md`, `docs/MAP_SCREEN.md`, `docs/MESH_NETWORKING.md`, `docs/CONTACT_STORE.md`, `src/ui/home_screen.*`, `src/ui/navigation.*`, `src/ui/screens_common.*`, `src/app/map_renderer.*`, `src/app/tile_cache.*`, `src/mesh/persistence_store.*`, `src/mesh/contact_store.*`, `src/mesh/message_store.*`, `src/ui/screens/screen_packets.cpp`, `src/ui/screens/screen_settings.cpp`, `src/ui/screens/screen_bluetooth.cpp`, `src/ui/screens/screen_wifi_networks.cpp`, `scripts/build_metadata.py`, `scripts/merge_bin.py`, and related tests.

SigurdOS-TDeck targets the LilyGo T-Deck GT911 touch controller, while this firmware targets the Seeed SenseCAP Indicator D1L FT5x06/GX panel. D1L touch controller code therefore stays on the Seeed BSP/FT5x06 path, with SigurdOS-TDeck used as an attributed reference for input architecture and validation behavior rather than blindly copying the GT911 driver.
