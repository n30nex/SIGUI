# MeshCore DeskOS D1L Roadmap

This is the active roadmap. Historical phase checkpoints and Codex handoff docs were removed from the release-facing docs so current status lives in one place.

Release status: `scripts/release_gate_audit_d1l.py` reports `ready_for_public_release=false` until all P0 evidence gates pass on the release commit. No release tag should be cut until the audit is green.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| UI split-page redraw corruption | Firmware now defers content rebuilds out of LVGL event callbacks and queues live-data refreshes on the UI task. | Run `ui_corruption_probe_d1l.py` on COM12 from the current Actions artifact. |
| On-screen keyboard and sheets | Functional but too small/cut off in some flows. | Redesign compact keyboard/sheets, rerun simulator and physical review. |
| Full RF/DM acceptance | Public and outbound DM foundations exist; full inbound DM, ACK/PATH, direct-route proof remains open. | Produce `rf_full_acceptance_*.json` with health and no-Public-command proof. |
| SD release matrix | Core SD file/history/remount/map canaries pass on the current FAT32 card. | Add official Seeed smoke, boot/retry proof, non-FAT32 behavior, <=32GB FAT32 matrix, no-format evidence, and power/electrical evidence. |
| Physical screenshots/review | Host simulator screenshots are committed under `docs/screenshots`. | Add physical device photos and manual UI review artifact. |
| Long soak | Short evidence exists. | Run 12-hour idle/listening soak on the release artifact. |

## Feature Direction

- Keep MeshCore-first behavior. Public messages, DMs, routes, packet diagnostics, and retained history are the core release value.
- Keep Wi-Fi, BLE, GPS, OTA, and live map tile browsing honest as experimental or pending until hardware-proven.
- Keep SD FAT32-only. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. Non-FAT32 media must report guidance and retain NVS fallback.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Keep hardware validation autonomous where possible: COM12 for ESP32 app/console, COM16 for RP2040 USB/CDC/UF2. Do not use COM8, COM11, or COM29 for D1L validation.

## Active Work Queue

1. Finish targeted UI corruption fix and hardware proof.
2. Rework keyboard/sheet sizing and rerun simulator screenshots.
3. Complete README/current-doc polish and physical screenshots.
4. Finish RF/DM acceptance without reserved local ports.
5. Complete the SD release matrix now that core SD works.
6. Run final soak and release gate.

## Validation Notes

- `tools/ui_simulator.py` produces deterministic 480x480 screenshots and schema checks.
- The simulator must keep `large-mesh` coverage for oversized node/message stores.
- Serial diagnostics include `routes`, `routes detail <seq>`, `routes trace <fingerprint>`, `routes probe <fingerprint>`, and `routes clear`.
- `ui_corruption_probe_d1l.py` replaces the old high-count tab stress gate. It exercises tabs, serial-only UI data refresh through `ui data-canary`, Packet search, Public search, health, and crashlog checks with `public_rf_tx=false` and `formats_sd=false`.

## Active Docs

Use [docs/README.md](README.md) as the documentation index. Update [README.md](../README.md), [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md), [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md), and [TEST_PLAN_D1L.md](TEST_PLAN_D1L.md) whenever evidence changes.
