# Phase 3 UI Simulator Checkpoint

Date: 2026-06-29

## Scope

- Added `tools/ui_simulator.py` as a deterministic host-side UI check for the D1L 480x480 surface.
- The simulator renders PNGs for Home, Messages, Nodes, Packets, Settings, compose/contact/DM/route/packet-detail/packet-search/mesh-role sheets, lock overlay, and first-boot onboarding.
- The simulator writes `ui-sim-report.json` with display size, labels, truncation details, required-label misses, and measured text overflow results.
- GitHub Actions now uploads simulator screenshots and the report in `d1l-host-artifacts`.

## Command

```powershell
python .\tools\ui_simulator.py --out artifacts\ui-sim
```

## Validation Boundary

This is a host regression check for layout intent and readable labels. It does not replace physical LCD review, touch input confirmation, LVGL object lifetime checks, or animation/performance review on the D1L.
