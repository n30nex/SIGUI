# MeshCore DeskOS D1L

MeshCore DeskOS D1L is a Seeed SenseCAP Indicator D1L firmware project for a touch-first MeshCore desk console. It targets the ESP32-S3 + RP2040 D1L with the 480x480 RGB touch display and SX1262 LoRa radio.

Current status: Phase 1 bring-up scaffold plus Phase 2 foundation. The project has an ESP-IDF v5.1.x firmware skeleton, D1L pin/profile contracts, NVS settings scaffolding, MeshCore service status plumbing, strict serial smoke commands, no-port host tests, and flashing/recovery scripts that require an explicit `D1L_PORT` or `--port`.

The companion compatibility contract is documented in [docs/COMPANION_3BYTE_COMPATIBILITY.md](docs/COMPANION_3BYTE_COMPATIBILITY.md). Phase 1 includes the MeshCore 3-byte transport codec and status command; live binary companion bridging is scheduled after D1L board/radio bring-up.

## Build

No hardware required:

```powershell
.\scripts\build_d1l.ps1
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
```

Hardware flow, once the user supplies the D1L port:

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
```

Do not use COM11 or COM29 for D1L flashing/testing.

## Roadmap

The implementation follows [docs/ROADMAP.md](docs/ROADMAP.md). The framework decision is recorded in [docs/D1L_BUILD_DECISION.md](docs/D1L_BUILD_DECISION.md).
