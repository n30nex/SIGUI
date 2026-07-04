# MeshCore DeskOS D1L Roadmap

This is the active roadmap. Historical phase checkpoints and Codex handoff docs were removed from the release-facing docs so current status lives in one place.

Release status: `scripts/release_gate_audit_d1l.py` reports `ready_for_public_release=false` until all P0 evidence gates pass on the release commit. No release tag should be cut until the audit is green.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| UI split-page redraw corruption | Firmware now defers content rebuilds out of LVGL event callbacks and queues live-data refreshes on the UI task. | Run `ui_corruption_probe_d1l.py` on COM12 from the current Actions artifact. |
| Hardware pixel capture | Firmware exposes a serial RGB565 capture path so the PC can reconstruct the actual 480x480 UI frame. | Run `ui_capture_d1l.py` on COM12 and compare the PNG to the simulator/reference view. |
| On-screen keyboard and sheets | Compose now uses a compact D1L keyboard map and has serial-only Public/DM compose probes. Other keyboard sheets still need the same capture-driven pass. | Run `ui_compose_keyboard_capture_d1l.py` on COM12 from the current Actions artifact and review the four PNG/RGB565 captures. |
| SiguredOS-style icon Home | GitHub issue #27 tracks replacing the dense/status-card Home view with a 480x480 icon-first launcher while preserving Mesh, Wi-Fi, BLE, SD, and power status. | Implement #27, then capture simulator and COM12 pixel-readback evidence for the new Home screen. |
| Full RF/DM acceptance | Public and outbound DM foundations exist; full inbound DM, ACK/PATH, direct-route proof remains open. | Produce `rf_full_acceptance_*.json` with health and no-Public-command proof. |
| SD release matrix | Core SD file/history/remount/map canaries and the official Seeed FAT32 smoke pass on the current FAT32 card from `708deea` / Actions `28712724435`. | Add physical no-card/unformatted/non-FAT32 proof, <=32GB FAT32 matrix, no-format language proof for unusable media, and power/electrical evidence. |
| Physical screenshots/review | Host simulator screenshots are committed under `docs/screenshots`. | Add physical device photos and manual UI review artifact. |
| Long soak | Short evidence exists. | Run 12-hour idle/listening soak on the release artifact. |

## Feature Direction

- Keep MeshCore-first behavior. Public messages, DMs, routes, packet diagnostics, and retained history are the core release value.
- Keep Wi-Fi, BLE, GPS, OTA, and live map tile browsing honest as experimental or pending until hardware-proven.
- Keep SD FAT32-only. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. Non-FAT32 media must report guidance and retain NVS fallback.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Keep hardware validation autonomous where possible: COM12 for ESP32 app/console, COM16 for RP2040 USB/CDC/UF2. Do not use COM8, COM11, or COM29 for D1L validation.

## Active Work Queue

1. Finish targeted UI corruption fix and hardware pixel proof.
2. Prove compose keyboard sizing on hardware, then repeat the capture-driven pass for remaining keyboard sheets.
3. Build the SiguredOS-style icon Home tracked by #27 and capture matching simulator/COM12 evidence.
4. Complete README/current-doc polish and physical screenshots.
5. Finish RF/DM acceptance without reserved local ports.
6. Complete the remaining physical SD release matrix now that core SD works.
7. Run final soak and release gate.

## Validation Notes

- `tools/ui_simulator.py` produces deterministic 480x480 screenshots and schema checks.
- The simulator must keep `large-mesh` coverage for oversized node/message stores.
- Serial diagnostics include `routes`, `routes detail <seq>`, `routes trace <fingerprint>`, `routes probe <fingerprint>`, and `routes clear`.
- `ui_corruption_probe_d1l.py` replaces the old high-count tab stress gate. It exercises tabs, serial-only UI data refresh through `ui data-canary`, Packet search, Public search, health, and crashlog checks with `public_rf_tx=false` and `formats_sd=false`.
- `ui_capture_d1l.py` freezes the firmware-maintained RGB565 display shadow, reconstructs a PNG on the PC, and gives the release gate real pixel evidence for split/stale hardware frames.

## Active Docs

Use [docs/README.md](README.md) as the documentation index. Update [README.md](../README.md), [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md), [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md), and [TEST_PLAN_D1L.md](TEST_PLAN_D1L.md) whenever evidence changes.
