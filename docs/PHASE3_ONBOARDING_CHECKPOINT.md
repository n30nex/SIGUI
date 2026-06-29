# Phase 3 Onboarding Checkpoint

Date: 2026-06-29

## Scope

- Added settings schema v3 with persisted `onboarding_complete`.
- Added schema v2 migration that preserves node name, radio settings, companion settings, and MeshCore identity, then marks existing upgraded devices as already onboarded.
- Added first-boot touch onboarding sheet with node-name entry, Canada/USA preset confirmation, Desk Companion role, offline Wi-Fi/BLE/observer defaults, and `Start` / `Use Defaults` actions.
- Added serial diagnostics: `settings onboarding status`, `settings onboarding complete <name>`, and `settings onboarding reset`.
- Added simulator coverage for the onboarding surface.

## Commands

```powershell
python .\tools\ui_simulator.py --out artifacts\ui-sim
python -m pytest -q
```

Hardware smoke includes `settings onboarding status` after this checkpoint.

## Validation Boundary

This completes the first persisted onboarding flow. Richer optional-radio choices are still limited by the current Wi-Fi/BLE runtime status, and physical touch review of the onboarding keyboard remains part of the broader manual UI review.
