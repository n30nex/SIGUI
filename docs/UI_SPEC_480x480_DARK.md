# 480x480 Dark UI Spec

Phase 1 implements only the diagnostic shell needed for bring-up:

- 480x480 fixed layout.
- Near-black blue background, graphite tiles, cyan status accent, amber warnings, red only for errors.
- Minimum touch target 44x44 px; primary targets 56x56 px or larger.
- Status hierarchy visible from desk distance.
- No Apple assets, SF Symbols, copied screenshots, or proprietary UI assets.

## Phase 1 Screens

- Home placeholder with firmware name and Canada/USA radio profile.
- Display color bars via `display test`.
- Touch coordinate test via `touch test`.

## Later Screens

- Home dashboard.
- Public chat and DMs.
- Heard nodes and contacts.
- Packet log and packet detail.
- Radio settings.
- Diagnostics.

UI screens consume app/model snapshots and should not call MeshCore or HAL directly except through controller glue. This keeps the LVGL app portable while Phase 1/2 hardware work settles.
