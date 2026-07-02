# MeshCore DeskOS D1L Codex Roadmap And Feature Matrix

Updated: 2026-07-02

Source of truth: `docs/MeshCore_DeskOS_D1L_Final_Roadmap_and_Codex_Handoff_2026-07-02.md`

## Current Release Position

DeskOS D1L remains a developer preview. Public binary release is blocked until the release gate reports `ready_for_public_release=true` with current hardware evidence.

## P0 Matrix

| Area | Status | Notes | Evidence |
|---|---|---|---|
| A. No on-device SD formatting | Complete in code/docs/tests | Removed ESP32 storage format API, serial setup-confirm path, RP2040 formatter command, simulator formatter model, and acceptance-runner format branch. SD policy is FAT32-only with NVS fallback. | `python -m pytest tests/test_sd_policy_no_format.py tests/test_rp2040_sd_protocol.py tests/test_rp2040_sd_bridge_target.py tests/test_storage_status_contract.py tests/test_ui_shell_contract.py tests/test_ui_simulator.py tests/test_sd_boot_prepare_acceptance_d1l.py tests/test_rp2040_sd_bridge_preflight_d1l.py tests/test_smoke_d1l_parser.py -q` -> 92 passed |
| A. FAT32 boot auto-provision | Blocked by firmware mount issue | RP2040 mount path creates `/deskos` folders/manifests on mounted FAT32 cards, but the user-confirmed FAT32 32 GB validation card still fails before the ready file-operation gate. Actions run `28610808704` for `6b66452` proved the expanded raw diagnostics build passes Actions and keeps the bridge responsive, but direct COM16 USB still reports `state=no_card` / `probe_err=4`. The completed diagnostic shows all candidates returning `CMD0=0`, `CMD8=0`, and R7 `[0,0,0,0]`, so the current firmware follow-up enables the RP2040 internal pull-up on SD MISO before SPI1 claims the pin. This is firmware-side, not a card-format task. | Actions-built bridge artifact flashed locally; `artifacts/hardware/com16/rp2040_direct_preflight_6b66452_actions_28610808704_COM16.json` and `artifacts/hardware/com16/rp2040_direct_diag_6b66452_actions_28610808704_COM16.json` report `ready_for_sd_acceptance=false`, `formats_sd=false`, `public_rf_tx=false`, all-zero raw CMD diagnostics, with no ESP32 flash because the only ESP32-S3 port currently enumerated is forbidden `COM11` |
| B. Home latest-message overlap | Complete in code/tests | Home latest-message cards now use separate sender, message body, and metadata lanes with taller rows. | `python -m pytest tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 30 passed |
| B. Messages layout/header overlap | Complete in code/tests | Messages header actions moved to a dedicated row; Public/DM rows use separate text lanes and scrollable preview rendering. | `python -m pytest tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 30 passed |
| B. Compose keyboard dock overlap | Complete in code/tests | Compose now opens as a full-height modal, hides the dock while active, and exposes a larger keyboard surface. | `python -m pytest tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 30 passed |
| B. Message detail normal/Advanced split | Complete in code/tests | Normal detail hides seq, uptime, and path hash; Advanced toggles developer metadata. | `python -m pytest tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 30 passed |
| C. Nodes full heard browser | Complete in code/tests | Nodes page queries `D1L_NODE_STORE_CAPACITY`, scrolls all heard nodes, adds DM only for contactable keyed nodes, and keeps room/repeater management gated until protocol/auth exists. | `python -m pytest tests/test_node_store_contract.py tests/test_packet_log_contract.py tests/test_mesh_inspector_contract.py tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 39 passed |
| C. Messages paged history | Complete in code/host tests; physical review pending | Public history and DM thread sheets now use page-aware retained-store queries, show newest rows first, and expose Load Older when older retained rows exist. Serial diagnostics support `offset <n>` for Public, Public search, recent DMs, and filtered DM threads with page metadata. | `python -m pytest tests/test_message_store_contract.py tests/test_dm_store_contract.py tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 39 passed |
| C. Packets latest 100 plus Load Older | Complete in code/host tests; hardware proof pending | Packet UI now renders bounded 100-row pages, with Load Older/Newer paging through the SD segment journal when available and falling back to the RAM/NVS window otherwise. Packet logging keeps the small 8-row NVS fallback plus a 24h-target, 4096-record SD segment journal when the SD file gate is ready. Hardware 24h acceptance proof remains open. | `python -m pytest tests/test_packet_log_contract.py tests/test_storage_status_contract.py tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 33 passed |
| D. Lat/lon map location sheet | Complete in code/tests | Replaced the compass/zoom picker with full-height decimal latitude/longitude entry, validation, keyboard target switching, and dock-hiding behavior. | `python -m pytest tests/test_ui_shell_contract.py tests/test_ui_simulator.py tests/test_settings_contract.py -q` -> 33 passed |
| D. Wi-Fi scan/connect/status | Complete in code/host tests; hardware proof pending | Added explicit station runtime enablement, bounded scan results, password-safe connect/status, touch Scan/Connect controls, and offline-first default-off behavior. Firmware build/hardware proof must use GitHub Actions artifacts only. | `python -m pytest tests/test_connectivity_manager_contract.py tests/test_storage_status_contract.py tests/test_ui_shell_contract.py tests/test_ui_simulator.py tests/test_settings_contract.py -q` -> 45 passed |
| D. Policy-compliant tile downloader | Complete in code/host tests; hardware proof pending | Added provider-required map state, user-facing no-tile guidance, attribution requirement, hard URL-template guard rejecting public OSM bulk tile hosts, explicit serial `storage map-tile-download` HTTPS streaming into the SD cache, persisted touch provider/attribution/zoom setup, Settings/Map Tiles entry, and touch `Download` for one center tile at the saved D1L location. | `python -m pytest tests/test_storage_status_contract.py tests/test_connectivity_manager_contract.py tests/test_settings_contract.py tests/test_ui_shell_contract.py tests/test_ui_simulator.py -q` -> 46 passed; `python .\tools\ui_simulator.py --out artifacts\ui-sim-phase-d-map-tiles --view map --view map_tiles_sheet --view settings` -> ok |
| E. Settings setup dashboard | Complete in code/host tests; hardware proof pending | Settings is now a compact end-user setup dashboard with SD Card, Wi-Fi, BLE, Radio, Map Tiles, Display, Identity, Diagnostics, About, and Advanced tiles. Advanced is the only landing tile for raw/experimental advert tools. | `python -m pytest tests/test_ui_shell_contract.py tests/test_ui_simulator.py tests/test_settings_contract.py tests/test_storage_status_contract.py -q` -> 43 passed; `python -m pytest tests -q` -> 240 passed; `python .\tools\ui_simulator.py --out artifacts\ui-sim-phase-e-settings --view settings --view storage_setup_sheet --view wifi_setup_sheet --view radio_settings_sheet --view diagnostics_sheet` -> ok |
| P0. RF receive corrupts visible UI | Complete in code/tests; automated COM12 smoke/tab/scroll passed; manual photos/RF acceptance pending | Added static mutex guards around packet, Public message, DM, node, route, contact, and mesh-inspector stores so UI snapshots and serial diagnostics cannot copy mutable rings while RF receive appends/update them. Fixed a serial tab-state race found by the COM12 100-cycle tab-abuse gate so `pending=false` is not reported until the requested tab is active and rendered. Periodic chrome refresh still avoids content redraw. | `python -m pytest tests -q` -> 243 passed; GitHub Actions `d1l-ci` run `28593151178` passed for `c0c20a2`; downloaded artifact flashed to `COM12`; `smoke_c0c20a2_actions_28593151178_COM12.json` ok; `ui_tab_abuse_c0c20a2_actions_28593151178_COM12.json` ok with 100 cycles/0 failures; `scroll_probe_c0c20a2_actions_28593151178_COM12.json` ok |
| F. Release gate | Pending | Current Actions artifacts/checksums, packaged notices, COM12 smoke, 100-cycle tab abuse, and scroll probe passed for `c0c20a2`. Still blocked by SD matrix readiness, outbound/full RF proof, 12h soak, and manual photo review. SD preflight remains non-destructive; the current SD blocker is the RP2040 firmware mount path failing against a user-confirmed FAT32 card. | `python .\scripts\release_gate_audit_d1l.py --github-run-id 28593151178 --github-run-dir artifacts\github\28593151178-current --commit c0c20a21883950dc3e0214eeda1f0a7bc64eb547 --d1l-port COM12 --hardware-dir artifacts\hardware\com12 --out artifacts\release-gate\d1l-release-gate-audit-c0c20a2-COM12-progress.json` -> `ready_for_public_release=false`, 5 P0 failures |

## Phase A Completion Notes

- The RP2040 bridge no longer exposes an SD format request, progress line, confirmation phrase, or formatter function.
- ESP32 storage no longer has a confirmed-format API.
- `storage setup` is policy/status only and reports `policy="no_device_format"`.
- Unmountable or non-FAT32 cards are classified as `not_fat32_or_unmountable`; raw-card-present failures with RP2040 `mount_error`/`mount_data` diagnostics are surfaced as `inspect_rp2040_sd_mount_error_firmware_path` because the current validation card is user-confirmed FAT32.
- A mounted FAT32 card with missing DeskOS files is still auto-provisioned with the DeskOS directory and manifest structure.
- Existing/invalid DeskOS manifests are not overwritten; status falls back to NVS and asks the user to back up/reformat FAT32 on a computer.

## Phase B Completion Notes

- Home message previews now keep sender/status, body text, and radio metadata on distinct rows.
- Messages header controls no longer compete with the title or unread summary.
- Public and DM preview rows are taller and scroll with the main content root instead of being squeezed above the dock.
- Compose Public/DM hides the dock and uses the full lower display for text entry plus keyboard.
- Message Detail normal mode shows sender, body, signal, and friendly hop count only; sequence, uptime, direction, and path hash are behind Advanced.

## Phase C Notes

- Nodes now use a direct full-capacity query from the UI instead of the 4-row snapshot preview.
- Node rows expose DM only for keyed, contactable non-management nodes; room and repeater management remains gated and non-clickable until protocol/auth support exists.
- Packet feed now uses bounded 100-row pages. Load Older advances to older pages and Newer returns toward the live page, instead of rendering thousands of LVGL rows at once.
- Packet storage keeps the small NVS fallback, the compact SD snapshot, and a 64 x 64 record SD history journal under `stores/packet_log/segments/` for a 4096-record, 24h-target history window.
- The SD journal write/read/query path is code/host-contract complete; the 24h release requirement still needs real-card COM12 proof from a downloaded GitHub Actions artifact.
- Public History and DM Thread now use page-aware retained queries, keep the visible page in chronological order, and Load Older by expanding the retained row limit toward the bounded store capacity.
- Serial message diagnostics now include `offset`, `page_size`, `page_count`, `total_matches`, `has_older`, and `next_offset` so hardware scripts can prove older Public/DM rows without scraping UI state.
- Full host regression after message and packet SD paging passed: `python -m pytest tests -q` -> 240 passed.

## Phase D Notes

- Map location entry now uses decimal latitude and longitude text fields plus the LVGL keyboard; the old compass/zoom picker controls were removed from firmware, simulator flows, and tests.
- Opening the map location sheet hides the bottom dock so the keyboard cannot be covered.
- Wi-Fi station setup now supports explicit enablement, bounded scans, saved-profile connect, status/IP/RSSI reporting, and a full-height touch setup sheet with Scan/Connect controls. Wi-Fi remains off by default.
- Map no-tile state now tells users to connect Wi-Fi and download allowed tiles for their area; provider configuration and attribution are required, public OSM bulk tile hosts are rejected by policy guard, `storage map-tile-download <z> <x> <y> <url-template> <attribution>` provides the bounded serial HTTPS-to-SD primitive, and the touch Map Tiles sheet persists an allowed provider plus attribution/zoom before downloading one center tile for the saved D1L location.
- Full host regression after Map Tiles touch provider/download work passed: `python -m pytest tests -q` -> 240 passed.
- Firmware build validation is GitHub Actions artifact only; a local Podman build was intentionally stopped after the operator restated this hard rule. Host tests remain the local validation path.
- Latest Actions-only validation: `d1l-ci` run `28593151178` passed for `c0c20a2`; the downloaded release package flashed to `COM12` with esptool hash verification.

## Phase E Notes

- Settings now opens as a setup dashboard instead of developer-oriented Wireless/MeshCore panels.
- The first viewport exposes all required setup categories: SD Card, Wi-Fi, BLE, Radio, Map Tiles, Display, Identity, Diagnostics, About, and Advanced.
- SD Card copy repeats the non-destructive FAT32-only policy; Wi-Fi copy states that mesh messaging remains offline; Map Tiles points users toward Wi-Fi plus an allowed provider; Advanced keeps raw/experimental advert tools out of normal setup rows.
- Full host regression passed after the dashboard/race/policy work (`240 passed`), and the Phase E simulator report had zero overflow, zero touch-target issues, and no missing required labels.
- Firmware build and physical UI proof still must come from GitHub Actions artifacts and hardware photos/manual review.

## P0 RF Receive UI Stability Notes

- Packet receive no longer shares unguarded mutable ring/static scratch storage with UI snapshot rendering.
- `main/mesh/store_lock.h` provides static FreeRTOS mutex initialization without heap allocation.
- Packet, Public message, DM, node, route, contact, and mesh-inspector store reads/writes now serialize around their static RAM state.
- The downloaded `c0c20a2` GitHub Actions artifact passed COM12 smoke, 100-cycle tab abuse, and scroll probe while recent RF packet/message rows were present. Manual physical photos/review and the full RF acceptance artifact remain open.

## Release Gate Policy Notes

- `scripts/release_gate_audit_d1l.py` now requires SD preflight evidence to report `formats_sd=false` before the SD acceptance gate can pass.
- Stale SD artifacts that contain old format-capable next actions are marked obsolete and sanitized to a no-device-format repair action in the generated audit. Fresh raw-card-present failures with SdFat `mount_error` fields must point to firmware-side RP2040 mount inspection.
- The latest local audit `artifacts/release-gate/d1l-release-gate-audit-c0c20a2-COM12-progress.json` still fails closed with five P0 blockers: outbound DM proof, SD acceptance matrix, 12-hour soak, manual physical UI/photos, and full RF acceptance.

## Next Work Order

1. Complete SD acceptance with the user-confirmed FAT32 card using a downloaded Actions artifact: fix the RP2040 firmware mount path so the card reaches `ready`, then prove boot auto-provision, no-card NVS fallback, non-destructive unmountable-card guidance, and 24h packet-history retention. The current COM16 direct-RP2040 evidence reports a firmware-side all-zero/CMD8 echo rejection path; do not treat reformatting the card as the fix.
2. Capture RF acceptance without using forbidden ports unless the operator explicitly reassigns them: outbound DM proof, controlled inbound DM, ACK/PATH, and direct-route proof.
3. Capture manual physical UI/photos and the full 12-hour idle/listening soak from the downloaded Actions artifact.
4. Hardware-validate Phase D map work from a downloaded Actions artifact: Wi-Fi connect, provider setup, center-tile download, visible attribution, no public OSM bulk endpoint, and SD cache reboot/remount behavior.
