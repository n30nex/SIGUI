# 480x480 Dark UI Spec

Phase 3 starts the production shell while preserving the diagnostic serial commands needed for bring-up:

- 480x480 fixed layout.
- Near-black blue background, graphite tiles, cyan status accent, amber warnings, red only for errors.
- Minimum touch target 44x44 px; primary targets 56x56 px or larger.
- Status hierarchy visible from desk distance.
- No Apple assets, SF Symbols, copied screenshots, or proprietary UI assets.

## Implemented Shell Slice

- Top status bar with MeshCore state, RX/TX counters, identity state, and lock action.
- Home dashboard cards for mesh readiness, identity, RF packet/route counters, and system memory.
- Bottom dock navigation for Home, Messages, Nodes, Map, Packets, and Settings.
- Public message screen with persisted recent Public rows, a touch `test` send action, and a free-text composer sheet routed through the app model.
- Nodes screen that reports heard-node and contact counts, renders newest persisted contact rows when present, and falls back to newest persisted heard-node rows.
- Map screen that reports offline SD tile-cache policy/readiness, retained route/node counts, disabled live-download state, and an optional serial-configured manual center.
- Contact detail sheet with DM, favorite, mute, and MeshCore QR-compatible export actions.
- Packet log screen with bounded recent packet rows, route rows, first route detail sheet, and first packet detail sheet.
- Settings screen with live persisted radio profile, identity, companion status, health, radio editor entry point, and advert quick action.
- Radio Settings sheet with staged frequency, bandwidth, SF, CR, TX power, RX boost, US/CAN defaults, explicit Save, and reboot/apply warning.
- Modal advert sheet for zero-hop/flood actions.
- Toast feedback for touch actions.
- Lock/standby overlay with tap-to-unlock behavior.

The shell consumes `d1l_app_snapshot_t` from `app/app_model` and does not call MeshCore or HAL directly.

## Diagnostic Screens

- Display color bars via `display test`.
- Touch coordinate test via `touch test`.

## Later Screens And Depth

- Deeper Public scrollback/search.
- DMs.
- Touch map-center entry, GPS/location-source integration, richer contact editing, contact scan/import proof, and route trace/ping helpers.
- Packet filtering/search and raw packet hex in developer mode.
- Diagnostics detail and reset/crash views.
