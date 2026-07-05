# MeshCore DeskOS D1L Roadmap

This is the active roadmap. Historical phase checkpoints and Codex handoff docs were removed from the release-facing docs so current status lives in one place.

Release status: `scripts/release_gate_audit_d1l.py` reports `ready_for_public_release=false` until all P0 evidence gates pass on the release commit. No release tag should be cut until the audit is green.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| UI modular ownership and newest Home proof | Navigation ownership, the modal helper boundary, and shared chrome/layout policy are split into `ui_navigation.c`, `ui_modal.c`, and `ui_chrome.c`. The remaining #16/#6 work is to finish owned screen/render modules, keyboard/input ownership, single-active-root invariants, and current Home pixel/manual evidence without broad UI cycling. | For code-only module slices, use host tests plus default Actions. For Home/pixel acceptance, run only the matching COM12 `ui_capture_d1l.py` proof once COM12 is available. |
| Full RF/DM acceptance | Public and outbound DM foundations exist; full inbound DM, ACK/PATH, direct-route proof remains open. | Produce `rf_full_acceptance_*.json` with health and no-Public-command proof. |
| SD release matrix | Core SD file/history/remount/map/export canaries, raw diagnostics, RP2040-unavailable fallback, and the official Seeed FAT32 smoke pass on the current FAT32 card from `1a29876` / Actions `28714355561`. | Add physical no-card and unformatted/non-FAT32 proof, <=32GB FAT32 matrix, no-format language proof for unusable media, and power/electrical evidence. |
| Physical screenshots/review | Host simulator screenshots are committed under `docs/screenshots`; PR #37 refreshes the Home simulator screenshot for the newest title-only/full-height icon layout. | Add physical device photos, manual UI review artifact, and fresh COM12 Home pixel capture for the newest Home layout once COM12 is available. |
| Long soak | Short evidence exists. | Run 12-hour idle/listening soak on the release artifact. |

## Recently Closed P0 Evidence

- Split-page/stale-column redraw proof: COM12 `ui_corruption_probe_d1l.py` from `59610ab` / Actions `28723265336` completed 20 targeted rounds with zero failures, 20 serial data-refresh events, `final_pending=false`, `public_rf_tx=false`, and `formats_sd=false`.
- Previous Home pixel proof: COM12 `ui_capture_d1l.py` from PR #33 (`c6a88e2` / PR Actions `28725692751`, merged as `e086312`) reconstructed the then-current 480x480 RGB565 Home PNG, matched firmware CRC `ED8A8E31`, and passed simulator/reference diff.
- Icon Home proof: PR #37 host/simulator and Actions evidence show Home renders a title-only full-height icon launcher with colored Time/Wi-Fi/BLE/SD status icons and keeps the bottom dock off the Home screen while non-Home pages keep the dock. Fresh COM12 pixel proof is still pending for this newest Home layout.
- Compose/input keyboard proof: PR #35 / issue #2 captured all 12 release-blocking keyboard callers on COM12 from `fce5d82` / Actions `28727064923` using `ui_compose_keyboard_capture_d1l.py --targets all`. The artifact reports `ok=true`, `capture_count=12`, `public_rf_tx=false`, and `formats_sd=false` for Public/DM compose, Public search, Packet search, contact edit, onboarding, map location/provider, and Wi-Fi SSID/password.

## Feature Direction

- Keep MeshCore-first behavior. Public messages, DMs, routes, packet diagnostics, and retained history are the core release value.
- Keep Wi-Fi, BLE, GPS, OTA, and live map tile browsing honest as experimental or pending until hardware-proven.
- Keep SD FAT32-only. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. Non-FAT32 media must report guidance and retain NVS fallback.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Keep hardware validation autonomous where possible: COM12 for ESP32 app/console, COM16 for RP2040 USB/CDC/UF2. Do not use COM8, COM11, or COM29 for D1L validation.

## Active Work Queue

1. Continue #16/#6 UI modular ownership slices, then capture the current Home COM12 pixel/manual proof once COM12 is available.
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
