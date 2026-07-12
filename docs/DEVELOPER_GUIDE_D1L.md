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
python .\scripts\scroll_probe_d1l.py --dry-run --screens home,public_messages,dm_thread,nodes,packets,settings,storage,storage_card,storage_data,wifi,map,map_options,map_location,map_cache
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

The 8 MB layout preserves the default 24 KiB `nvs` partition for settings,
identity, Wi-Fi, contacts, nodes, read state, and crash state. Public, DM,
route, and packet fallback blobs use the separate 124 KiB `d1l_retained`
partition at `0x7E1000`; its 4 KiB metadata sector at `0x7E0000` holds two
versioned marker copies. The dedicated partition itself stores a versioned
`d1l_ret_meta/anchor`, and a final completion claim is stored in default NVS as
the `d1l_ret_meta/initialized` sentinel. The factory app remains at `0x10000`
and ends before the metadata sector. Upgrade reads copy a scoped legacy
retained key from default NVS only after the dedicated write commits, then
erase only that old key; the completion sentinel is committed after all known
legacy-key migration succeeds.

Marker format v2 is the release format. On blank first use, firmware writes
metadata marker 1, initializes NVS, writes and commits the dedicated anchor,
and only then writes metadata marker 2. Scoped legacy migration follows, and
the default-NVS completion sentinel is committed last. The anchor makes a
genuinely initialized user-empty store physically nonblank, while marker 2
proves that the anchor-commit point was crossed. The only blank owned-state
resume is the exact pre-initialization power-loss state with marker 1 valid,
marker 2 erased, and no default sentinel.

`nvs_flash_init_partition` is not a read-only probe. ESP-IDF initialization may
erase or activate a corrupt page, so firmware never uses it to classify a
nonblank region that has neither a valid current/future metadata marker nor a
valid default sentinel. It performs zero retained-region erases, returns
fail-closed status with `external_init_required=true`, and leaves the ambiguous
bytes untouched. The installer or hardware procedure must first verify the
supported predecessor partition/layout provenance, then perform a separately
audited erase scoped strictly to `d1l_retained` at `0x7E1000` for `0x1F000`
bytes. Firmware then verifies blank first use and executes marker 1 -> NVS init
-> anchor commit -> marker 2 -> legacy migration -> sentinel.
Use `scripts/prepare_retained_nvs_upgrade_d1l.py` for the external step. It
requires an exact running SHA from the audited predecessor allowlist, validates
the partition table's MD5 record, exact entries, and exact Actions artifact
hash, then requires the exact erase-scope confirmation. The one known failed
pre-anchor candidate is incident-specific: live failure-shaped status alone is
never authorization. Its committed evidence manifest is hash-pinned in the
tool and binds the exact flash and first-boot receipt hashes, COM port, ESP32-S3
MAC, the previously captured MeshCore identity fingerprint, and the complete pre-erase
`d1l_retained` raw SHA256. The tool rereads and matches all of those facts while
the chip is held in the bootloader, fsyncs staged JSON intent before mutation,
erases only the retained range without rebooting, rereads it before allowing a
hard reset, requires every byte to be `0xFF`, and retains no raw backup.

Marker- or sentinel-owned recovery performs no explicit retained-partition
erase. With only the default sentinel remaining, marker reconstruction proceeds
only after NVS initialization succeeds and the existing dedicated anchor is
verified. If both metadata markers and the default sentinel are lost
simultaneously, including an anchor-only valid NVS, firmware preserves the
region and reports `external_init_required=true`; it does not delete that state
automatically.

Published pre-anchor marker format v1 is an explicit supported upgrade path.
Valid v1 markers prove ownership even for an empty partition, so firmware
initializes it and commits the v2 anchor without erasing retained data, migrates
legacy keys, commits the default sentinel, and only then rewrites both metadata
slots as v2. Release hardware status must show `marker_ready=true`,
`markers_complete=true`, `anchor_ready=true`, `sentinel_ready=true`,
`external_init_required=false`, `ready=true`, and both init and migration errors
as `ESP_OK`. No automatic whole-default-NVS erase is allowed.

`ui_capture_d1l.py` is the hardware display truth path for the split-page UI blocker. It reads the 480x480 RGB565 frame back over the COM12 console, writes JSON/PNG/raw artifacts, and must stay non-destructive: no RF send, no SD format, and no manual touch requirement.

## GitHub Actions

The `d1l-ci` workflow runs host checks on Windows plus ESP32 firmware
build/package generation using the issue #63 selected target,
`espressif/idf:v5.5.4`. This is a version-pinned tag, not a content-immutable
image identity or proof that the SDK is already production-qualified. The default path
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

### Issue #63 SDK qualification

Do not generate or repair `dependencies.lock` by hand or with a local firmware
build. During the migration, let ESP-IDF Component Manager generate the lock in
the version-pinned Actions environment. Archive the exact generated lock and
diff, review and commit that output, then rerun Actions and require the lock to
remain unchanged. Retain the run ID, commit, selected image tag, resolved image
identity when Actions exposes it, lock file, package checksums, and artifact
metadata as one evidence set.

After that clean repeat build passes, flash only its verified artifact to exact
COM12. Run `version` first and require the JSON response to contain
`"idf":"v5.5.4"`, then run the issue #63 board, display/touch, Wi-Fi, RF,
RP2040/SD, Map, health, reboot, and post-power-cycle checks. Refresh the relevant
commit-matched release-gate evidence before calling v5.5.4 the production
baseline. The `supported_sdk_baseline` audit item checks the workflow selection
and committed lock's IDF version; it does not prove lock provenance or replace
these build and hardware stages.

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
