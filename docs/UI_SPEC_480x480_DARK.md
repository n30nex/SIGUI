# 480x480 Dark UI Spec

Phase 3 starts the production shell while preserving the diagnostic serial commands needed for bring-up:

- 480x480 fixed layout.
- Near-black blue background, graphite tiles, cyan status accent, amber warnings, red only for errors.
- Minimum touch target 44x44 px; primary targets 56x56 px or larger.
- Status hierarchy visible from desk distance.
- No Apple assets, SF Symbols, copied screenshots, or proprietary UI assets.

## Implemented Shell Slice

- Compact Home title bar; non-Home pages keep the full status bar with MeshCore state, RX/TX counters, identity state, and lock action.
- Home is a quiet dashboard with four task destinations: Messages, Network, Map, and More. A single Device card summarizes Time, Wi-Fi, Bluetooth, and SD state without turning each status into another competing button.
- The bottom dock uses the same five-destination information architecture on non-Home screens: Home, Messages, Network, Map, and More. Packets and diagnostics remain available under More instead of competing with primary tasks.
- Public message screen with persisted recent Public rows, a touch `test` send action, and a free-text composer sheet routed through the app model.
- Nodes screen that reports heard-node and contact counts, renders newest persisted contact rows when present, and falls back to newest persisted heard-node rows.
- Map screen that reports offline SD tile-cache policy/readiness, retained route/node counts, disabled live-download state, and an optional serial-configured manual center.
- Contact detail sheet with DM, favorite, mute, and MeshCore QR-compatible export actions.
- Packet log screen with bounded recent packet rows, route rows, first route detail sheet, and first packet detail sheet.
- More uses disclosure categories and nested rows: Tools, Connections, Storage & maps, Device, Support, and Advanced. Leaf actions include Packets, Diagnostics, Wi-Fi, Bluetooth, Radio, SD Card, Offline Maps, Display, Identity, and About.
- Radio Settings sheet with staged frequency, bandwidth, SF, CR, TX power, RX boost, US/CAN defaults, explicit Save, and live RF apply status.
- Modal advert sheet for zero-hop/flood actions.
- Toast feedback for touch actions.
- Lock/standby overlay with tap-to-unlock behavior.

The shell consumes `d1l_app_snapshot_t` from `app/app_model` and does not call MeshCore or HAL directly.

## Diagnostic Screens

- Display color bars via `display test`.
- Touch coordinate test via `touch test`.

## Navigation Rules

- Show one primary action per page. Put destructive, raw, flood, test, and developer actions under a clearly named secondary menu with confirmation where appropriate.
- Prefer full-width menu rows with a disclosure cue over grids of equal-weight buttons when an item leads to a deeper function.
- A row tap opens its detail or action page; inline buttons are reserved for the single most common safe action.
- Modal and nested pages hide the dock, provide one clear Back or Close action, and cannot leave background navigation looking active.
- User-facing summaries use plain language. Protocol identifiers, fingerprints, raw packet bytes, and hardware diagnostics live under Technical details or Advanced.
