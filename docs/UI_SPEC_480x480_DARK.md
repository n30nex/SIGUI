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
- Message Detail and DM Thread use full-height nested pages with one Back control, a scrollable content body, and a sticky full-width Reply action. Long Public text determines its own height before Technical details; opening a DM thread marks it read.
- Nodes screen that reports heard-node and contact counts, renders newest persisted contact rows when present, and falls back to newest persisted heard-node rows.
- Map is the actual current-view surface, not a setup dashboard. The map canvas fills the complete content region between the global status bar and dock; there is no map-local title row. OpenStreetMap Standard is dark-styled locally after decode, preserving the single built-in source/cache and attribution while giving bright signed-advert node markers and their names below them strong contrast. Marker refresh is a bounded lightweight overlay and does not rebuild tiles: it reads at most the 32 newest located nodes, displays at most eight, keeps its non-clickable marker/name layer aligned with the tile image during drag, and skips a marker when its required below-marker name would collide with an earlier marker, a control, progress/status copy, or attribution. A viewport-level 44 px hit area opens existing Node Detail by retained fingerprint; the detail row says `Advert location`, and closing it reacquires the unchanged retained Map view. Its sparse edge overlays provide one-finger pan, direct 48x48-or-larger `-`, `+`, and `Center` controls, one `Options` setup action, an unobtrusive zoom/status badge, and the always-visible ASCII attribution `(c) OpenStreetMap contributors`. It starts at regional zoom 10, clamps user zoom to 8 through 14, and `Center` returns to the saved manual location. D1L has no onboard GPS; peer coordinates are labelled `Advert location`, never live GPS. If the SD file gate is still preparing, the uncluttered overlay says `Waiting for SD` and the same physically opened generation resumes automatically when ready. While a bounded plan is active, the drag hint becomes a compact `Loading n/N` or `Downloading n/N` label above a thin completed-tile progress bar rather than leaving a static spinner/message with no progress feedback.
- Contact Detail uses the full-height nested-page pattern. A canonical chat contact exposes only `Message` and `Contact options`; repeater, room, sensor, or unknown roles do not expose a dead Message action. Contact Options owns Route, Export, Rename, favorite/mute state, and the destructive contact-removal path. Export QR is actionable only for a retained full public key plus a known canonical MeshCore role; an unknown or malformed role shows a non-clickable unavailable row. Route, Export, and Rename return to Contact Options rather than flattening those functions back into Contact Detail.
- Removing a contact requires a dedicated confirmation page. `Cancel` and every `Back` path are non-destructive and restore Contact Options; only the explicit confirmation callback may delete the retained contact.
- Packet log screen with bounded recent packet rows, route rows, first route detail sheet, and first packet detail sheet.
- Mesh Roles follows `Packets -> Mesh Roles -> Rooms or Repeaters`: the full-height root contains only the two role categories, each child owns one bounded 448x320 vertical list, and read-only observation rows have no click, RF, or destructive callback. Child Back returns to Mesh Roles; root Back returns to Packets.
- Storage follows `More -> Storage & maps -> SD Card -> Card status or Data locations`: all three pages are full-height and read-only, Card status uses plain-language media guidance, Data locations owns the bounded vertical list of store backends, and a fixed footer states that DeskOS never formats cards. Child Back returns to Storage; root Back returns to More.
- Map follows `Map -> Map options -> Set location or Cache status` (canonical surfaces `map -> map_options -> map_location or map_cache`). OpenStreetMap Standard is built in; there is no provider, key, source, or account editor. Back from Set location or Cache status returns to Map options; saving a location returns to Map so loading can begin.
- More uses disclosure categories and nested rows: Tools, Connections, Storage & maps, Device, Support, and Advanced. Leaf actions include Packets, Diagnostics, Wi-Fi, Bluetooth, Radio, SD Card, Map options, Display, Identity, and About.
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
- Contact actions follow `Network -> Contact Detail -> Contact options -> sub-function`. Destructive removal is never colocated with Message and never occurs from a Back, Cancel, keyboard-cancel, or child-page close callback.
- Mesh observation browsing follows `Packets -> Mesh Roles -> role list`. Do not flatten Rooms and Repeaters into one mixed scrolling sheet or attach actions to observation rows.
- Storage browsing never exposes raw setup-action slugs or mount/remount/delete/format callbacks. Keep Card status and Data locations separate, keep the no-format footer fixed while Data locations scrolls, and put serial-only maintenance commands outside the touch hierarchy.
- Interactive Map network policy is fail-closed: only while the actual Map is visible may firmware request at most the visible current-view 3x3 at one zoom per visible generation, selected by the user. A drag previews locally and commits one new bounded plan only on release; a `-`, `+`, or `Center` tap may likewise commit only the resulting visible view. Hiding Map cancels unfinished work. A completed exact-view Home-to-Map revisit must display the retained rendered frame without a new generation, network request, or SD tile reread. Tile cache remains the reboot/later-session reuse layer. There is no background fetch, multi-zoom prefetch, off-screen batch, or area download.
- Map page probes are network-suppressed navigation. `map`, `map_options`, `map_location`, and `map_cache` may open their pages for evidence, but probes never request map tiles and never mutate Wi-Fi, RF, or storage.
