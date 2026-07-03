# MeshCore DeskOS D1L

MeshCore DeskOS D1L is a Seeed SenseCAP Indicator D1L firmware project for a touch-first MeshCore desk console. It targets the ESP32-S3 + RP2040 D1L with the 480x480 RGB touch display and SX1262 LoRa radio.

Current status: staged D1L hardware validation through the Phase 7 diagnostics and soak-readiness slices. The project has an ESP-IDF v5.1.x firmware skeleton, D1L pin/profile contracts, NVS-backed settings/identity/contact/read-state storage, MeshCore Public group text TX/RX over the D1L SX1262, first DM TX/ACK/PATH plumbing, a 480x480 touch shell, mesh visibility diagnostics, heard-node role badges/detail inspection, strict serial smoke/soak commands, no-port host tests, and flashing/recovery scripts that require an explicit `D1L_PORT` or `--port`. Optional SD-card work now includes a CI-buildable RP2040 SD bridge target with status and generic bounded file-operation protocol support; users prepare FAT32 SD cards on a computer and there is no device-side SD formatting path. Retained Public/DM message history, route history, and packet history can switch to SD through the retained blob-store abstraction when the RP2040 reports a ready FAT32 card, file operations, and atomic rename; NVS remains mirrored as fallback. Serial-only `storage export-canary <token>`, `storage export-diagnostics <token>`, `storage export-data <token>`, `storage map-tile-canary <token>`, and `storage map-tile-check <token>` commands can prove diagnostic export, sampled user-data export, map-tile cache temp-write/read/atomic-rename/final-read, and read-only map-tile remount paths on SD without Public RF or formatting. SD remains release-blocked until official Seeed smoke, filecanary, retained-canary, reboot/remount, and <=32GB FAT32 matrix evidence pass on the release commit. The sampled data export writes `exports/data/data-export-<token>.json`, includes recent messages/DMs/routes/packets/contacts/nodes/read-state/manual-map-center summary, and excludes private identity material. A first offline Map page plus `storage map-policy`, touch `Set Pin`/`Move Pin`, and `map center` now expose the SD tile-cache path contract, readiness, optional manual center, and disabled download state; live network tile downloads and GPS/location-source integration remain pending. Settings, identity, contacts, and read-state remain onboard-backed or pending until later migrations are implemented and hardware-proven.

The companion compatibility contract is documented in [docs/COMPANION_3BYTE_COMPATIBILITY.md](docs/COMPANION_3BYTE_COMPATIBILITY.md). Phase 1 includes the MeshCore 3-byte transport codec and status command; live binary companion bridging is scheduled after D1L board/radio bring-up.

## Host Checks

No hardware required. These commands do not build firmware:

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\sd_file_canary_d1l.py --dry-run
python .\scripts\sd_retained_history_acceptance_d1l.py --dry-run --token dryrun
python .\scripts\sd_map_tile_canary_d1l.py --dry-run --token dryrun
python .\scripts\sd_reboot_remount_acceptance_d1l.py --dry-run --token dryrun
python .\scripts\sd_data_export_d1l.py --dry-run --token dryrun
python .\scripts\rp2040_sd_bridge_preflight_d1l.py --dry-run
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --sample-storage --sd-file-canary --allow-sd-unavailable
```

Firmware binaries are built by GitHub Actions only. The `d1l-ci` workflow produces ESP32 firmware/release artifacts, `rp2040-sd-bridge-firmware`, and `rp2040-seeed-official-sd-smoke-firmware` for SD hardware proof.

Hardware flow, once the user supplies the D1L port:

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
python .\scripts\rp2040_sd_bridge_preflight_d1l.py --port $env:D1L_PORT --artifact-dir .\artifacts\github\<run-id>\rp2040-sd-bridge-firmware
python .\scripts\sd_file_canary_d1l.py --port $env:D1L_PORT --allow-unavailable
python .\scripts\sd_retained_history_acceptance_d1l.py --port $env:D1L_PORT --allow-unavailable --token prebridge
python .\scripts\sd_map_tile_canary_d1l.py --port $env:D1L_PORT --allow-unavailable --token prebridge
python .\scripts\sd_data_export_d1l.py --port $env:D1L_PORT --allow-unavailable --token prebridge
python .\scripts\sd_reboot_remount_acceptance_d1l.py --port $env:D1L_PORT --token prod
python .\scripts\manual_ui_review_d1l.py --port $env:D1L_PORT --photo-dir .\artifacts\hardware\com12\photos --confirm-display-stable --confirm-touch-accurate --confirm-bottom-tabs --confirm-messages-public --confirm-dm-workflow --confirm-nodes-contacts-routes --confirm-map-storage --confirm-settings-sheets --confirm-photos-current
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 300 --sample-interval-sec 60 --sample-storage --sd-file-canary --allow-sd-unavailable
```

Current bench D1L validation uses COM12. Do not use COM11 or COM29 for D1L flashing/testing.

## Licensing and Attribution

MeshCore DeskOS D1L is released under GPL-3.0-or-later; see [LICENSE](LICENSE). This firmware uses Seeed SenseCAP Indicator BSP/examples and design references from SigurdOS-TDeck with permission. See [docs/ATTRIBUTIONS.md](docs/ATTRIBUTIONS.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and [docs/SOURCE_AUDIT_AND_ATTRIBUTION.md](docs/SOURCE_AUDIT_AND_ATTRIBUTION.md) before public release packaging or redistributing source/binaries.

## Roadmap

The implementation follows [docs/ROADMAP.md](docs/ROADMAP.md). The framework decision is recorded in [docs/D1L_BUILD_DECISION.md](docs/D1L_BUILD_DECISION.md).

For handoff docs, see [docs/USER_GUIDE_D1L.md](docs/USER_GUIDE_D1L.md) and [docs/DEVELOPER_GUIDE_D1L.md](docs/DEVELOPER_GUIDE_D1L.md).
