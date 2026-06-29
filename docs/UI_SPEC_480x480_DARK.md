# 480x480 Dark UI Spec

Phase 3 starts the production shell while preserving the diagnostic serial commands needed for bring-up:

- 480x480 fixed layout.
- Near-black blue background, graphite tiles, cyan status accent, amber warnings, red only for errors.
- Minimum touch target 44x44 px; primary targets 56x56 px or larger.
- Status hierarchy visible from desk distance.
- No Apple assets, SF Symbols, copied screenshots, or proprietary UI assets.

## Implemented Shell Slice

- Top status bar with MeshCore state, RX/TX counters, identity state, and lock action.
- Home dashboard cards for mesh readiness, identity, RF packet counters, and system memory.
- Bottom dock navigation for Home, Messages, Nodes, Packets, and Settings.
- Public message screen with recent Public packet notes and a touch `test` send action routed through the app model.
- Nodes screen that reports live advert/RF counts while full contacts and routes remain pending.
- Packet log screen with bounded recent packet rows.
- Settings screen with radio profile, identity, path-hash, and advert quick action.
- Modal advert sheet for zero-hop/flood actions.
- Toast feedback for touch actions.
- Lock/standby overlay with tap-to-unlock behavior.

The shell consumes `d1l_app_snapshot_t` from `app/app_model` and does not call MeshCore or HAL directly.

## Diagnostic Screens

- Display color bars via `display test`.
- Touch coordinate test via `touch test`.

## Later Screens And Depth

- Full Public chat composer and scrollback.
- DMs.
- Heard nodes and contacts with persistent storage.
- Packet detail.
- Radio settings editing from touch.
- Diagnostics detail and reset/crash views.
