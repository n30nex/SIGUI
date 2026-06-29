# Phase 8 Release Package Checkpoint

Date: 2026-06-29

## Scope

- Added `scripts/package_release_d1l.py`.
- The package contains the normal project flash set, an app update image, an 8MB full flash image, debug ELF/MAP files when present, `manifest.json`, `SHA256SUMS.txt`, and generated flash helpers.
- Generated flash helpers keep the repo rule that D1L flashing requires `D1L_PORT` or an explicit `-Port`.
- Full 8MB flashing is separated from normal project flashing and requires typed confirmation.
- GitHub Actions now uploads a `d1l-release-package` artifact beside host and firmware artifacts.

## Commands

Local package from an existing build:

```powershell
python .\scripts\package_release_d1l.py --build-dir build --out-dir artifacts\release --package-name d1l-release-local-smoke
```

Normal release package flash:

```powershell
$env:D1L_PORT = "COMx"
.\flash_project.ps1 -Port $env:D1L_PORT
```

Factory/recovery full image flash:

```powershell
$env:D1L_PORT = "COMx"
.\flash_full_8mb.ps1 -Port $env:D1L_PORT
```

## Validation

- `python -m pytest tests\test_package_release_d1l.py -q` passed.
- `python -m pytest -q` passed with 74 tests.
- Local package generation from `build/` passed and wrote `artifacts\release\d1l-release-local-smoke`.
- The generated `README_RELEASE.md` points at `firmware/meshcore_deskos_d1l.bin` for the app image.
- The generated package contains `SHA256SUMS.txt`, `manifest.json`, `firmware/`, `update/`, `full-flash/`, and flash helper scripts.
- Hardware smoke sanity on `COM7` passed without reflashing and wrote `artifacts\smoke\d1l-smoke-release-package-sanity-local-COM7.json`.
  - 28 commands passed.
  - `wifi status`, `wifi scan`, and `ble status` reported the documented release companion-radio state.
  - `crashlog` and `health` remained machine-readable; `health` reported `board_ready=true`, `ui_ready=true`, `current_task_stack_free_words=1120`, `ui_task_stack_free_words=1352`, and `lvgl_used_pct=62`.

## Still Pending

- Final release naming/versioning.
- Screenshots or hardware photos.
- Final full-duration 12-hour idle soak and 1-hour active messaging soak.
- Manual physical touch review.
