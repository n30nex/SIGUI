# Fast D1L Release Workflow

Use this loop until the release gate is green. The rule is simple: one issue, one
small PR, one targeted hardware proof. Do not rerun RP2040/SD work unless the
issue changes SD/RP2040 code or SD evidence is the selected blocker.

## Default Cycle

1. Start from current `main`.
2. Pick one open P0 issue from GitHub.
3. Make the smallest code/docs/test change that can close that issue.
4. Run focused host tests first.
5. Run `python -m pytest tests -q` only after the focused set passes.
6. Push and use the default `d1l-ci` path. It should skip RP2040/SD work unless
   SD/RP2040 files changed.
7. Download the ESP32 Actions artifacts.
8. Run one targeted COM12 hardware validator for the issue.
9. Update the issue and PR with artifact paths.
10. Merge the PR if checks and targeted hardware proof pass.

## Validation Tiers

### ESP32/UI Issue

Use this for UI, compose, docs, simulator, serial command, and ESP32 app fixes.

```powershell
python -m pytest tests\test_ci_workflow_contract.py tests\test_release_gate_audit_d1l.py -q
python -m pytest tests -q
gh workflow run d1l-ci.yml --ref <branch> -f include_sd_bridge=false
gh run watch <run-id> --exit-status
gh run download <run-id> --dir artifacts\github\<run-id>-<sha>
python .\scripts\verify_checksums.py artifacts\github\<run-id>-<sha>\d1l-firmware-artifacts
python .\scripts\verify_checksums.py artifacts\github\<run-id>-<sha>\d1l-release-package
python .\scripts\autonomous_hardware_validate_d1l.py --github-run-id <run-id> --github-run-dir artifacts\github\<run-id>-<sha> --commit <sha> --skip-sd-suite --include-ui-probes --out artifacts\hardware\d1l-autonomous-hardware-validation-<sha>-esp32-ui-only.json
```

Expected proof:

- `rp2040_uf2_flash=false`
- `sd_suite_enabled=false`
- no COM16/RP2040 flash/test path
- release-gate artifact exists, even if public release remains fail-closed

### SD/RP2040 Issue

Use only when SD/RP2040 code or SD physical evidence is the selected issue.

```powershell
gh workflow run d1l-ci.yml --ref <branch> -f include_sd_bridge=true
```

Then run the SD-specific validator or guided SD workflow named by the issue.
Keep `formats_sd=false`; users prepare FAT32 cards on a computer.

### RF/DM Issue

Use a targeted RF/DM proof and keep ports explicit. Do not mix this with SD or
UI refactors in the same PR.

## Local Progress Dashboard

Run this in a separate terminal while Codex works:

```powershell
python .\scripts\release_progress_dashboard.py --open
```

The dashboard is read-only. It reads the newest `artifacts\release-gate` and
`artifacts\hardware\d1l-autonomous-hardware-validation-*.json` files, then shows
overall progress, P0 progress, category progress, and the open P0 evidence
gates. It does not open serial ports, flash firmware, run Actions, or call
GitHub.

JSON snapshot:

```powershell
python .\scripts\release_progress_dashboard.py --once-json
```

## Stop Conditions

Stop the cycle only when one of these is true:

- The selected issue is merged and the issue is closed.
- Hardware evidence proves the fix is wrong and a new issue/comment is created.
- The device/port route is unsafe or unavailable.

Do not stop because broad release remains blocked after a narrow issue is
closed. Move to the next P0.
