# MeshCore DeskOS D1L

MeshCore DeskOS D1L is a Seeed SenseCAP Indicator D1L firmware project for a touch-first MeshCore desk console. It targets the ESP32-S3 + RP2040 D1L with the 480x480 RGB touch display and SX1262 LoRa radio.

Current status: staged D1L hardware validation through the Phase 7 diagnostics and soak-readiness slices. The project has an ESP-IDF v5.1.x firmware skeleton, D1L pin/profile contracts, NVS-backed settings/identity/contact/read-state storage, MeshCore Public group text TX/RX over the D1L SX1262, first DM TX/ACK/PATH plumbing, a 480x480 touch shell, mesh visibility diagnostics, strict serial smoke/soak commands, no-port host tests, and flashing/recovery scripts that require an explicit `D1L_PORT` or `--port`. Optional SD-card work now includes a CI-buildable RP2040 SD bridge target with status/format and generic bounded file-operation protocol support. Retained Public/DM message history, route history, and packet history can switch to SD through the retained blob-store abstraction when the RP2040 reports a ready card, file operations, and atomic rename; NVS remains mirrored as fallback. Map tiles, exports, settings, identity, contacts, read-state, and diagnostics remain onboard-backed or pending until later migrations are implemented and hardware-proven.

The companion compatibility contract is documented in [docs/COMPANION_3BYTE_COMPATIBILITY.md](docs/COMPANION_3BYTE_COMPATIBILITY.md). Phase 1 includes the MeshCore 3-byte transport codec and status command; live binary companion bridging is scheduled after D1L board/radio bring-up.

## Host Checks

No hardware required. These commands do not build firmware:

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\sd_file_canary_d1l.py --dry-run
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --sample-storage --sd-file-canary --allow-sd-unavailable
```

Firmware binaries are built by GitHub Actions only. The `d1l-ci` workflow produces ESP32 firmware/release artifacts and the RP2040 SD bridge artifact.

Hardware flow, once the user supplies the D1L port:

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
python .\scripts\sd_file_canary_d1l.py --port $env:D1L_PORT --allow-unavailable
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 300 --sample-interval-sec 60 --sample-storage --sd-file-canary --allow-sd-unavailable
```

Current bench D1L validation uses COM12. Do not use COM11 or COM29 for D1L flashing/testing.

## Roadmap

The implementation follows [docs/ROADMAP.md](docs/ROADMAP.md). The framework decision is recorded in [docs/D1L_BUILD_DECISION.md](docs/D1L_BUILD_DECISION.md).

For handoff docs, see [docs/USER_GUIDE_D1L.md](docs/USER_GUIDE_D1L.md) and [docs/DEVELOPER_GUIDE_D1L.md](docs/DEVELOPER_GUIDE_D1L.md).
