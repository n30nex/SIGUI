# MeshCore DeskOS D1L Roadmap

This is the active roadmap. Historical phase checkpoints and Codex handoff docs were removed from the release-facing docs so current status lives in one place.

Release status: `scripts/release_gate_audit_d1l.py` reports `ready_for_public_release=false` until all P0 evidence gates pass on the release commit. No release tag should be cut until the audit is green.

The active UI boundary includes `ui_navigation.c`, `ui_chrome.c`, `ui_home.c`, `ui_settings.c`, `ui_keyboard.c`, `ui_screen.c`, and `ui_modal.c`.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| Supported ESP-IDF qualification (issue #63) | ESP-IDF v5.1 reached end of life in December 2025. The migration slice selects the version-pinned official `espressif/idf:v5.5.4` Actions tag and adds a P0 `supported_sdk_baseline` audit gate that rejects missing, moving, EOL, or unapproved workflow tags and a missing/stale committed lock target. The tag is not content-immutable, and these checks establish selected-target/lock policy only; they do not yet prove lock provenance, source/BSP compatibility, or hardware behavior. | Complete the pinned Seeed BSP/API/Kconfig inventory and source/config migration; generate and archive `dependencies.lock` in Actions, review and commit that exact output, then require a clean repeat build; pass the full host suite and Actions firmware/package/checksum checks; flash that exact artifact to COM12; require serial `version` to report `idf=v5.5.4`; repeat board/display/touch/Wi-Fi/RF/RP2040-SD/Map/health/reboot/power-cycle smoke; then refresh relevant commit-matched release evidence. |
| UI modular ownership and current hierarchy proof | Navigation, shared chrome/layout, Home state/geometry/rendering, the top-level More hierarchy/actions, keyboard policy, the screen-root boundary, and single-active modal ownership are split across focused modules. PR #60 / source `0b138be` passed push Actions `29068006554` and PR Actions `29068007961`, then proved Mesh Roles, Rooms, and Repeaters on COM12 with exact CRC matches `63DE54FB`, `FD538D71`, and `5C41EE08`, passing simulator diffs, and a clean three-round probe without Public RF or formatting. The current host slice applies the same full-height progressive disclosure to read-only Storage, Card status, and Data locations pages with a fixed no-format footer. The remaining #16/#6 work is to own the Messages renderer and Network/routes, Map, Packets, contact, and settings page domains; add UI-task command ownership; finish sheet-specific keyboard submit/cancel ownership; and deepen single-root/callback-lifetime invariants. | Build this Storage slice in Actions, capture `storage`, `storage_card`, and `storage_data` on COM12, and physically verify Back, fixed-footer, and Data locations scrolling. Do not rerun PR #60 Mesh Roles pixels for this unrelated slice. The final frozen release commit still needs commit-matched gate evidence. |
| Full RF/DM acceptance | Public and outbound DM foundations exist; full inbound DM, ACK/PATH, direct-route proof remains open. | Produce `rf_full_acceptance_*.json` with health and no-Public-command proof. |
| SD release matrix | Core SD file/history/remount/map/export canaries, raw diagnostics, RP2040-unavailable fallback, and the official Seeed FAT32 smoke pass on the current FAT32 card from `1a29876` / Actions `28714355561`. | Add physical no-card and unformatted/non-FAT32 proof, <=32GB FAT32 matrix, no-format language proof for unusable media, and power/electrical evidence. |
| Physical screenshots/review | Host simulator screenshots are committed under `docs/screenshots`; PR #56 / Actions `29060900359` provides current hierarchy framebuffer proof on COM12 with CRC `E72745BA`. | Add physical device photos and the manual UI review artifact. |
| Long soak | Short evidence exists. | Run 12-hour idle/listening soak on the release artifact. |

## Recently Closed P0 Evidence

- Split-page/stale-column redraw proof: COM12 `ui_corruption_probe_d1l.py` from `59610ab` / Actions `28723265336` completed 20 targeted rounds with zero failures, 20 serial data-refresh events, `final_pending=false`, `public_rf_tx=false`, and `formats_sd=false`.
- Previous Home pixel proof: COM12 `ui_capture_d1l.py` from PR #33 (`c6a88e2` / PR Actions `28725692751`, merged as `e086312`) reconstructed the then-current 480x480 RGB565 Home PNG, matched firmware CRC `ED8A8E31`, and passed simulator/reference diff.
- Prior icon Home proof: PR #40 host/simulator evidence, Actions run `28731948363`, and COM12 `ui_pixel_capture_b2ca00a_actions_28731948363_COM12.json` showed the previous small-title icon launcher with colored Time/Wi-Fi/BLE/SD status icons and no Home bottom dock. The capture reconstructed a 480x480 PNG with CRC `0DB2C27A`, simulator diff `ok=true`, `public_rf_tx=false`, and `formats_sd=false`; current Home geometry changes need a matching current-commit proof before release-gate use.
- Tightened Home proof: PR #51 / Actions `29055441069` captured `ui_pixel_capture_485744e_actions_29055441069_COM12.json` after fixing the live Setup-state label. The 480x480 RGB565 capture matched firmware/host CRC `BB15A654`, passed the simulator diff, and reported `public_rf_tx=false` plus `formats_sd=false`.
- Current hierarchy proof: PR #56 / source `51258ba` / Actions `29060900359` captured `ui_pixel_capture_51258ba_actions_29060900359_COM12.json`. The 480x480 RGB565 frame matched firmware/host CRC `E72745BA`, passed the simulator diff, and reported `public_rf_tx=false` plus `formats_sd=false`; the companion three-round all-tab probe had zero failures, empty crashlog, and no stuck render state.
- Contact hierarchy proof: merged PR #59 / source `d24552e` / Actions `29064260772` captured Contact Detail (`4DE99F9D`), Contact Options (`E1728433`), and confirmation-only Forget (`5A6D0604`) on COM12. Firmware and host CRCs matched, all simulator diffs passed, and the companion three-round all-tab probe had zero failures with `public_rf_tx=false` and `formats_sd=false`.
- Mesh Roles hierarchy proof: PR #60 / source `0b138be` passed Actions `29068006554` and `29068007961`; COM12 captured Mesh Roles (`63DE54FB`), Rooms (`FD538D71`), and Repeaters (`5C41EE08`) with exact firmware/host CRC matches and passing simulator diffs. The companion three-round all-tab/data-refresh probe had zero failures, empty crashlog, `public_rf_tx=false`, and `formats_sd=false`.
- Compose/input keyboard proof: PR #35 / issue #2 captured all 12 release-blocking keyboard callers on COM12 from `fce5d82` / Actions `28727064923` using `ui_compose_keyboard_capture_d1l.py --targets all`. The artifact reports `ok=true`, `capture_count=12`, `public_rf_tx=false`, and `formats_sd=false` for Public/DM compose, Public search, Packet search, contact edit, onboarding, map location/provider, and Wi-Fi SSID/password.

## Feature Direction

- Keep MeshCore-first behavior. Public messages, DMs, routes, packet diagnostics, and retained history are the core release value.
- Keep Wi-Fi, BLE, GPS, OTA, and live map tile browsing honest as experimental or pending until hardware-proven.
- Keep SD FAT32-only. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. Non-FAT32 media must report guidance and retain NVS fallback.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Keep hardware validation autonomous where possible: COM12 for ESP32 app/console, COM16 for RP2040 USB/CDC/UF2. Do not use COM8, COM11, or COM29 for D1L validation.

## Active Work Queue

1. Close issue #63 in stages: compatibility inventory and source/config migration; Actions-generated/reviewed/committed dependency lock plus a clean repeat build/package run; exact Actions-artifact COM12 `version.idf=v5.5.4` and behavioral qualification; and refreshed release evidence.
2. Prove the host-complete Storage/Card status/Data locations hierarchy on exact Actions-built COM12 pixels, Back paths, fixed no-format footer, and bounded Data locations scrolling, then continue the screenshot-audited #16/#6 slices through remaining owned page domains before adding the bounded UI command queue.
3. Finish RF/DM acceptance without reserved local ports.
4. Complete the remaining physical SD release matrix now that core SD works.
5. Capture physical device photos and manual UI review.
6. Run the 12-hour idle/listening soak.
7. Run the final release-gate sweep on the release artifact.

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
