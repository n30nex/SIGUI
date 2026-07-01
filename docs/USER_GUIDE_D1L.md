# MeshCore DeskOS D1L User Guide

This firmware turns a Seeed SenseCAP Indicator D1L into a touch-first desk console for a local MeshCore mesh.

## What Works Now

- Canada/USA MeshCore radio profile: 910.525 MHz, BW62.5, SF7, CR5, 20 dBm, TCXO NONE.
- Local MeshCore identity stored in D1L NVS.
- Public MeshCore `test` send/receive against local bots.
- Signed advert receive/transmit.
- Persisted recent Public messages, bounded Public History/Search, DM rows, heard nodes, contacts, routes, packet evidence, unread state, and reset history.
- Touch Public/DM compose enforces the 138-character MeshCore limit and shows the current character count.
- Tapping a Public message opens a detail sheet with sender, message text, signal, retained path evidence, and a Reply action that opens Public compose without transmitting until Send.
- Targeted outbound DM to a local MeshCore bot has been verified through hardware counters and D1L packet/message logs.
- First-boot setup for node name, Canada/USA preset confirmation, Desk Companion role, offline radio defaults, and local identity generation.
- 480x480 dark touch shell with Home, Messages, Nodes, Packets, Settings, modal sheets, toast feedback, onboarding, and lock overlay.
- Staged touch radio settings editor for frequency, bandwidth, SF, CR, TX power, RX boost, and US/CAN defaults. Saved profile changes are persisted and flagged as reboot/apply pending.
- Mesh visibility summaries for signal, room servers, and repeater candidates.
- Nodes tab role badges for companions, repeaters, room servers, sensors, and unknown nodes, plus a read-only heard-node detail sheet for role, fingerprint, public-key state, signal, path, and heard-count evidence.
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

Do not use COM11 or COM29 for D1L flashing/testing unless the operator explicitly reassigns the hardware. In the current validation setup, use COM12 for the D1L.

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
- `messages public`
- `messages public search test`
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
- `wifi scan`
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
- Manual touch review of the Radio Settings sheet is still pending.
- Manual touch review of the Contact Edit rename/Forget sheet is still pending.
- Full DM ACK/PATH, direct-route, and inbound-DM RF proof is still pending.
- Route Trace is retained local evidence only; active RF ping/trace commands are not enabled yet.
- Contact export QR scanning/import has not yet been manually proven with a phone/client.
- Wi-Fi runtime, BLE companion runtime, OTA, and live Wi-Fi scan/connect are not enabled yet.
- Optional SD-card data storage is staged but not fully hardware-accepted yet. Current builds expose `storage status`, `storage map-policy`, non-destructive `storage setup`, serial-only `storage filecanary`, serial-only `storage retained-canary <token>`, serial-only `storage map-tile-canary <token>`, serial-only `storage export-canary <token>`, serial-only `storage export-diagnostics <token>`, and serial-only `storage export-data <token>`, include a GitHub Actions-built RP2040 SD bridge target with generic file-operation protocol support, and do not run boot-time formatting. When the RP2040 bridge reports a ready card plus file-operation and atomic-rename support, retained Public/DM message history, route history, packet history, diagnostic exports, sampled user-data exports, and the map-tile cache can report SD backends with onboard fallback. The touch Map tab first asks for a D1L location when none is saved, offers `Set Pin`/`Move Pin` with pan/zoom/drop controls, persists the same manual center used by `map center set <lat> <lon>`, and `storage map-policy` shows the offline cache path contract, cache readiness, disabled live-download state, and retained route/node counts; live network tile downloads, GPS, and browser/geolocation integration remain pending. `storage filecanary` verifies the bridge file path with temp write, read-back, `rename replace=1`, stat, final read, delete, and cleanup without Public RF or formatting. `storage retained-canary <token>` appends synthetic retained Public/DM/route/packet rows only when those stores already report SD backends, so the acceptance runner can prove retained history survives reboot without Public RF. `storage map-tile-canary <token>` commits a synthetic tile under `map/tiles/` through temp write/read and atomic rename without Public RF or formatting. `storage export-canary <token>` writes one diagnostic export canary file under `exports/diagnostics/` through temp write/read and atomic rename, leaving the final JSON present for inspection. `storage export-diagnostics <token>` writes a bounded chunked diagnostic JSON bundle under `exports/diagnostics/`, including storage, health, crashlog, limits, and map-tile cache readiness. `storage export-data <token>` writes a bounded sampled user-data JSON bundle under `exports/data/data-export-<token>.json`, including recent messages, DMs, routes, packets, contacts, nodes, settings summary, read-state, and manual map-center summary while excluding private identity material. Settings, identity, contacts, and read-state remain onboard/fallback-backed or pending until later migrations. The confirmation phrase path is guarded and refuses unless the RP2040 bridge first reports a present card, format support, and setup-required state.
- Full 12-hour idle/listening soak is still pending; the 1-hour active Public `test` soak has passed.
- Flash backup was intentionally skipped during current bring-up when the operator requested it.
