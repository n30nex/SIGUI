# MeshCore DeskOS D1L Core 1.0

MeshCore DeskOS D1L Core 1.0 is a touch-first, non-forwarding MeshCore desk
client for the Seeed SenseCAP Indicator D1L. It focuses on reliable Public and
direct messaging, local mesh visibility, diagnostics, and safe USB recovery.

The shipping profile is immutable:

```text
D1L_RELEASE_PROFILE=core_1_0
D1L_SD_HISTORY_MODE=disabled
```

Release status is fail-closed. An untagged checkout, green source test,
simulator image, predecessor binary, or dry run is not a release. Tag
`v1.0.0` is authorized only when the exact GitHub Actions package, COM12
hardware gates, controlled-peer RF/DM acceptance, and Core audit all pass.
Full Feature readiness remains false.

## Core capability matrix

| Capability | Core 1.0 |
|---|---|
| D1L board, 480×480 display, touch, backlight | Supported |
| Home | Supported |
| Fixed default Public channel | Supported |
| Direct messages and truthful delivery state | Supported |
| Basic verified contacts | Supported |
| Nodes and bounded detail | Supported |
| Packet log/search/filter | Supported, read-only |
| DM route plus signal/route diagnostics | Supported, read-only |
| Canada/USA radio settings | Supported |
| Identity and adverts | Supported |
| Retained settings and Core state | Internal NVS |
| Diagnostics, crashlog, and health | Supported |
| Fixed UTC offset and truthful time state | Supported |
| Checksum-verified USB install/recovery | Supported |
| SD history | Disabled; RP2040 payload omitted |

## Intentionally unavailable

Core 1.0 does not expose or start:

- Map or tile networking;
- user-controlled Wi-Fi;
- BLE;
- multi-channel management;
- repeater or room-server administration;
- Observer/MQTT;
- signed SD update or OTA;
- GPS/location;
- mutable terminal/log UI;
- advanced QR/emoji tools;
- user-facing active TRACE/PATH tools;
- a notification-system claim beyond unread counters.

Unavailable mutations are rejected before side effects with
`ESP_ERR_NOT_SUPPORTED`, `release_profile="core_1_0"`, and the unavailable
feature ID.

## Core navigation

The on-device dock contains exactly:

1. Home
2. Messages
3. Nodes
4. Packets
5. Settings

Messages contains Public and Direct messages. Read-only navigation, searches,
scrolling, and probes are RF-silent. Sending requires an explicit user action.
Release automation never transmits on the default Public channel.

## Build and test policy

Firmware binaries are built only by the repository's `d1l-ci` GitHub Actions
workflow. Local workstations may run host tests and inspect source, but may not
compile release firmware.

The final source gate is:

```powershell
python -m pytest tests -q
python .\scripts\completion_ledger.py validate --check-generated
python .\scripts\completion_pack_manifest.py check
python .\scripts\core_release_gate_audit_d1l.py --dry-run
git diff --check
```

The frozen candidate workflow must produce the host evidence, MeshCore
conformance/fuzz evidence, ESP-IDF v5.5.4 firmware, release package, checksums,
provenance, and SBOM for one exact commit. The Core candidate uses
`include_sd_bridge=false`.

## Install the exact release package

Download the package attached to the GitHub release, verify all checksums, and
use the package's explicit-port helper:

```powershell
python .\scripts\verify_checksums.py <extracted-package-directory>
$env:D1L_PORT = "COM12"
.\flash_project.ps1 -Port $env:D1L_PORT
```

Normal project flashing is non-erasing. The full 8 MB recovery image can
overwrite retained settings, contacts, messages, and logs, and requires typed
confirmation.

Hardware safety rules:

- use COM12 for the D1L;
- use COM16 only for separately authorized SD/RP2040 work;
- never use COM8, COM11, or COM29;
- never format SD;
- never automate default Public RF;
- flash only the exact checksum-verified Actions candidate.

SD is not required for Core 1.0. The Core package omits RP2040 firmware and
uses internal NVS for retained state.

## Documentation

- [Core user guide](docs/USER_GUIDE_D1L.md)
- [Flash and recovery](docs/FLASH_RECOVERY_D1L.md)
- [Core product contract](docs/release/SIGUI_CORE_1_0_PRODUCT_CONTRACT_2026-07-18.md)
- [24-hour execution ledger](docs/release/24H_STATUS.md)
- [Core audit and roadmap](docs/release/SIGUI_24H_AUDIT_AND_ROADMAP_2026-07-18.md)
- [Core execution backlog](docs/release/SIGUI_24H_EXECUTION_BACKLOG_2026-07-18.yaml)
- [Attribution](docs/ATTRIBUTIONS.md)

The historical Full Feature development guide is retained at
`docs/FULL_FEATURE_DEVELOPMENT_GUIDE_D1L_2026-07-18.md`; it describes
unavailable future surfaces and is not part of the Core package.

## Licensing

MeshCore DeskOS D1L is GPL-3.0-or-later. Release packages include third-party
notices and source attribution for the pinned dependencies and permitted
references.
