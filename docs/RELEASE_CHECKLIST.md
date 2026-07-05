# Release Checklist

## Phase 1

- [x] Host tests pass.
- [x] Firmware builds with ESP-IDF v5.1.x in GitHub Actions; local firmware builds are not part of validation.
- [ ] Flash backup captured with SHA256. Skipped for current bring-up per operator instruction.
- [x] D1L flashes using explicit port only.
- [x] Boot banner includes firmware name/version/schema.
- [x] `i2c` detects D1L expander/touch.
- [ ] `display test` shows stable bars. Command passed on hardware; manual visual confirmation is still pending.
- [x] `wifi status`, bounded `wifi scan`, `wifi save <ssid> [password]`, password-safe `wifi connect`, `wifi clear`, and `ble status` report the supported companion-radio/profile state for the release build without printing saved Wi-Fi passwords.
- [x] `crashlog` and `health` provide reset reason, memory, LVGL, and task stack evidence after reboot.
- [x] `touch test` reports coordinates.
- [x] COM12 GitHub-built touch recovery validated from run `28510825288` / commit `c214ca1`: FT touch coordinates were sampled, bottom dock taps did not reboot, and post-fix idle soak stayed crash-free.
- [x] `backlight <0-100>` accepts dim/restore commands.
- [x] `radiohw` reports SX1262 status or exact wiring failure.
- [x] `radio get` reports US/CAN 910.525/BW62.5/SF7/CR5/20 dBm/TCXO NONE.
- [x] NVS settings survive reboot on hardware.
- [x] MeshCore local identity is generated and retained.
- [x] Controlled MeshCore advert TX/RX validated with a local bot.
- [x] Controlled MeshCore Public `test` TX/RX validated with a local bot.
- [x] Smoke JSON and monitor logs archived.

## Phase 3 UI Shell

- [x] 480x480 dark shell replaces the bring-up tile home.
- [x] Top status, home dashboard, bottom dock, packet view, Settings setup dashboard, advert sheet, toast, and lock overlay implemented.
- [x] First persisted onboarding sheet implemented with node name, Canada/USA preset, Desk Companion role, offline radio defaults, and identity generation.
- [x] Touch Public `test` action routes through the app model.
- [x] Phase 3 shell host contract tests pass.
- [x] Phase 3 shell firmware build passes in GitHub Actions.
- [x] Phase 3 shell flashed and smoke-tested on `COM7`.
- [x] Controlled Public `test` RF regression still receives local bot replies.
- [x] Simulator screenshots captured for the main shell views and current modal sheets.
- [x] Simulator layout report passes required-label and text-overflow checks.
- [x] Simulator touch-flow report passes expected-flow, 44x44 touch-target, RF/destructive flag, and no-format storage checks.
- [x] Offline Map tab, serial-configured manual center, and SD tile-cache/provider policy are covered by simulator required-label and flow checks.
- [x] First-open touch Map latitude/longitude keyboard entry and persisted D1L pin flow are covered by simulator required-label and flow checks.
- [x] Large simulated mesh UI stress passes with bounded message/node previews.
- [x] Bottom dock tab taps validated on D1L `COM12` after the deferred tab-switch fix; monitored touch samples and crashlog stayed clean.
- [ ] Manual visual review of the physical shell.
- [ ] Manual visual/touch review of the Public composer, DMs, persistent nodes/contacts/routes, and touch radio editing.

## Phase 4 Messaging And Stores

- [x] Bounded NVS-backed Public message store implemented.
- [x] Public TX/RX events append persisted recent message rows.
- [x] `messages public` serial diagnostic added to smoke coverage.
- [x] `messages public search <text>` and bounded Public History/Search UI added to smoke and simulator coverage.
- [x] Public History and DM Thread retained-history paging implemented with Load Older buttons plus serial `offset <n>` diagnostics and page metadata.
- [x] Public History/Search build flashed, standard-smoked on `COM7`, and targeted with COM11 hardware DM receive proof without Public-channel RF.
- [x] Repeatable DM-only COM11 hardware probe added and passed so Public-channel RF can stay quiet during targeted regressions; `artifacts/hardware/com12/dm_probe_b841621c.json` passed COM12-to-COM11 outbound DM proof with retained DM, packet, route, meshbot receive-counter, health, and no-Public-command checks all true.
- [x] Messages tab reads persisted Public rows from the app snapshot.
- [x] Public message store survives reboot on `COM7`.
- [x] Free-text Public composer implemented, built, flashed, and RF-regression tested.
- [x] Heard-node store survives reboot on `COM7`.
- [x] Contact store promotion from heard node survives reboot on `COM7`.
- [x] Route store survives reboot on `COM7`.
- [x] `routes trace <fingerprint>` diagnostic and contact-detail Route Trace sheet implemented with retained route/contact evidence plus an explicit DM-only `routes probe <fingerprint>` / touch `Ping` action; no Public RF is used by the active probe.
- [x] Heard-node and contact public-key retention survives reboot on `COM7`.
- [x] Contact favorite/mute flags persist across reboot and are exposed through serial diagnostics plus the contact detail UI.
- [x] Public unread/read state persists across reboot and is exposed through serial diagnostics plus the Messages UI.
- [x] Route detail diagnostics and first Packet-tab route detail sheet are implemented, built, flashed, and Public-RF probed on `COM7`.
- [x] Packet log persists newest 8 rows across reboot and exposes serial/touch packet detail.
- [x] Packet log filter/search/raw-hex developer mode implemented in serial diagnostics and Packet-tab UI.
- [x] Packet log active RAM capacity increased to 128 rows with compact 8-row NVS fallback and split SD/NVS flush foundation.
- [x] First `storage status`/`storage setup` fallback surface implemented: probes the RP2040 SD status protocol, reports protocol/card/root/setup state, never auto-formats, keeps onboard NVS store backends, and has SD Card tile/sheet plus simulator visibility.
- [x] ESP32 SD format request path removed; `storage setup` is policy/status only and reports `policy="no_device_format"`.
- [x] CI-buildable RP2040 SD bridge target added for `DESKOS_SD_STATUS`, `DESKOS_SD_MOUNT`, `DESKOS_SD_DIAG`, and bounded `DESKOS_SD_FILE` operations with no SD formatting command.
- [x] FAT32-only SD policy implemented: users prepare FAT32 cards on a computer, and DeskOS only creates missing `/deskos` folders/manifests on mounted FAT32 media.
- [x] Generic RP2040 SD file-operation protocol foundation added for future SD-backed stores: bounded `DESKOS_SD_FILE v=1` stat/read/write/append/delete/rename with path validation, base64url payloads, CRC32 checks, and simulator coverage.
- [x] Retained blob-store abstraction can use the RP2040 SD file protocol for retained Public/DM message history, route history, and packet history when a ready card reports file operations and atomic rename; NVS remains mirrored as fallback.
- [x] Serial-only SD file-operation proof command added: `storage filecanary` performs guarded temp write, read-back compare, `rename replace=1`, stat, final read, delete, and deleted-stat verification without Public RF or formatting.
- [x] Serial-only retained-history SD acceptance command added: `storage retained-canary <token>` appends synthetic Public/DM/route/packet rows only after all retained-history stores report SD backends, and `scripts/sd_retained_history_acceptance_d1l.py` proves readback before and after reboot without Public RF or formatting.
- [x] Serial-only diagnostic export SD canary added: `storage export-canary <token>` commits `exports/diagnostics/export-canary-<token>.json` through temp write/read plus atomic rename and `scripts/sd_export_canary_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only diagnostic export SD bundle added: `storage export-diagnostics <token>` commits bounded chunked storage/health/crashlog JSON to `exports/diagnostics/diagnostic-export-<token>.json`, reports map tile cache readiness without bundling actual tiles, and `scripts/sd_diagnostic_export_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only sampled data export SD bundle added: `storage export-data <token>` commits bounded chunked user-data JSON to `exports/data/data-export-<token>.json`, includes recent messages/DMs/routes/packets/contacts/nodes/read-state, excludes private identity material, and `scripts/sd_data_export_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only map tile cache SD canary added: `storage map-tile-canary <token>` commits `map/tiles/z12/x1/y2-<token>.tile` through temp write/read plus atomic rename and `scripts/sd_map_tile_canary_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only SD reboot/remount acceptance runner added: `storage map-tile-check <token>` verifies the previous synthetic tile read-only after remount, and `scripts/sd_reboot_remount_acceptance_d1l.py` proves retained Public/DM/routes/packets plus map-tile cache readback before and after reboot without Public RF or formatting.
- [x] First offline Map tab, serial `storage map-policy`, and `map center` commands added: they expose `map/tiles/z{z}/x{x}/y{y}.tile`, current cache readiness/backend, optional manual center, and disabled live-download state without Public RF or formatting.
- [x] Touch `Set Pin`/`Move Pin` Map entry added: it uses decimal latitude/longitude fields with an onscreen keyboard, persists through the app model/settings path shared with `map center set`, and does not send Public RF or touch/format SD.
- [x] Non-destructive RP2040 SD bridge preflight added: `scripts/rp2040_sd_bridge_preflight_d1l.py` verifies the Actions UF2 artifact, lists UF2 bootloader volumes, queries only the selected D1L port, records optional `storage diag` probe evidence, and reports the next bridge-flash/SD-acceptance action without Public RF, formatting, or UF2 copying.
- [x] SD boot/use acceptance runner added: `scripts/sd_boot_prepare_acceptance_d1l.py` covers no-card, correct-structure, missing-structure, unformatted, existing-data, and bridge-unavailable scenarios with no format command path.
- [x] Actions-built RP2040 SD bridge protocol and no-card fallback hardware-validated on COM12/COM16 from run `28478756887`; follow-up COM12 evidence through `8150b7b` proves RP2040 UART/ping/protocol/diag responsiveness and inserted-card `setup_required` detection without Public RF or formatting.
- [x] Actions-built RP2040 bridge restored after official Seeed SD smoke on COM16; current COM12 preflight from `1a29876` / Actions `28714355561` reports the FAT32 card ready for file operations without Public RF or formatting.
- [ ] Optional SD-card data storage implemented: boot detect/validate, FAT32-only auto-provision without device formatting, onboard fallback, SD-backed message/packet/route/export/map-tile stores, reboot/remount proof, and serial network tile validation only after explicit Wi-Fi enablement, allowed provider configuration, visible attribution, and user-scoped area selection. Current COM12/COM16 evidence proves the installed FAT32 card path, retained-history survival, reboot/remount, map-tile canary, export canary, diagnostic export, sampled data export, official Seeed smoke, raw diagnostics, correct/missing/existing-data boot scenarios, and RP2040-unavailable fallback; remaining release work is physical no-card and unformatted/non-FAT32 behavior, <=32GB card matrix, no-format guidance evidence tied to unusable media, and power/electrical proof. The serial single-tile `storage map-tile-download` primitive is implemented in code/host tests; touch live downloads remain unavailable as `tile_render_pending` until tile rendering proof exists.
- [x] Signal/room-server/repeater mesh visibility commands and summary cards are flashed, smoke-tested, and Public `test` RF-regression tested on `COM7`.
- [x] First touch Mesh Roles browser sheet is built, flashed, smoke-tested, and RF-regression tested on `COM7`.
- [x] Contact export QR-compatible serial command and touch sheet are implemented, host/simulator tested, flashed, smoke-tested, and targeted with a keyed contact on `COM7`.
- [x] Touch Radio Settings editor implemented, host/simulator tested, flashed, smoke-tested, targeted through serial profile changes, and Public-RF-regression tested on `COM7`.
- [ ] Manual contact-export QR scan/import proof with a MeshCore client.
- [x] DM store and serial flood-TX path survive reboot on `COM7`.
- [x] DM ACK/PATH receive parser and direct-route TX backend implemented, built, flashed, and smoke-tested on `COM7`.
- [x] First touch DM composer opens from keyed contact rows and routes through the DM backend.
- [x] First paged DM thread/detail sheet opens from recent DM rows and offers Load Older plus `Reply`.
- [x] Per-thread DM read cursors are persisted, exposed through serial diagnostics, and surfaced in the DM thread sheet.
- [x] DM thread sheet renders bounded scrollable retained history and `messages dm <fingerprint>` filters one retained thread; host/simulator, COM7 smoke, targeted serial, and active Public RF regression pass.
- [x] Full RF acceptance runner added for the COM12 D1L plus local COM11 Meshcorebot path: `scripts/rf_full_acceptance_d1l.py` writes one release-gate artifact covering identity, Meshcorebot status, outbound DM, controlled inbound DM, ACK/PATH, direct route, health, and no-Public-command checks.
- [x] Current `a1afd4b` RF evidence proves outbound DM, ACK/PATH, direct route, health, Meshcorebot-on-COM11, and no-Public-command checks; `artifacts/hardware/com12/rf_full_acceptance_a1afd4b.json` remains failed only because the controlled inbound DM token was not observed.
- [ ] Full DM workflow: manual touch review plus a passing newest `artifacts/hardware/com12/rf_full_acceptance_*.json` after the Meshcorebot control channel sends the recorded inbound `+dm ... <token>_in` command.

## Phase 7 Polish And Soak

- [x] Crash ring and reset reason diagnostics implemented and hardware-smoke validated.
- [x] Health telemetry reports heap/PSRAM, LVGL, reset reason, board/UI readiness, and task stack watermarks.
- [x] Repeatable idle/active soak runner added with JSON artifact output.
- [x] Short active Public `test` soak passed on `COM7`.
- [x] Post-touch-fix 3-minute idle soak passed on `COM12` with monotonic uptime, ready board/UI/mesh, and empty crashlog after start clear.
- [x] Host-side fix for live-packet UI corruption added: RF-mutated packet/message/DM/node/route/contact stores and mesh-inspector scratch buffers now serialize reads/writes before UI snapshots.
- [x] UI refresh race fix added: touch callbacks request coalesced content refreshes instead of rebuilding the active tab inline, and live data generation changes queue redraws on the UI task.
- [x] Current COM12 targeted UI corruption probe artifact captured from Actions-built `59610ab` / run `28723265336` with tab, button/search, retained-data refresh, health, crashlog, 20 rounds, zero failures, `final_pending=false`, `public_rf_tx=false`, and `formats_sd=false` proof.
- [x] Previous COM12 Home hardware pixel capture artifact captured from PR #33 (`c6a88e2` / PR Actions `28725692751`, merged as `e086312`) with `scripts/ui_capture_d1l.py`, 480x480 RGB565 PNG reconstruction, matching CRC `ED8A8E31`, passing simulator/reference diff, and `public_rf_tx=false` / `formats_sd=false` proof.
- [ ] Fresh COM12 Home pixel capture for the newest PR #37 title-only/full-height icon Home layout.
- [x] Historical COM12 compose keyboard capture artifact captured from Actions-built `59610ab` / run `28723265336` for Public short/long and DM short/long targets with PNG/RGB565 captures, `public_rf_tx=false`, and `formats_sd=false`.
- [x] Expanded issue #2 compose/input keyboard capture from PR #35 (`fce5d82` / Actions `28727064923`) passed on COM12 using `ui_compose_keyboard_capture_d1l.py --targets all`; the artifact reports `ok=true`, `capture_count=12`, `public_rf_tx=false`, and `formats_sd=false` for Public/DM compose, Public search, Packet search, contact edit, onboarding, map location/provider, and Wi-Fi SSID/password.
- [ ] Manual screen photos and physical touch review for the newest COM12 UI stability build.
- [ ] 12-hour idle/listening soak without crash.
- [x] 1-hour active Public messaging soak without UI freeze passed on `COM7`.

## Major Version Release

- [x] Private GitHub repo exists under the user's account.
- [x] CI artifacts produced.
- [x] Firmware binaries and SHA256 checksums attached.
- [x] Release package artifact generation added to GitHub Actions.
- [x] RP2040 SD bridge artifact generation added to GitHub Actions.
- [x] Release package includes normal flash set, app update image, full 8MB image, manifest, checksums, and explicit-port flash helpers.
- [x] Release package includes third-party notices and source attribution documents for upstream material, including the permitted SigurdOS-derived work.
- [x] Top-level MeshCore DeskOS D1L project license selected, committed as `LICENSE`, and included in release package notices.
- [ ] SigurdOS permission evidence archived with date, channel/link, and exact permitted scope.
- [x] First user guide added.
- [x] First developer guide added.
- [x] Known limitations updated.
- [x] Hardware validation notes include exact port, board, and date.
- [x] Host simulator screenshots captured.
- [x] Evidence-based release gate audit script added and uploaded by CI; hardware evidence is metadata-strict for commit-matched UI/scroll/DM/SD/RF/manual/soak artifacts so stale evidence cannot pass a final release audit. The SD gate rejects obsolete format-capable preflight guidance and requires no-device-format evidence (`formats_sd=false`). The keyboard gate is closed by PR #35 / issue #2; the overall audit still intentionally fails closed while RF/DM, SD matrix, manual review, newest Home pixel proof, soak, and remaining UI modular ownership evidence stay open.
- [x] Older COM12 smoke, shorter historical tab stress, scroll probe, outbound DM proof, supplemental route-probe proof, RP2040 preflight, UF2 scan, full RF partial proof, Actions checksums, and packaged notices captured for `a1afd4b` after flashing the verified Actions package from run `28569851955`.
- [x] Current `1a29876` / Actions run `28714355561` COM12/COM16 core SD evidence captured: autonomous validation, official Seeed smoke, COM12 smoke, preflight, raw diagnostic, file canary, safe boot scenarios, RP2040-unavailable fallback, retained-history canary, reboot/remount, map-tile canary, export canary, diagnostic export, and sampled data export all pass without Public RF or formatting.
- [ ] Fresh issue-sized evidence still required for the remaining P0s: #16/#6 owned screen/render modules, residual keyboard widget wiring, deeper single-root invariants, and current Home pixel proof; full RF/DM acceptance; remaining physical SD release matrix; physical photos/manual UI review; and 12-hour soak. The previous Home pixel-capture simulator diff is proven on PR #33 (`c6a88e2` / PR Actions `28725692751`, merged as `e086312`); targeted UI corruption and scroll proof are already proven on `59610ab` / Actions `28723265336`; the all-target keyboard proof is proven by PR #35 / Actions `28727064923`. Do not rerun them for unrelated PRs. Collect one artifact set per issue; save the full sweep for final release readiness.
- [ ] Physical screen photos captured.
- [x] Manual physical UI review artifact helper added: `scripts/manual_ui_review_d1l.py` requires current photos plus explicit display/touch/navigation/workflow confirmations before producing an `ok=true` review JSON.
- [ ] Final full-duration soak evidence added.
- [ ] Final manual touch review added.
