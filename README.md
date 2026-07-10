# MeshCore DeskOS D1L

MeshCore DeskOS D1L is firmware for the Seeed SenseCAP Indicator D1L: ESP32-S3, RP2040, 480x480 touch display, and SX1262 LoRa radio. The goal is a touch-first MeshCore desk console for Public messages, direct messages, node visibility, packet diagnostics, and optional FAT32 SD-card backed history.

Current public-release status: **not ready to tag**. The fast release path is issue-sized: start from current `main`, change one P0, run focused host tests, run the full host suite, use GitHub Actions with `include_sd_bridge=false` unless SD/RP2040 evidence changed, and run only the hardware proof named by the selected issue. Issue #63 now selects the version-pinned `espressif/idf:v5.5.4` Actions target because the former v5.1.x baseline is end of life, but v5.5.4 is not the qualified production baseline until the Actions-generated dependency lock, clean repeat build/package run, exact COM12 `version.idf=v5.5.4` and behavioral checks, and refreshed release evidence all pass. PR #60 / source `0b138be` passed push Actions `29068006554` and PR Actions `29068007961`, then proved Mesh Roles, Rooms, and Repeaters on COM12 with matching framebuffer CRCs `63DE54FB`, `FD538D71`, and `5C41EE08`, passing simulator diffs, and a clean three-round all-tab probe with `public_rf_tx=false` and `formats_sd=false`. The next Storage hierarchy is host-only until its exact Actions-built firmware is captured on COM12; it does not replace the core FAT32 bench evidence from `1a29876` / Actions `28714355561`. Release remains blocked by supported-SDK qualification, full RF/DM acceptance, the remaining physical SD matrix, manual physical photos/review, 12-hour soak, and the remaining #16/#6 UI modular ownership split. No release tag should be cut until the release-commit gate is green.

## Feature Matrix

Status values: Working, Hardware-proven, Partial, Experimental, Not started.

| Area | Status | Notes |
|---|---|---|
| ESP-IDF migration target | Partial | Issue #63 selects the version-pinned `espressif/idf:v5.5.4` Actions target. Qualification still requires the Actions-generated/reviewed dependency lock, a clean repeat firmware/package/checksum run, exact COM12 `version.idf=v5.5.4` plus behavioral proof, and refreshed commit-matched release evidence. The tag alone is not content-immutable or qualification. |
| GitHub Actions firmware package | Working | Default ESP32/UI CI builds the ESP32 firmware and release package without rebuilding RP2040 artifacts. `include_sd_bridge=true` or SD/RP2040 path changes opt into the RP2040 SD bridge UF2 and official Seeed SD smoke UF2 checksums. Firmware compilation and dependency-lock generation remain Actions-only. |
| Touch Home and shell navigation | Partial | Home is a quiet dashboard with Messages, Network, Map, and More destinations plus one Device status card. Non-Home pages use the matching five-item dock; Packets, diagnostics, connections, storage, and device settings live in disclosure categories under More. `ui_home.c` owns the Home renderer, `ui_settings.c` owns the More hierarchy and stable leaf actions, every top-level renderer receives the canonical content root explicitly from `ui_screen.c`, and `ui_modal.c` enforces one active sheet while the shell hides background dock navigation for every modal and preserves nested parent returns. PR #56 proves the current Home pixels and all-tab quick health on COM12. Messages, Network/routes, Map, and Packets still need owned renderers; settings sheets also remain in the shell pending their domain slices. UI-task command ownership, deeper callback-lifetime contracts, and manual physical photos/review are still open. |
| Compose/input keyboard capture | Hardware-proven | PR #35 / issue #2 captured all 12 release-blocking keyboard callers on COM12 from `fce5d82` / Actions `28727064923`: Public/DM compose, Public search, Packet search, contact edit, onboarding, map location/provider, and Wi-Fi SSID/password. The artifact reports `ok=true`, `capture_count=12`, `public_rf_tx=false`, and `formats_sd=false`; do not rerun this proof for unrelated PRs. |
| Public messages | Hardware-proven | Public TX/RX plumbing, retained Public history, search, unread/read state, Packet-tab evidence, and a full-height message detail page with wrapped text plus nested technical details exist. |
| Direct messages | Partial | DM TX/store/thread UI exists; the full-height thread page marks read on open and keeps one sticky Reply action. Full inbound DM, ACK/PATH, and direct-route acceptance remain release blockers. |
| Nodes, contacts, routes | Partial | Heard nodes, contacts, route trace/detail, role browser, and diagnostics exist. PR #59 proves the simplified contact hierarchy without invoking removal or Public RF. PR #60 / `0b138be` proves the bounded read-only Mesh Roles/Rooms/Repeaters pages on exact Actions-built COM12 pixels; physical touch review and final RF route proof remain open. |
| Packet diagnostics | Working | Packet list, filter/search, raw preview, detail sheet, and retained packet storage paths exist. |
| SD core file operations | Hardware-proven | Current COM12/COM16 evidence from `1a29876` / Actions `28714355561` proves official Seeed FAT32 smoke, FAT32 `READY_SD`, raw diagnostics, filecanary, safe boot scenarios for correct/missing/existing data plus RP2040-unavailable fallback, retained history after reboot, reboot/remount, map-tile canary, export canary, diagnostic export, and sampled data export without Public RF or formatting. |
| SD release matrix | Partial | The touch Storage hierarchy now separates Card status from scrollable Data locations and keeps a fixed no-format footer, but this new UI is still pending exact Actions/COM12 pixels and physical touch review. The release matrix also still needs physical no-card and unformatted/non-FAT32 scenario proof, <=32GB FAT32 multi-card proof, and power/electrical evidence. Users prepare FAT32 cards on a computer; there is no device-side formatting path. |
| Map and offline tiles | Partial | Manual center, provider setup, SD map-tile canary, and policy UI exist. GPS, tile rendering proof, and live touch tile browsing remain pending. |
| Wi-Fi | Experimental | Setup UI and bounded serial controls exist; disabled by default and not release-proven for map downloads. |
| BLE, OTA, GPS | Not started | Release-grade BLE companion transport, OTA, GPS/location-source integration, and nearby GPS node pins remain pending. |
| Soak and physical review | Partial | Short evidence exists. Full 12-hour idle/listening soak and physical photos/manual UI review are still open. |

Retained Public/DM message history, route history, packet history, diagnostic exports, sampled user-data exports, and map-tile cache can use SD only when the RP2040 bridge reports a ready FAT32 card with file operations and atomic rename. These retained stores keep NVS fallback available.

## Screenshots

These committed host simulator screenshots are representative of the current UI surfaces. They are not a substitute for physical device photos or issue-matched COM12 pixel-capture PNGs when a selected issue requires pixel evidence. Physical device photos are still required before release.

| Home | Messages | Network | Packets |
|---|---|---|---|
| ![Home](docs/screenshots/home.png) | ![Messages](docs/screenshots/messages.png) | ![Nodes](docs/screenshots/nodes.png) | ![Packets](docs/screenshots/packets.png) |

| More | Storage | Map |
|---|---|---|
| ![Settings](docs/screenshots/settings.png) | ![Storage](docs/screenshots/storage_setup_sheet.png) | ![Map](docs/screenshots/map.png) |

| Compose | DM Thread | Wi-Fi |
|---|---|---|
| ![Compose](docs/screenshots/compose_sheet.png) | ![DM Thread](docs/screenshots/dm_thread_sheet.png) | ![Wi-Fi](docs/screenshots/wifi_setup_sheet.png) |

## Host Checks

No hardware required. The local full suite remains available; CI runs ESP32/UI host
checks by default and runs SD/RP2040 dry-runs only when the SD bridge scope is
explicitly included.

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\ui_corruption_probe_d1l.py --dry-run --rounds 20
python .\scripts\ui_capture_d1l.py --dry-run
python .\scripts\ui_compose_keyboard_capture_d1l.py --dry-run --targets all
python .\scripts\scroll_probe_d1l.py --dry-run --screens home,public_messages,dm_thread,nodes,packets,settings,storage,storage_card,storage_data,wifi,map
python .\scripts\sd_file_canary_d1l.py --dry-run
python .\scripts\sd_retained_history_acceptance_d1l.py --dry-run --token dryrun
python .\scripts\sd_map_tile_canary_d1l.py --dry-run --token dryrun
python .\scripts\sd_reboot_remount_acceptance_d1l.py --dry-run --token dryrun
python .\scripts\sd_export_canary_d1l.py --dry-run --token dryrun
python .\scripts\sd_diagnostic_export_d1l.py --dry-run --token dryrun
python .\scripts\sd_data_export_d1l.py --dry-run --token dryrun
python .\scripts\release_gate_audit_d1l.py --out artifacts\release-gate\d1l-release-gate-audit-local.json
```

Firmware binaries are built by GitHub Actions only. The normal workflow path is
ESP32-first; it does not rebuild or package RP2040 artifacts unless
`include_sd_bridge=true` is selected or SD/RP2040 sources changed. The Windows
host should use downloaded Actions artifacts for any ESP32 or opt-in RP2040
hardware proof.

## Hardware Route

Current D1L bench defaults:

- ESP32 app/console: `COM12`
- RP2040 USB/CDC/UF2: `COM16`
- Do not use `COM8`, `COM11`, or `COM29` for D1L validation.
- Do not format SD from firmware, scripts, serial commands, or UI.
- Do not send Public RF during SD validation.

Issue-scoped hardware validation:

```powershell
python .\scripts\smoke_d1l.py --port COM12 --out artifacts\hardware\com12\smoke-<sha>-COM12.json
```

For normal P0 work, run the one COM12 proof that matches the selected issue
instead of cycling every UI surface. Use the full autonomous UI bundle only when
the issue explicitly spans multiple UI gates, or as a final release sweep.

Focused UI proof, choose the matching command only:

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\ui_corruption_probe_d1l.py --port $env:D1L_PORT --rounds 20 --clear-crashlog-before-start
python .\tools\ui_simulator.py --view home --out artifacts\ui-sim-reference\<sha>
python .\scripts\ui_capture_d1l.py --port $env:D1L_PORT --prep-command "ui tab home" --reference-png artifacts\ui-sim-reference\<sha>\home.png --reference-view home --out artifacts\hardware\com12\ui_pixel_capture-<sha>-COM12.json
python .\scripts\ui_compose_keyboard_capture_d1l.py --port $env:D1L_PORT --targets all --out artifacts\hardware\com12\ui_compose_keyboard_capture-<sha>-COM12.json
python .\scripts\scroll_probe_d1l.py --port $env:D1L_PORT --screens <screen-or-small-list> --manual-touch --clear-crashlog-before-start
```

Bundled COM12 UI sweep, not the default for every UI issue:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --skip-sd-suite --include-ui-probes
```

Only use the RP2040 refresh path when the bridge firmware, official SD smoke, or
SD electrical evidence actually changed or needs to be re-proven:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --refresh-rp2040-smoke
```

That refresh path captures official RP2040 SD smoke, the RP2040-unavailable
fallback window, bridge restore, preflight, raw diag,
file/export/map/retained/reboot canaries, smoke, and the final release-gate
audit. For SD-only refreshes where the ESP32 app is already flashed from the
matching Actions artifact, add `--skip-esp32-flash`.

Guided SD install, only when autonomous RP2040 access is not available:

```powershell
python .\scripts\guided_sd_install_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --d1l-port COM12 --rp2040-port COM16
```

## Documentation

Start with [docs/README.md](docs/README.md). The active docs are intentionally small:

- [User guide](docs/USER_GUIDE_D1L.md)
- [Developer guide](docs/DEVELOPER_GUIDE_D1L.md)
- [Current roadmap](docs/ROADMAP.md)
- [Release checklist](docs/RELEASE_CHECKLIST.md)
- [Known limitations](docs/KNOWN_LIMITATIONS.md)
- [D1L test plan](docs/TEST_PLAN_D1L.md)
- [Fast release workflow](docs/FAST_RELEASE_WORKFLOW_D1L.md)
- [Codex goal prompt](docs/CODEX_GOAL_PROMPT_D1L.md)
- [SD guided install](docs/D1L_SD_CARD_GUIDED_INSTALL.md)
- [RP2040 SD bridge flash/proof](docs/RP2040_SD_BRIDGE_FLASH_D1L.md)
- [SD bridge protocol](docs/SD_BRIDGE_PROTOCOL_D1L.md)
- [Companion compatibility](docs/COMPANION_3BYTE_COMPATIBILITY.md)
- [Attribution](docs/ATTRIBUTIONS.md) and [source audit](docs/SOURCE_AUDIT_AND_ATTRIBUTION.md)

## Licensing

MeshCore DeskOS D1L is GPL-3.0-or-later; see [LICENSE](LICENSE). Release packages include third-party notices and attribution for Seeed SenseCAP Indicator materials, MeshCore, and permitted SigurdOS-derived references.
