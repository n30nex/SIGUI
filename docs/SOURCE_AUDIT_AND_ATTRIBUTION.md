# Source Audit and Attribution

Last updated: 2026-07-13

This project uses references for architecture and feature parity, but Phase 1 source is newly written except for git submodules.

Top-level project license: GPL-3.0-or-later. The public release package must include `LICENSE`, `THIRD_PARTY_NOTICES.md`, `docs/ATTRIBUTIONS.md`, and this source audit.

## Included Submodules

- Seeed SenseCAP Indicator ESP32 SDK: https://github.com/Seeed-Solution/sensecap_indicator_esp32
  - License: Apache-2.0.
  - Use: D1L ESP-IDF components for BSP, RGB LCD, touch, IO expander, and SX1262 wiring. WP-04 host conformance also compiles the submodule's Brian Gladman `soft-se/aes.c` implementation only inside the sanitized oracle target; its permissive source notice remains in `aes.c`/`aes.h` and the source is pinned by canonical-LF SHA-256.
  - Risk: Seeed LoRa sample code can enable SX1262 DIO3 TCXO control when its board-detect pin suggests TCXO. MeshCore DeskOS D1L currently treats TCXO as `NONE` by default and avoids calling TCXO setup during Phase 1 radio hardware probe.

- MeshCore upstream: https://github.com/meshcore-dev/MeshCore
  - License: MIT-style `license.txt` in the upstream repo, with bundled third-party notices.
  - Pinned gitlink reviewed for the issue #65 wire-envelope slice: `e8d3c53ba1ea863937081cd0caad759b832f3028`.
  - Use: protocol reference plus the exact structural packet-envelope oracle and bounded advert/public-group/route/ACK/TRACE golden-vector target for issue #65. The production service retains its original narrow C adapter rather than linking the upstream service/chat stack.
  - Conformance boundary: the packet-envelope result remains `wire_envelope_only`; the separately versioned WP-04 oracle also pins advert fields, strict signed-advert verification, `BaseChatMesh` channel hashing, `mesh::Utils` public-group AES/HMAC creation and authenticated parsing, route preparation, ACK framing, and source TRACE framing. The strict advert verifier uses the vendored C Ed25519 implementation with production canonical-scalar, canonical-point, and low-order-point checks. The upstream native-test AES/SHA mocks are excluded; group crypto instead uses the source-pinned Seeed/Brian Gladman AES implementation and a functional host SHA-256/HMAC adapter checked against FIPS/RFC known-answer vectors. Neither result proves full Mesh dispatch, delivery, retained state, real-peer interop, full conformance, or release readiness.
  - Risk: Upstream is Arduino/PlatformIO oriented in many paths; ESP-IDF integration may need an adapter layer rather than direct reuse. Optional web stack dependencies must be reviewed before Wi-Fi management is added.

## Reference Repositories

- SigurdOS T-Deck: https://github.com/hermes-gadget/SigurdOS-tdeck
  - Source commit reviewed: `e7e7b12ee771edd2b15e64e1a6569b7b18f84423` on `dev`.
  - License: GPL-3.0-or-later.
  - Metadata reviewed for release attribution: default branch `dev`, root `LICENSE`, README license wording, `LICENSES/DejaVu-Fonts.txt`, and MeshCore submodule pin `60ea4a91bf14363e837037a79ce1bff7fa37483f`.
  - Upstream copyright headers reviewed: `Copyright (C) 2025 Ben`.
  - Permission: maintainers gave this project permission to use the work with attribution. Public release evidence still needs the archived permission date, channel/link, and exact scope.
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

## External Map Data and Tile Service

- OpenStreetMap Standard tiles: https://tile.openstreetmap.org/
  - Copyright and attribution: https://www.openstreetmap.org/copyright
  - Data license: ODbL 1.0, https://opendatacommons.org/licenses/odbl/1-0/
  - Service policy: https://operations.osmfoundation.org/policies/tiles/
  - Required attribution: `© OpenStreetMap contributors`; the UI keeps the hardware-font-compatible `(c) OpenStreetMap contributors` visible on the actual Map.
  - Request boundary: built-in source only; at most the visible current-view 3x3 at one zoom per visible generation, only while Map is visible, with tile-cache reuse. The user may pan with one finger and select zooms 8 through 14, but each committed view remains a separate bounded plan; drag motion, probes, background tasks, and hidden Map surfaces make no tile requests. A completed exact-view Home-to-Map revisit uses its retained rendered frame without network or SD reread. No provider editor, arbitrary URL, multi-zoom prefetch, off-screen batch, or area download is permitted.
  - Privacy: a tile request discloses the viewed approximate area and network address to the tile service/CDN. Network access therefore requires explicit Wi-Fi setup and an actively visible Map.
  - Independence: reference to OpenStreetMap does not imply endorsement by OpenStreetMap or the OpenStreetMap Foundation.

## Attribution Policy

Keep third-party code in submodules or clearly attributed components. New MeshCore DeskOS files should use original implementations and avoid proprietary icons, Apple assets, untracked copied SigurdOS GPL code, and unlicensed LimitlezzOS source.

If this firmware later copies/adapts SigurdOS source code rather than architecture/behavior, the project should be treated as GPL-compatible and release obligations must be updated, including per-file SPDX/modification notices and public source availability for distributed binaries.
