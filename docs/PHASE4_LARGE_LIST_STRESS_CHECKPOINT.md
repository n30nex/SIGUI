# Phase 4 Large List Stress Checkpoint

Date: 2026-06-30

## Scope

- Added a `large-mesh` scenario to `tools/ui_simulator.py`.
- The stress snapshot feeds 96 heard nodes, 18 contacts, 48 Public messages, 32 DM messages, 40 packet rows, and 24 route rows.
- The simulator report records source counts and rendered-row counts so oversized stores can be distinguished from visible previews.
- Messages and Nodes simulator views now stop rendering rows before they exceed the fixed 480x480 layout.
- GitHub Actions now generates both `artifacts/ui-sim` and `artifacts/ui-sim-large`.

## Validation

```powershell
python -m pytest -q
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start
python .\scripts\smoke_d1l.py --port COM7 --out artifacts\smoke\d1l-smoke-large-list-sim-local-COM7.json
```

Result:

- `84 passed`
- The default simulator returned `ok=true`.
- `ui-sim-large/ui-sim-report.json` returned `ok=true`.
- Large-mesh source counts were `heard=96`, `contacts=18`, `public_messages=48`, and `dm_messages=32`.
- Rendered previews stayed bounded: Public rows 3, DM rows 2, contact rows 2, and heard-node rows 3.
- No measured text overflow was reported.
- Smoke and soak dry-runs returned `ok=true`.
- COM7 hardware smoke returned `ok=true` with board/UI/radio ready and crashlog count 0. No flash was needed for this simulator/docs/test-only chunk.

## Still Pending

- Live RF stress with a genuinely large hardware mesh.
- Manual physical scroll/touch review remains separate from host screenshot validation.
