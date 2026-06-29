# Phase 1 Checkpoint

Date: 2026-06-29

## Completed

- Initialized `feature/meshcore-deskos-d1l`.
- Added Seeed SenseCAP Indicator ESP32 SDK and MeshCore as submodules.
- Chose ESP-IDF v5.1.x as the D1L framework.
- Added D1L pin map and Canada/USA MeshCore radio profile.
- Added Phase 1 firmware scaffold:
  - boot banner
  - board init wrapper
  - I2C scan
  - display color bars
  - touch sample
  - button status probe
  - LEDC backlight
  - RP2040 UART bridge status probe
  - SX1262 hardware status probe with TCXO `NONE`
  - in-memory packet ring and `packets` command
  - JSONL serial command shell
  - MeshCore 3-byte companion transport codec and `companion status`
  - health snapshot
- Added build/flash/monitor/backup/smoke scripts with no hardcoded COM port.
- Added CI host checks and host tests.

## Commands Run

```powershell
git init -b feature/meshcore-deskos-d1l
git submodule add https://github.com/Seeed-Solution/sensecap_indicator_esp32.git third_party/sensecap_indicator_esp32
git submodule add https://github.com/meshcore-dev/MeshCore.git third_party/MeshCore
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
powershell -ExecutionPolicy Bypass -File .\scripts\build_d1l.ps1
python .\scripts\verify_checksums.py artifacts
python -c "import yaml, pathlib; yaml.safe_load(pathlib.Path('.github/workflows/d1l-ci.yml').read_text())"
```

## Results

- Host tests: 17 passed.
- Smoke dry run: passed and includes `button`, `rp2040 status`, `packets`, and `companion status`.
- Build wrapper: passed host-only path and wrote `artifacts/build/build-manifest.json`.
- Firmware build: skipped because `idf.py` was not found on PATH.
- Checksum verifier: passed with no checksum files present because no firmware binary was built locally.
- GitHub Actions workflow YAML: parsed successfully.
- GitHub Actions firmware-build job: configured to run in `espressif/idf:release-v5.1`, but not yet proven by a pushed Actions run.

## Not Yet Validated

- Real D1L display/touch/radio/button/RP2040 hardware.
- Flashing to COM7/COM8/COM9.
- MeshCore RF RX/TX with a second node.

See [PHASE2_FOUNDATION_CHECKPOINT.md](PHASE2_FOUNDATION_CHECKPOINT.md) for the host-verifiable Phase 2 settings/service foundation added after this Phase 1 scaffold.
