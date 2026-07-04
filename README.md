# MeshCore DeskOS D1L

MeshCore DeskOS D1L is firmware for the Seeed SenseCAP Indicator D1L: ESP32-S3, RP2040, 480x480 touch display, and SX1262 LoRa radio. The goal is a touch-first MeshCore desk console for Public messages, direct messages, node visibility, packet diagnostics, and optional FAT32 SD-card backed history.

Current public-release status: **not ready to tag**. Core SD card support is working on the current bench artifact set, but release remains blocked by targeted UI corruption proof, full RF/DM acceptance, the broader SD matrix, manual physical photos/review, and long soak evidence. `scripts/release_gate_audit_d1l.py` currently reports `ready_for_public_release=false`. No release tag should be cut until that gate is green on the release commit.

## Feature Matrix

| Area | Status | Notes |
|---|---|---|
| GitHub Actions firmware package | Working | Builds ESP32 firmware, release package, RP2040 SD bridge UF2, and official Seeed SD smoke UF2 with checksums. |
| Touch shell | Partial | Home, Messages, Nodes, Map, Packets, Settings, sheets, keyboard, and simulator coverage exist. Current work targets split-page redraw corruption and keyboard/sheet layout polish. |
| Public messages | Hardware-proven core | Public TX/RX plumbing, retained Public history, search, unread/read state, and Packet-tab evidence exist. |
| Direct messages | Partial | DM TX/store/thread UI exists. Full inbound DM, ACK/PATH, and direct-route acceptance remain release blockers. |
| Nodes, contacts, routes | Partial | Heard nodes, contacts, route trace/detail, role browser, and diagnostics exist. Physical review and final RF route proof remain open. |
| Packet diagnostics | Working core | Packet list, filter/search, raw preview, detail sheet, and retained packet storage paths exist. |
| SD core file operations | Hardware-proven core | Current COM12/COM16 evidence proves FAT32 `READY_SD`, filecanary, retained-canary, retained history after reboot, reboot/remount, map-tile canary, and RP2040 raw diagnostics without Public RF or formatting. |
| SD release matrix | Partial | Still needs current official Seeed smoke, boot/retry manager proof, non-FAT32 behavior, <=32GB FAT32 matrix, no-format evidence policy, and power/electrical evidence. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. |
| Map and offline tiles | Partial | Manual center, provider setup, SD map-tile canary, and policy UI exist. GPS, tile rendering proof, and live touch tile browsing remain pending. |
| Wi-Fi | Experimental | Setup UI and bounded serial controls exist; disabled by default and not release-proven for map downloads. |
| BLE, OTA, GPS | Not release-ready | BLE companion transport, OTA, GPS/location-source integration, and nearby GPS node pins remain pending. |
| Soak and physical review | Partial | Short evidence exists. Full 12-hour idle/listening soak and physical photos/manual UI review are still open. |

Retained Public/DM message history, route history, packet history, diagnostic exports, sampled user-data exports, and map-tile cache can use SD only when the RP2040 bridge reports a ready FAT32 card with file operations and atomic rename. These retained stores keep NVS fallback available.

## Screenshots

These are current host simulator screenshots from the `e3c0510` / Actions `28690375628` artifact set. Physical device photos are still required before release.

| Home | Messages | Packets |
|---|---|---|
| ![Home](docs/screenshots/home.png) | ![Messages](docs/screenshots/messages.png) | ![Packets](docs/screenshots/packets.png) |

| Settings | Storage | Map |
|---|---|---|
| ![Settings](docs/screenshots/settings.png) | ![Storage](docs/screenshots/storage_setup_sheet.png) | ![Map](docs/screenshots/map.png) |

| Compose | DM Thread | Wi-Fi |
|---|---|---|
| ![Compose](docs/screenshots/compose_sheet.png) | ![DM Thread](docs/screenshots/dm_thread_sheet.png) | ![Wi-Fi](docs/screenshots/wifi_setup_sheet.png) |

## Host Checks

No hardware required:

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\ui_corruption_probe_d1l.py --dry-run --rounds 20
python .\scripts\scroll_probe_d1l.py --dry-run --screens home,public_messages,dm_thread,nodes,packets,settings,storage,wifi,map
python .\scripts\sd_file_canary_d1l.py --dry-run
python .\scripts\sd_retained_history_acceptance_d1l.py --dry-run --token dryrun
python .\scripts\sd_map_tile_canary_d1l.py --dry-run --token dryrun
python .\scripts\sd_reboot_remount_acceptance_d1l.py --dry-run --token dryrun
python .\scripts\release_gate_audit_d1l.py --out artifacts\release-gate\d1l-release-gate-audit-local.json
```

Firmware binaries are built by GitHub Actions only. The Windows host should use downloaded Actions artifacts for ESP32 and RP2040 hardware proof.

## Hardware Route

Current D1L bench defaults:

- ESP32 app/console: `COM12`
- RP2040 USB/CDC/UF2: `COM16`
- Do not use `COM8`, `COM11`, or `COM29` for D1L validation.
- Do not format SD from firmware, scripts, serial commands, or UI.
- Do not send Public RF during SD validation.

Autonomous validation:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --include-ui-probes
```

Focused UI proof:

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\ui_corruption_probe_d1l.py --port $env:D1L_PORT --rounds 20 --clear-crashlog-before-start
```

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
- [SD guided install](docs/D1L_SD_CARD_GUIDED_INSTALL.md)
- [RP2040 SD bridge flash/proof](docs/RP2040_SD_BRIDGE_FLASH_D1L.md)
- [SD bridge protocol](docs/SD_BRIDGE_PROTOCOL_D1L.md)
- [Companion compatibility](docs/COMPANION_3BYTE_COMPATIBILITY.md)
- [Attribution](docs/ATTRIBUTIONS.md) and [source audit](docs/SOURCE_AUDIT_AND_ATTRIBUTION.md)

## Licensing

MeshCore DeskOS D1L is GPL-3.0-or-later; see [LICENSE](LICENSE). Release packages include third-party notices and attribution for Seeed SenseCAP Indicator materials, MeshCore, and permitted SigurdOS-derived references.
