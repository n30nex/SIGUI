# MeshCore DeskOS D1L Developer Guide

## Repo Shape

- `main/` contains the ESP-IDF firmware app.
- `main/ui/` contains the LVGL touch shell.
- `main/mesh/` contains MeshCore service/store helpers.
- `main/comms/` contains USB console and connectivity status plumbing.
- `main/diagnostics/` contains health and crash/reset telemetry.
- `scripts/` contains host-check, flash, smoke, soak, backup, checksum, and release-package tooling.
- `firmware/rp2040_sd_bridge/` contains the Arduino RP2040 SD bridge target. It is compiled by GitHub Actions.
- `tests/` contains host contract tests.
- `docs/` contains the active README/index, user/developer guides, release checklist, known limitations, test plan, SD runbooks, protocol docs, screenshots, and attribution notes.

## Host Checks

```powershell
python -m pytest -q
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large
python .\tools\ui_simulator.py --scenario storage-states --out artifacts\ui-sim-storage
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\ui_corruption_probe_d1l.py --dry-run --rounds 20
python .\scripts\ui_capture_d1l.py --dry-run
python .\scripts\scroll_probe_d1l.py --dry-run --screens home,public_messages,dm_thread,nodes,packets,settings,storage,wifi,map
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test
python .\scripts\sd_boot_prepare_acceptance_d1l.py --dry-run --scenario all
```

## Firmware Build

Do not build firmware on the Windows host. Use GitHub Actions for ESP32
binaries. RP2040 SD bridge binaries are opt-in for bridge/SD work only:

```powershell
gh workflow run d1l-ci.yml --ref <branch-or-main> -f include_sd_bridge=false
gh run watch
gh run download <run-id> -D artifacts\github\<run-id>
```

For a bridge/SD release artifact refresh, run the same workflow with
`-f include_sd_bridge=true`.

The local `scripts/build_d1l.ps1` path is host-only and rejects `-RequireFirmware`.

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
python .\scripts\ui_capture_d1l.py --port $env:D1L_PORT --out artifacts\hardware\com12\ui_pixel_capture-COM12.json
```

For current local validation, `COM12` is the D1L ESP32/UI console and `COM16` is the D1L RP2040 SD bridge/UF2 side. Do not touch `COM8`, `COM11`, or `COM29` for D1L validation.

The other local MeshCore bot may be used as the controlled DM RF peer for production validation. Keep the bot port explicit, and prefer the targeted DM probe when Public-channel RF should stay quiet.

Do not format SD cards from DeskOS firmware, RP2040 firmware, serial commands, UI, or scripts. Production validation assumes users provide FAT32 cards prepared on a computer; the current validation device has a fresh FAT32 32 GB card installed. DeskOS may create the `/deskos` folders/manifests on a mounted FAT32 card and otherwise falls back to NVS.

`ui_capture_d1l.py` is the hardware display truth path for the split-page UI blocker. It reads the 480x480 RGB565 frame back over the COM12 console, writes JSON/PNG/raw artifacts, and must stay non-destructive: no RF send, no SD format, and no manual touch requirement.

## GitHub Actions

The `d1l-ci` workflow runs host checks on Windows plus ESP32 firmware
build/package generation in `espressif/idf:release-v5.1`. The default path
skips SD/RP2040 dry-runs and RP2040 Arduino builds so ESP32/UI fixes do not
rebuild or revalidate the already-working bridge. Expected default artifacts:

- `d1l-host-artifacts`
- `d1l-firmware-artifacts`
- `d1l-release-package`

When `include_sd_bridge=true` is selected, or SD/RP2040 paths changed, the
workflow also emits:

- `rp2040-sd-bridge-firmware`
- `rp2040-sd-smoke-firmware`
- `rp2040-seeed-official-sd-smoke-firmware`

`d1l-host-artifacts` includes `ui-sim/` screenshots and `ui-sim-report.json`, including the first-boot onboarding surface.

Download with:

```powershell
gh run download <run-id> -D artifacts\github\<run-id>
```

RP2040 SD bridge UF2 flashing is not an ESP32 `esptool` path. After putting the
D1L RP2040 into UF2/BOOTSEL mass-storage mode, use the guarded helper so the
artifact checksum and target UF2 metadata are verified before any copy:

```powershell
python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --list-volumes
python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --volume <RP2040_UF2_DRIVE>:
python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --volume <RP2040_UF2_DRIVE>: --copy
```

## Release Rules

- Keep flash commands explicit-port only.
- Keep Wi-Fi/BLE optional and documented when runtime support is disabled.
- Keep full-flash flows behind typed confirmation.
- Update `README.md`, `docs/ROADMAP.md`, `docs/KNOWN_LIMITATIONS.md`, and `docs/RELEASE_CHECKLIST.md` when hardware evidence changes.
- Do not mark the roadmap complete until manual UI review, full DM proof, long soaks, and final release docs/tests are actually complete.
