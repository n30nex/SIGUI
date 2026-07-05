# MeshCore DeskOS D1L Roadmap

This is the active roadmap. Historical phase checkpoints and Codex handoff docs were removed from the release-facing docs so current status lives in one place.

Release status: `scripts/release_gate_audit_d1l.py` reports `ready_for_public_release=false` until all P0 evidence gates pass on the release commit. No release tag should be cut until the audit is green.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| On-screen keyboard and sheets | Public/DM compose keyboard geometry is hardware-proven on COM12 from Actions-built `59610ab` / run `28723265336`. Other keyboard and sheet callers still need the same capture-driven pass plus physical touch review. | Run the issue-named `ui_compose_keyboard_capture_d1l.py` or manual review helper only for the specific remaining sheet/workflow under test. |
| Full RF/DM acceptance | Public and outbound DM foundations exist; full inbound DM, ACK/PATH, direct-route proof remains open. | Produce `rf_full_acceptance_*.json` with health and no-Public-command proof. |
| SD release matrix | Core SD file/history/remount/map/export canaries, raw diagnostics, RP2040-unavailable fallback, and the official Seeed FAT32 smoke pass on the current FAT32 card from `1a29876` / Actions `28714355561`. | Add physical no-card and unformatted/non-FAT32 proof, <=32GB FAT32 matrix, no-format language proof for unusable media, and power/electrical evidence. |
| Physical screenshots/review | Host simulator screenshots are committed under `docs/screenshots`. | Add physical device photos and manual UI review artifact. |
| Long soak | Short evidence exists. | Run 12-hour idle/listening soak on the release artifact. |

## Recently Closed P0 Evidence

- Split-page/stale-column redraw proof: COM12 `ui_corruption_probe_d1l.py` from `59610ab` / Actions `28723265336` completed 20 targeted rounds with zero failures, 20 serial data-refresh events, `final_pending=false`, `public_rf_tx=false`, and `formats_sd=false`.
- Hardware Home pixel proof: COM12 `ui_capture_d1l.py` from `59610ab` / Actions `28723265336` reconstructed a 480x480 RGB565 PNG, matched firmware CRC `B7D2D890`, and passed simulator/reference diff.
- Icon Home proof: Home renders the icon-first launcher and keeps the bottom dock off the Home screen while non-Home pages keep the dock.
- Compose proof: COM12 `ui_compose_keyboard_capture_d1l.py` from `59610ab` / Actions `28723265336` captured Public short/long and DM short/long keyboard states with no RF TX or SD formatting.

## Feature Direction

- Keep MeshCore-first behavior. Public messages, DMs, routes, packet diagnostics, and retained history are the core release value.
- Keep Wi-Fi, BLE, GPS, OTA, and live map tile browsing honest as experimental or pending until hardware-proven.
- Keep SD FAT32-only. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. Non-FAT32 media must report guidance and retain NVS fallback.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Keep hardware validation autonomous where possible: COM12 for ESP32 app/console, COM16 for RP2040 USB/CDC/UF2. Do not use COM8, COM11, or COM29 for D1L validation.

## Active Work Queue

1. Finish the remaining keyboard/sheet physical review with issue-scoped capture evidence.
2. Finish RF/DM acceptance without reserved local ports.
3. Complete the remaining physical SD release matrix now that core SD works.
4. Capture physical device photos and manual UI review.
5. Run the 12-hour idle/listening soak.
6. Run the final release-gate sweep on the release artifact.

## Validation Notes

- Keep the release loop issue-sized: each open P0 should name the one proof that
  closes it. Do not run the bundled COM12 UI sweep unless a P0 spans multiple UI
  gates or the issue-sized P0 list is done and final production sweep evidence is
  being collected.
- `tools/ui_simulator.py` produces deterministic 480x480 screenshots and schema checks.
- The simulator must keep `large-mesh` coverage for oversized node/message stores.
- Serial diagnostics include `routes`, `routes detail <seq>`, `routes trace <fingerprint>`, `routes probe <fingerprint>`, and `routes clear`.
- `ui_corruption_probe_d1l.py` replaces the old high-count tab stress gate. It exercises tabs, serial-only UI data refresh through `ui data-canary`, Packet search, Public search, health, and crashlog checks with `public_rf_tx=false` and `formats_sd=false`.
- `ui_capture_d1l.py` freezes the firmware-maintained RGB565 display shadow, reconstructs a PNG on the PC, and gives the release gate real pixel evidence for split/stale hardware frames.

## Active Docs

Use [docs/README.md](README.md) as the documentation index. Update [README.md](../README.md), [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md), [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md), and [TEST_PLAN_D1L.md](TEST_PLAN_D1L.md) whenever evidence changes.
