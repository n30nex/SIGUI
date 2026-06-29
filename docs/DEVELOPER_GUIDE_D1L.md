# MeshCore DeskOS D1L Developer Guide

## Repo Shape

- `main/` contains the ESP-IDF firmware app.
- `main/ui/` contains the LVGL touch shell.
- `main/mesh/` contains MeshCore service/store helpers.
- `main/comms/` contains USB console and connectivity status plumbing.
- `main/diagnostics/` contains health and crash/reset telemetry.
- `scripts/` contains build, flash, smoke, soak, backup, checksum, and release-package tooling.
- `tests/` contains host contract tests.
- `docs/` contains roadmap, validation notes, checklists, and phase checkpoints.

## Host Checks

```powershell
python -m pytest -q
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1
```

## Firmware Build

Preferred local container build:

```powershell
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py build"
```

Native ESP-IDF shell build:

```powershell
.\scripts\build_d1l.ps1 -RequireFirmware
```

## Release Package

After `build/` exists:

```powershell
python .\scripts\package_release_d1l.py --build-dir build --out-dir artifacts\release
```

The package includes:

- `firmware/bootloader.bin`
- `firmware/partition-table.bin`
- `firmware/meshcore_deskos_d1l.bin`
- `firmware/flasher_args.json`
- `update/meshcore_deskos_d1l-app.bin`
- `full-flash/meshcore_deskos_d1l-full-8mb.bin`
- `manifest.json`
- `SHA256SUMS.txt`
- `flash_project.ps1`
- `flash_project.sh`
- `flash_full_8mb.ps1`

## Hardware Validation

Use only the supplied D1L port:

```powershell
$env:D1L_PORT = "COMx"
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
```

For current local validation, COM7 has been the D1L and COM11 has been a local MeshCore bot. Do not bake those ports into scripts or defaults.

## GitHub Actions

The `d1l-ci` workflow runs host checks on Windows and firmware build/package generation in `espressif/idf:release-v5.1`. Expected artifacts:

- `d1l-host-artifacts`
- `d1l-firmware-artifacts`
- `d1l-release-package`

`d1l-host-artifacts` includes `ui-sim/` screenshots and `ui-sim-report.json`, including the first-boot onboarding surface.

Download with:

```powershell
gh run download <run-id> -D artifacts\github\<run-id>
```

## Release Rules

- Keep flash commands explicit-port only.
- Keep Wi-Fi/BLE optional and documented when runtime support is disabled.
- Keep full-flash flows behind typed confirmation.
- Update `docs/HARDWARE_VALIDATION_D1L_2026-06-29.md`, `docs/KNOWN_LIMITATIONS.md`, and `docs/RELEASE_CHECKLIST.md` when hardware evidence changes.
- Do not mark the roadmap complete until manual UI review, full DM proof, long soaks, and final release docs/tests are actually complete.
