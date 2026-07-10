# MeshCore DeskOS D1L Roadmap

This is the active roadmap. Historical phase checkpoints and Codex handoff docs were removed from the release-facing docs so current status lives in one place.

Release status: `scripts/release_gate_audit_d1l.py` reports `ready_for_public_release=false` until all P0 evidence gates pass on the release commit. No release tag should be cut until the audit is green.

The active UI boundary includes `ui_navigation.c`, `ui_chrome.c`, `ui_home.c`, `ui_settings.c`, `ui_keyboard.c`, `ui_screen.c`, and `ui_modal.c`.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| UI modular ownership and current hierarchy proof | Navigation, shared chrome/layout, Home, More, keyboard policy, the screen-root boundary, and single-active modal ownership are split across focused modules. Merged PR #61 / main `5b5dfaa` (source `4d9f384`) proved Storage on COM12. The current Map slice uses the actual map as its root, one Options action, `Map -> Map options -> Set location or Cache status`, built-in OpenStreetMap Standard, fixed `(c) OpenStreetMap contributors` attribution, and no provider/source editor. Its fail-closed policy permits only the visible current-view 3x3 at one zoom while Map is active, with cache/reuse. There is no area download and no background fetch. | Build this Map slice in Actions; capture `map`, `map_options`, `map_location`, and `map_cache` on COM12. Prove pre/post `map tiles status.network_requests` counters are equal and the probes report `network_tx=false`, `map_network_requests=false`, `visible_tile_limit=9`, and `zoom_batch_limit=1`, then perform one quick live 3x3/cache-reuse check. Manual physical touch/photos and final frozen-release evidence remain open. |
| Supported ESP-IDF baseline | The Seeed D1L BSP and current Actions image remain pinned to ESP-IDF v5.1.x. [Espressif's final v5.1.7 notice](https://github.com/espressif/esp-idf/releases/tag/v5.1.7) says that branch reached end of life in December 2025 and no longer receives bug or security fixes. | Qualify the D1L BSP and firmware on a supported ESP-IDF release (target v5.5.x unless vendor compatibility requires another supported branch), update Actions/configuration, then rerun build, flash, smoke, RF, SD, UI, and soak gates. If migration is blocked by the vendor BSP, document and approve a maintained security-patch plan before public release. |
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
- Compose/input keyboard proof: PR #35 / issue #2 captured the historical 12-input set on COM12 from `fce5d82` / Actions `28727064923`. The built-in map source removes the provider keyboard; the active `--targets all` set is 11 inputs and current artifacts use `capture_count=11` with `network_tx=false`, `public_rf_tx=false`, and `formats_sd=false`.

## Feature Direction

- Keep MeshCore-first behavior. Public messages, DMs, routes, packet diagnostics, and retained history are the core release value.
- Keep Wi-Fi, BLE, GPS, OTA, and live map tile browsing honest as experimental or pending until hardware-proven. Map requests remain current-view-only even after proof.
- Keep SD FAT32-only. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. Non-FAT32 media must report guidance and retain NVS fallback.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Keep hardware validation autonomous where possible: COM12 for ESP32 app/console, COM16 for RP2040 USB/CDC/UF2. Do not use COM8, COM11, or COM29 for D1L validation.

## Active Work Queue

1. Prove the host-complete Map/Options/Location/Cache hierarchy on exact Actions-built COM12 pixels, verify network-suppressed probes never request tiles, and capture one guarded visible 3x3/cache-reuse hardware check before continuing the screenshot-audited #16/#6 slices.
2. Qualify and migrate the Seeed D1L BSP from EOL ESP-IDF v5.1.x to a supported SDK baseline, then repeat hardware smoke on its Actions artifact.
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
- `ui_corruption_probe_d1l.py` replaces the old high-count tab stress gate. It enters Map through the network-suppressed scroll-probe path, exercises serial-only UI data refresh, Packet/Public search, health, and crashlog checks, and samples `map tiles status.network_requests` before and after automation. Release evidence requires equal counters plus `network_tx=false`, `map_network_requests=false`, `public_rf_tx=false`, and `formats_sd=false`.
- `ui_capture_d1l.py` freezes the firmware-maintained RGB565 display shadow, reconstructs a PNG on the PC, and gives the release gate real pixel evidence for split/stale hardware frames.

## Active Docs

Use [docs/README.md](README.md) as the documentation index. Update [README.md](../README.md), [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md), [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md), and [TEST_PLAN_D1L.md](TEST_PLAN_D1L.md) whenever evidence changes.
