# MeshCore DeskOS D1L

MeshCore DeskOS D1L is firmware for the Seeed SenseCAP Indicator D1L: ESP32-S3, RP2040, 480x480 touch display, and SX1262 LoRa radio. The goal is a touch-first MeshCore desk console for Public messages, direct messages, node visibility, packet diagnostics, and optional FAT32 SD-card backed history.

Current public-release status: **not ready to tag**. Exact main `de0bb75bd91146f0dc9896540d12c71889d7766b` through PR #182 is strict-banked by Actions `29548300732`: 1,263 host plus 33 checksum tests, 1,008 wire vectors, 931 oracle checks, existing wire/advert fuzzing, and 100,000 native plus 100,000 Clang 18 semantic-packet cases pass with zero findings. Five artifacts / 46 entries / 341 files total 27,208,527 bytes; canonical receipt SHA-256 is `8da06d90df77a439e37892560272f902243776107365a1676fdd5a49824b74d9`. The truthful WP-05 matrix is 13 implemented / 3 partial / 0 missing semantic requirements across 11 suites / 75 scenarios; fuzz is 4 implemented / 1 partial / 3 missing. PRs #171/#173 bank WP-06 Admin-owner and queue-fairness foundations, but complete ownership remains open. A non-erasing exact COM12 flash passed; retained predecessor DM data now has an explicit data-preserving migration blocker while final storage health remained READY_SD/FAT32/mounted with zero SD/NVS-mirror I/O errors. Official-peer RF, physical acceptance, full WP-05/WP-06, and release closure remain open. Reporting remains 80% capability implementation, 74% weighted release progress, and 0 of 11 final gates green. No tag may be cut until the exact release-commit gate is green.

## Feature Matrix

Status values: Working, Hardware-proven, Partial, Experimental, Not started.

| Area | Status | Notes |
|---|---|---|
| ESP-IDF migration target | Partial | Issue #63 selects version-pinned `espressif/idf:v5.5.4`. Standalone migration commit `39a043c` has the exact Actions-generated lock plus green host, firmware, package, checksum, release, and effective-config checks. The combined candidate still requires green Actions, exact COM12 `version.idf=v5.5.4` and behavioral proof, and refreshed commit-matched release evidence. |
| GitHub Actions firmware package | Working | Default ESP32/UI CI builds the ESP32 firmware and release package without rebuilding RP2040 artifacts. `include_sd_bridge=true` or SD/RP2040 path changes opt into the RP2040 SD bridge UF2 and official Seeed SD smoke UF2 checksums. Firmware compilation, dependency resolution, and packaging remain Actions-only. |
| Touch Home and shell navigation | Partial | Home is a quiet dashboard with Messages, Nodes, Map, and Tools destinations plus compact Mesh/Wi-Fi/BLE/SD/Attention actions. PR #164 strict-banks fail-closed status projection and direct Radio/Wi-Fi/BLE/Storage/Diagnostics routing; modal close returns to the active tab. Non-Home pages use the five-item icon-plus-text dock. UI-task-only LVGL proof, lifecycle/open-close proof, 1,000 transitions, exact-candidate pixel/touch/scroll/focus/keyboard/accessibility qualification, and the required runtime-safety artifact remain open, as do live BLE/RF/SD behavior and downstream feature acceptance. |
| Compose/input keyboard capture | Hardware-proven | PR #35 / issue #2 captured the historical 12-input set on COM12 from `fce5d82` / Actions `28727064923`. The built-in map source removes the provider keyboard, so the active `--targets all` contract is now 11 inputs (`capture_count=11`): Public/DM compose, Public search, Packet search, contact edit, onboarding, map location, and Wi-Fi SSID/password. The prior artifact remains valid evidence for those retained inputs. |
| Public messages | Hardware-proven | Public TX/RX plumbing, retained Public history, search, unread/read state, Packet-tab evidence, and a full-height message detail page with wrapped text plus nested technical details exist. |
| Messages and direct messages | Partial | PR #140 adds strict printable UTF-8 admission across Public/DM receive, retry, retained storage, reboot, console JSON, and compose, with decoded-character plus encoded-byte counts and Send disabled for empty, malformed, control-containing, or over-138-byte text. PR #139 adds the Messages root with Public/Direct destinations and a truthful retained Public conversation; PR #137 adds the owned retained DM thread controller; PR #117/#123 provide runtime-owned delivery and authenticated retained-path/fallback foundations. Curated emoji insertion/glyph coverage, no-route/contact UI eligibility, sender-to-DM identity explanations, bounded search and degraded/failure/retry states, trusted wall-time display, cursor-driven badges, remaining simulator/incoming-event switching, controlled-peer delivery, and exact-candidate RF/UI/physical acceptance remain release blockers. |
| Multi-channel model and messaging | Partial | PR #114 adds a host-tested persistent Public-plus-seven channel model with stable channel/history identities, unread/default/source metadata, fail-closed schema-v2 CRC/lineage/generation, v1 migration, and secret-redacted normal reads. PR #116 adds one atomic redacted metadata snapshot, persisted default selection, and bounded app/USB list/select commands with no RF or SD-format side effects. Selected-channel send/receive/history/unread integration, channel-management UI, official-client interoperability, RF/physical proof, and WP-09 closure remain open. |
| Nodes, contacts, routes | Partial | Heard nodes, contacts, route trace/detail, role browser, and diagnostics exist. PR #165 strict-banks exact, capacity-bounded Chat/Repeater/Room/Sensor/Unknown totals from the same rendered rows; PR #172 banks bounded Node Detail/action truth. PR #175 banks at most eight pins from a bounded 32-marker query only when signed-advert provenance and wall-age truth pass, labels role and unknown accuracy without inferred precision, gates initial/interactive/reacquire tile paths on explicit center provenance, and invalidates retained view/lease state on trust loss or backward-time correction. Remaining Node/network actions, real trace/network tools, max-record live updates, Map-provider qualification, controlled-peer route/TRACE, exact-candidate physical Nodes/Map UI, dependencies, and the required WP-16 artifact remain open. |
| Repeater/room administration | Partial | PR #166 strict-banks bounded production read-only repeater login/status behavior; PR #171 moves bounded Admin login/status/logout under the runtime owner. PR #173 adds bounded priority-lane fairness, terminal precedence, accepted-command preservation, TX admission/RX recovery guards, and saturating queue telemetry. Complete radio/command ownership, watchdog/reboot/power-loss recovery, room administration, mutations, truthful Admin UI, controlled-peer RF, exact-candidate hardware acceptance, WP-06, and WP-18 closure remain open. |
| Packet diagnostics | Working | Packet list, filter/search, raw preview, detail sheet, and retained packet storage paths exist. PR #115 gives filter/search/pause/paging/fallback state one bounded controller with separate PSRAM query scratch and no direct LVGL/storage/RP2040 side effects. Exact-candidate scrolling, navigation, corruption, and physical UI acceptance remain open. |
| Retained storage and reset | Partial | PR #118 gives Public, DM, packet, and route stores one descriptor scheduler with isolated serializers and bounded deadlines. PR #158 banks fail-closed versioned settings, and PR #174 adds a durable exact-inventory factory-reset journal, producer-silent recovery, owned-path-only clearing, and removable-SD lineage fencing without formatting or reset-time SD access. Physical interruption/repeated-power-loss, card swap/lineage durability, NVS write-amplification/endurance, exact-candidate recovery proof, and WP-11 closure remain open. |
| SD core file operations | Hardware-proven | Current COM12/COM16 evidence from `1a29876` / Actions `28714355561` proves official Seeed FAT32 smoke, FAT32 `READY_SD`, raw diagnostics, filecanary, safe boot scenarios for correct/missing/existing data plus RP2040-unavailable fallback, retained history after reboot, reboot/remount, map-tile canary, export canary, diagnostic export, and sampled data export without Public RF or formatting. |
| SD release matrix | Partial | PR #61 proves the read-only Storage/Card status/Data locations hierarchy on predecessor exact Actions-built COM12 pixels, including restored bounded scrolling and a fixed no-format footer. PR #129 strict-banks the current bounded Storage truth model for no-card, ready, reconnect, degraded, FAT32-required, and backend-location states while preserving the no-device-format boundary; this is software/artifact proof, not physical SD/UI closure. Manual touch/photos, physical no-card and unformatted/non-FAT32 scenarios, <=32GB FAT32 multi-card proof, and power/electrical evidence remain open. Users prepare FAT32 cards on a computer. |
| Map and tile cache | Partial | Map uses the built-in OpenStreetMap Standard tile source with no provider editor. The simple setup path is `Map -> Map options -> Set location or Cache status`; connect Wi-Fi, then open the actual Map. PR #131 owns the bounded setup/controller lifecycle. PR #175 adds bounded signed-location pins, verified age/role/unknown-accuracy truth, explicit center provenance, trust-loss invalidation, and backward-time rollback rechecks without background prefetch, RF transmission, or SD formatting. The Map starts at regional zoom 10, supports one-finger pan plus 44x44-or-larger `-`, `+`, and `Center` controls across zooms 8 through 14, and requests at most the visible current-view 3x3 at one zoom per visible generation. Map provider/cache/fetch/render/cancel/revisit qualification, combined-COM12 pixels, live control/cache/heap proof, and physical acceptance remain pending. |
| Truthful time | Partial | PR #120 centralizes monotonic, wall, certificate-validity, and MeshCore protocol clocks. PR #124 binds the protocol allocator floor to the exact source epoch, quarantines excessive SNTP forward jumps, permits authenticated representable companion recovery, preflights protocol time before TX-side effects, and exposes persistence/admission recovery truth. Retained validated-wall recovery, explicit legacy migration, timezone/display conversion, authenticated companion transport acceptance, and exact-device cold-boot/SNTP/jump/reboot/power/TLS proof remain open. |
| Wi-Fi | Partial | Setup UI and bounded serial controls exist and remain disabled by default. PR #121 adds a truthful connectivity view model; PR #126 adds the bounded reconnect policy; PR #128 adds the CRC-protected persisted repeated-crash boot guard. PR #130 extracts the setup sheet into one bounded controller with owned immutable view strings, nine persistent generation-checked bindings, fail-closed malformed-state cleanup, repeat-create safety, and password buffers cleared before refresh, hide, or destruction. The credential physical-flash threat model/NVS-encryption decision and exact-candidate live AP/reboot/wrong-password/weak-signal/safe-mode/full reconnect-stress plus touch/keyboard/credential-memory proof remain open. Standalone `de79c9f` remains predecessor hardware evidence only. |
| BLE, OTA, GPS | Not started | PR #132 owns a truthful BLE setup sheet that keeps pairing/forget unavailable; it does not implement release-grade BLE companion transport. BLE transport, OTA, GPS/location-source integration, and nearby GPS node pins remain pending. |
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
python .\scripts\scroll_probe_d1l.py --dry-run --screens home,public_messages,dm_thread,nodes,packets,settings,storage,storage_card,storage_data,wifi,map,map_options,map_location,map_cache
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
- RP2040 smoke/UF2 maintenance: `COM16`; the production bridge intentionally
  exposes no USB CDC port and is controlled through the ESP32 console on `COM12`.
- Never use `COM8`, `COM11`, or `COM29` as the D1L serial/flash target. `COM11` may be checked separately only as the independent bot/radio endpoint for controlled DM evidence.
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

# Network-suppressed Map proof. Do not add --clear-crashlog-before-start.
python .\scripts\scroll_probe_d1l.py --port $env:D1L_PORT --screens map,map_options,map_location,map_cache --out artifacts\hardware\com12\scroll_probe_map-<sha>-COM12.json
```

Bundled COM12 UI sweep, not the default for every UI issue:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --skip-sd-suite --include-ui-probes
```

Use the bundled SD suite only for an SD/RP2040 slice or final release sweep:

```powershell
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-current --commit <sha> --refresh-rp2040-smoke
```

Every bundled SD run binds the downloaded host-success marker, release manifest,
packaged files, and standalone firmware hashes to the requested full commit and
explicitly supplied numeric Actions run before any flash; the resolved commit
must be a canonical 40-hex SHA. A pre-existing UF2 disk is never selected
automatically; pass `--uf2-volume` to authorize it explicitly. COM16 is the only
RP2040 serial port the runner may mutate during smoke/maintenance; other
discovered ports are read-only inventory. The production bridge intentionally
has no USB CDC port, so absent COM16 is accepted only when COM12 proves the
bridge protocol and its explicit `rp2040 bootloader` path. The raw electrical
diagnostic is an isolated, bounded maintenance phase gated by a fresh clean
`READY_SD` preflight both before entry and after exact bridge/ESP32 recovery.
Any failed diagnostic, later SD stage, or post-SD smoke preserves its receipt,
runs a post-recovery release audit, attempts bounded exact-artifact recovery,
and stops subsequent canaries and UI probes.
`--refresh-rp2040-smoke` additionally captures official RP2040 SD smoke and the
RP2040-unavailable fallback window. `--skip-esp32-flash` is valid only together
with `--skip-sd-suite` for an ESP32/UI-only run.

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
