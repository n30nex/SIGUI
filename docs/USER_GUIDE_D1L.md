# MeshCore DeskOS D1L User Guide

This firmware turns a Seeed SenseCAP Indicator D1L into a touch-first desk console for a local MeshCore mesh.

## What Works Now

- Canada/USA MeshCore radio profile: 910.525 MHz, BW62.5, SF7, CR5, 20 dBm, TCXO NONE.
- Local MeshCore identity stored in D1L NVS.
- Public MeshCore `test` send/receive against local bots.
- Signed advert receive/transmit.
- Persisted recent Public messages, bounded Public History/Search, DM rows, heard nodes, contacts, routes, packet evidence, unread state, and reset history.
- Messages opens to the Public channel by default, with an explicit DMs mode for retained direct-message conversations and thread review.
- Touch Public/DM compose enforces the 138-character MeshCore limit and shows the current character count.
- Tapping a Public message opens a detail sheet with sender, message text, signal, retained path evidence, and a Reply action that opens Public compose without transmitting until Send.
- Home is a quiet five-destination dashboard: Messages, Network, Map, and More are the task entries, while one Device card summarizes Time, Wi-Fi, Bluetooth, and SD state. Secondary tools such as Packets and diagnostics are grouped under More instead of competing on the Home screen.
- Targeted outbound DM to a local MeshCore bot has been verified through hardware counters and D1L packet/message logs.
- First-boot setup for node name, Canada/USA preset confirmation, Desk Companion role, offline radio defaults, and local identity generation.
- 480x480 dark touch shell with Home, Messages, Network, Map, More, full-height nested pages, toast feedback, onboarding, and lock overlay.
- More groups Tools, Connections, Storage & maps, Device, Support, and Advanced into simple menus. SD Card, Wi-Fi, BLE, Radio, Map Tiles, Display, Identity, Diagnostics, About, and Packets are leaf destinations rather than one busy grid.
- Staged touch radio settings editor for frequency, bandwidth, SF, CR, TX power, RX boost, and US/CAN defaults. Saved profile changes are persisted and flagged as reboot/apply pending.
- Mesh visibility summaries for signal, room servers, and repeater candidates. `Packets -> Mesh Roles` opens a two-choice root; Rooms and Repeaters then open separate bounded read-only lists, with Back returning one level at a time.
- Packets is now a terminal-style diagnostic feed with RX/TX/fail/error color treatment, filter/search controls, pause/resume, route evidence, and normal/advanced packet detail.
- Network shows role badges for companions, repeaters, room servers, sensors, and unknown nodes, plus a read-only heard-node detail page for role, fingerprint, public-key state, signal, path, and heard-count evidence.
- Read-only route trace helper for promoted contacts or known fingerprints, backed by retained route/contact evidence.
- Contact export for promoted contacts with retained public keys, using MeshCore-compatible `meshcore://contact/add?...` serial output and a touch QR sheet.
- USB serial diagnostics, smoke test, and soak test tooling.

## Release Package Flashing

Use a release package from GitHub Actions or `artifacts/release`.

Normal project flash writes the bootloader, partition table, and app image at ESP-IDF offsets while preserving unrelated flash regions.

```powershell
$env:D1L_PORT = "COMx"
.\flash_project.ps1 -Port $env:D1L_PORT
```

Do not use COM8, COM11, or COM29 for D1L flashing/testing unless the operator explicitly reassigns the hardware. In the current validation setup, use COM12 for the D1L.

The full 8MB image is for factory/recovery workflows and can overwrite persisted settings, contacts, messages, and logs. It requires typed confirmation:

```powershell
$env:D1L_PORT = "COMx"
.\flash_full_8mb.ps1 -Port $env:D1L_PORT
```

## After Flash

Run smoke from the repo checkout:

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
```

For a short active stability probe with local Public bots that respond to `test`:

```powershell
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 180 --sample-interval-sec 45 --active-public-text test --active-interval-sec 60 --require-rx-delta --min-tx-delta 1
```

## Useful Serial Commands

- `version`
- `settings get`
- `settings onboarding status`
- `settings onboarding complete <name>`
- `health`
- `crashlog`
- `mesh status`
- `mesh send public test`
- `radio get`
- `radio set preset uscan`
- `radio set txpower 20`
- `radio set rxboost <0|1>`
- `ui capture status`
- `ui capture begin`
- `ui capture chunk <offset> <len>`
- `ui capture end`
- `ui compose-probe <public|public-long|dm|dm-long|public-search|packet-search|contact-edit|onboarding|map-location|map-provider|wifi-ssid|wifi-password>`
- `ui scroll-probe <...|mesh_roles|mesh_rooms|mesh_repeaters|...>` (safe read-only page-open aliases for capture)
- `messages public`
- `messages public offset 8`
- `messages public search test`
- `messages public search test offset 8`
- `messages dm offset 8`
- `messages dm <fingerprint> offset 8`
- `messages unread`
- `nodes`
- `contacts`
- `contacts export`
- `contacts export <fingerprint>`
- `contacts rename <fingerprint> <alias>`
- `contacts delete <fingerprint>`
- `routes`
- `routes trace <fingerprint>`
- `packets`
- `signal`
- `roomservers`
- `repeaters`
- `wifi status`
- `wifi on`
- `wifi scan`
- `wifi save <ssid> [password]`
- `wifi connect`
- `wifi off`
- `wifi clear`
- `ble status`
- `storage status`
- `storage setup`
- `storage map-tile-canary <token>`
- `storage export-canary <token>`
- `storage export-diagnostics <token>`
- `storage export-data <token>`
- `storage map-policy`
- `map center`
- `map center set <lat> <lon>`
- `map center clear`

## Current Limits

- Manual physical review of the touch UI is still pending.
- Hardware pixel capture over the COM12 console is the primary way to prove what the D1L display drew. Merged PR #59 / source `d24552e` / Actions `29064260772` proves the simplified Contact Detail, Contact Options, and confirmation-only Forget pages with exact CRC matches and passing simulator diffs, plus a clean three-round all-tab probe with no Public RF or formatting. The newer Mesh Roles hierarchy remains pending its own exact Actions/COM12 capture and physical Back/scroll review; rerun only the probe named by the selected issue.
- Manual touch review of the Settings dashboard and Radio Settings sheet is still pending.
- Manual touch review of the Contact Edit rename/Forget sheet is still pending.
- Full DM ACK/PATH, direct-route, and inbound-DM RF proof is still pending.
- Route Trace is retained local evidence only; active RF ping/trace commands are not enabled yet.
- Contact export QR scanning/import has not yet been manually proven with a phone/client.
- Wi-Fi is off by default but can be explicitly enabled for local setup, bounded `wifi scan`, saved-profile `wifi connect`, and the touch Wi-Fi Setup sheet. Map Tiles setup has touch fields for an allowed HTTPS provider template, attribution, and zoom, but live touch downloads remain unavailable with `tile_render_pending` until tile rendering proof exists; the serial `storage map-tile-download <z> <x> <y> <url-template> <attribution>` primitive remains available for exact SD/cache validation. BLE companion runtime, OTA, and hardware proof remain pending.
- Optional SD-card data storage is staged but not fully hardware-accepted yet. Current builds expose `storage status`, `storage map-policy`, non-destructive `storage setup`, serial-only `storage filecanary`, serial-only `storage retained-canary <token>`, serial-only `storage map-tile-canary <token>`, serial-only `storage map-tile-download <z> <x> <y> <url-template> <attribution>`, serial-only `storage export-canary <token>`, serial-only `storage export-diagnostics <token>`, and serial-only `storage export-data <token>`, include a GitHub Actions-built RP2040 SD bridge target with generic file-operation protocol support, and never format SD cards. Users must provide FAT32 cards prepared on a computer. When the RP2040 bridge reports a ready card plus file-operation and atomic-rename support, retained Public/DM message history, route history, packet history, diagnostic exports, sampled user-data exports, and the map-tile cache can report SD backends with onboard fallback; packet history keeps the newest small NVS fallback plus a bounded 4096-record, 24h-target DeskOS-owned SD segment journal. The touch Map tab first asks for a D1L location when none is saved, offers `Set Pin`/`Move Pin` with decimal latitude/longitude keyboard entry, persists the same manual center used by `map center set <lat> <lon>`, opens Map Tiles provider setup from Map/Settings, and `storage map-policy` shows the offline cache path contract, cache readiness, provider-required download state, attribution requirement, and retained route/node counts. All tile downloads require connected Wi-Fi, ready SD cache, an allowed non-public-OSM HTTPS URL template, and attribution metadata; GPS and browser/geolocation integration remain pending. `storage filecanary` verifies the bridge file path with temp write, read-back, `rename replace=1`, stat, final read, delete, and cleanup without Public RF or formatting. `storage retained-canary <token>` appends synthetic retained Public/DM/route/packet rows only when those stores already report SD backends, so the acceptance runner can prove retained history survives reboot without Public RF. `storage map-tile-canary <token>` commits a synthetic tile under `map/tiles/` through temp write/read and atomic rename without Public RF or formatting. `storage export-canary <token>` writes one diagnostic export canary file under `exports/diagnostics/` through temp write/read and atomic rename, leaving the final JSON present for inspection. `storage export-diagnostics <token>` writes a bounded chunked diagnostic JSON bundle under `exports/diagnostics/`, including storage, health, crashlog, limits, and map-tile cache readiness. `storage export-data <token>` writes a bounded sampled user-data JSON bundle under `exports/data/data-export-<token>.json`, including recent messages, DMs, routes, packets, contacts, nodes, settings summary, read-state, and manual map-center summary while excluding private identity material. Settings, identity, contacts, and read-state remain onboard/fallback-backed or pending until later migrations.
- Full 12-hour idle/listening soak is still pending; the 1-hour active Public `test` soak has passed.
- Flash backup was intentionally skipped during current bring-up when the operator requested it.
