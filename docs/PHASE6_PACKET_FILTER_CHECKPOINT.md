# Phase 6 Packet Filter / Raw Hex Checkpoint

Date: 2026-06-29

## Scope

- Added bounded raw packet hex previews to the NVS-backed packet log.
- Added serial diagnostics:
  - `packets filter <any|rx|tx> <any|text|kind>`
  - `packets search <text>`
  - `packets raw <seq>`
- Added Packet-tab filter chips for All/RX/TX/Text.
- Added a touch Packet Search sheet with on-screen keyboard entry.
- Added raw hex preview to the Packet Detail sheet.
- Added simulator coverage for the Packet Search sheet and raw hex detail.

## Commands

```powershell
python .\tools\ui_simulator.py --out artifacts\ui-sim
python -m pytest tests\test_packet_log_contract.py tests\test_ui_shell_contract.py tests\test_ui_simulator.py tests\test_smoke_d1l_parser.py -q
```

## Validation Boundary

This checkpoint proves the host contracts, UI simulator shape, and flashed COM7 hardware behavior for packet filtering/search/raw hex. Hardware artifacts:

- `artifacts/smoke/d1l-smoke-packet-filter-rawhex-local-COM7.json`
- `artifacts/smoke/d1l-packet-filter-rawhex-public-test-local-COM7.json`
- `artifacts/soak/d1l-soak-packet-filter-rawhex-active-local-COM7.json`

Manual physical touch review of the Packet-tab filter/search controls is still part of the broader pending UI review.
