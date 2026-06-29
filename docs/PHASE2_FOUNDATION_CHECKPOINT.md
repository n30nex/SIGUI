# Phase 2 Foundation Checkpoint

Date: 2026-06-29

## Completed

- Added NVS-backed settings model for node name, desk companion role, default-off Wi-Fi/BLE/observer policy, contrast flags, path hash size, and Canada/USA radio settings.
- Validated settings persistence on real D1L flash by setting a temporary node name/path-hash value, rebooting, verifying both survived NVS, then resetting back to defaults.
- Added `settings get`, `settings reset`, `settings set name <name>`, and `settings set pathhash <1|2|3>` serial commands.
- Added `identity status` command that reports the persisted node label and honestly marks the MeshCore cryptographic local identity as pending the Phase 2 C++ MeshCore binding.
- Added a `meshcore_service` foundation with state, counters, path-hash mode, companion framing readiness, and explicit not-ready responses for advert/message commands until RF integration is complete.
- Implemented serial handlers for `radio set freq`, `radio set bw`, `radio set sf`, and `radio set cr`.
- Added `mesh advert flood` handling alongside `mesh advert zero`.
- Added host contract tests for the settings and command surface.

## Commands Run

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
```

## Results

- Host tests: 22 passed.
- Smoke dry run: passed and now includes `settings get` and `identity status`.
- Hardware persistence smoke: passed on `COM7`; evidence is archived at `artifacts/smoke/d1l-smoke-persistence-COM7.json`.

## Not Yet Validated

- MeshCore C++ `LocalIdentity` generation/storage.
- MeshCore advert TX/RX or public message TX/RX.
- Applying saved radio settings to the SX1262 runtime.
