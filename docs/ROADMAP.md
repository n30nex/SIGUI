# MeshCore DeskOS D1L Roadmap

This is the active roadmap. Historical phase checkpoints and Codex handoff docs were removed from the release-facing docs so current status lives in one place.

Release status: the exact post-WP-01 audit still reports `ready_for_public_release=false`, with **15 remaining P0 failures and 16 failures overall including P1**. No release tag should be cut until every applicable exact-commit gate is green.

Execution checkpoint: live `main` is `3e712916a05931fd10998f51d7f616e506daeeb4`. WP-01 is now `hardware_green` with `proof_banked=true` on exact source `092293f2311a24c9899bc9bf343ab014c4ba0411`, but its implementation is **not merged**. Push/PR Actions runs `29272708844` / `29272709642` are green, the Actions host job reports **773 passed**, and all **8 checksum manifests / 78 entries** verify. WP-02 is `in_progress` and owns landing PRs #62, #64, and #80 in dependency order. [COMPLETION_LEDGER.yaml](COMPLETION_LEDGER.yaml) remains the machine-readable execution source, with [COMPLETION_STATUS.md](COMPLETION_STATUS.md) as its generated view.

The active UI boundary includes `ui_navigation.c`, `ui_chrome.c`, `ui_home.c`, `ui_settings.c`, `ui_keyboard.c`, `ui_screen.c`, and `ui_modal.c`.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| UI modular ownership and current hierarchy proof | Navigation, shared chrome/layout, Home state/geometry/rendering, the top-level More hierarchy/actions, keyboard policy, the screen-root boundary, and single-active modal ownership are split across focused modules. PR #60 / source `0b138be` passed push Actions `29068006554` and PR Actions `29068007961`, then proved Mesh Roles, Rooms, and Repeaters on COM12 with exact CRC matches `63DE54FB`, `FD538D71`, and `5C41EE08`, passing simulator diffs, and a clean three-round probe without Public RF or formatting. The current host slice applies the same full-height progressive disclosure to read-only Storage, Card status, and Data locations pages with a fixed no-format footer. The remaining #16/#6 work is to own the Messages renderer and Network/routes, Map, Packets, contact, and settings page domains; add UI-task command ownership; finish sheet-specific keyboard submit/cancel ownership; and deepen single-root/callback-lifetime invariants. | Build this Storage slice in Actions, capture `storage`, `storage_card`, and `storage_data` on COM12, and physically verify Back, fixed-footer, and Data locations scrolling. Do not rerun PR #60 Mesh Roles pixels for this unrelated slice. The final frozen release commit still needs commit-matched gate evidence. |
| Full RF/DM acceptance | Public and outbound DM foundations exist; full inbound DM, ACK/PATH, direct-route proof remains open. | Produce `rf_full_acceptance_*.json` with health and no-Public-command proof. |
| Remaining FAT32 matrix and SD/card/power release gates | WP-01's narrow exact-pair gate is banked on `092293f`: clean inserted-card stability, 10/10 real removals/reinsertions, 5/5 retained reboots, and a 7,207.089-second six-segment active-storage soak passed with retained-worker stack floor 7,976 bytes, `public_rf_tx=false`, and `formats_sd=false`. This closes only the WP-01 source gate. | After WP-02 lands the stack, complete the exact integrated/frozen-candidate no-card, unusable/non-FAT32, representative-card/size, electrical/power-loss, Seeed, cold/warm boot, and final 12-hour storage-aware matrices. Do not reuse `092293f` as exact proof for a later integrated or tagged SHA. |
| Physical screenshots/review | Host simulator screenshots are committed under `docs/screenshots`; PR #56 / Actions `29060900359` provides current hierarchy framebuffer proof on COM12 with CRC `E72745BA`. | Add physical device photos and the manual UI review artifact. |
| Long soak | Short evidence exists. | Run 12-hour idle/listening soak on the release artifact. |

## Recently Closed P0 Evidence

- WP-01 exact-pair storage/reboot source proof: `092293f2311a24c9899bc9bf343ab014c4ba0411` / Actions `29272708844` and `29272709642` passed 773 host tests and 8 manifests / 78 checksum entries. Canonical SHA-256 receipts are provenance `2decf8ad60b73e71bbb09b489adba8fd827856a8daf00c376c5a9ba5354e451e`, inserted stability `a038ee7ca371c4ee404493c721a343ef54e7bbd55b08273c8dc833d9d0203aef`, removal/reinsert `3a3882038fec2497529d281f3c2b9b7468c1e62dcca7962ca3b9492125f0fad1`, reboot matrix `db6cab3020bfa8ef575bd6a59c61d1277a8e72e1e73eb987248817199797a986`, active soak `caf19395d0e1a175f6fa13c2550bc8693297661756bf339ba4acec63da2699b9`, and aggregate `994f4e5ac7b9e0e8bdb57aad7715f52a99294a1841847860e2ce2f70bd6e2277`. WP-01 remains unmerged; this proof does not close the broader final SD/power/soak gates.
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

1. Execute WP-02: land PR #62, then retarget/land #64, then retarget/land #80, running fresh exact-head Actions and checksum verification at each layer. Pre-final local rehearsals are #62 `7648611c412e7f4658f5d14b43ba530744d96160` (**423 full / 80 focused**), #64 `c5886de1e2988b2097034183d5e39bb3aec88344` (**575 / 128**), and #80 `341a3abf4db4c52acf5859e396f25e7adb4cbab1` (**787 / 302**). These are local merge rehearsals only, not remote Actions or hardware proof, and must absorb the final proof-ledger/main state before push.
2. Capture the exact merged-main WP-02 integration baseline; do not treat the banked `092293f` WP-01 hardware receipt as evidence for the new integrated SHA.
3. Continue the dependency graph through immutable build inputs, protocol/runtime ownership, semantic MeshCore conformance, truthful messaging/routing, UI ownership, connectivity/Map, administration, update/recovery, and full-feature work.
4. Complete the broader physical SD/card/Seeed/electrical/power-loss and cold/warm boot matrices.
5. Finish RF/DM acceptance without forbidden ports, then capture physical UI/photos and manual review.
6. Run the final 12-hour idle/listening and mixed active soaks on the frozen release candidate.
7. Run the final release-gate sweep and tag only the exact fully qualified artifact.

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
