# Release Checklist

## Phase 1

- [x] Standalone Map/Wi-Fi and ESP-IDF 5.5.4 branch host suites pass.
- [x] The bounded `727bd23` platform base passed all 461 host tests, including Map simulator, network-suppressed dry-run, strict native SD reply/state, and host-reference wire checks. The current Stage 3 stack requires its own complete host and Actions results below.
- [ ] The combined commit passes the version-pinned Actions firmware/package/checksum/effective-config path without changing the generated lock.
- [x] Historical firmware builds passed with ESP-IDF v5.1.x in GitHub Actions; that EOL baseline is not release-acceptable. Local firmware builds are not part of validation.
- [x] Issue #63 selected-target policy passes: the firmware job uses exact version-pinned `espressif/idf:v5.5.4`, the committed component lock records IDF 5.5.4, and the release audit rejects missing, moving, EOL, unapproved, or stale selections. These configuration checks alone do not qualify the SDK.
- [x] ESP-IDF Component Manager generated `dependencies.lock` in the version-pinned Actions environment; that exact reviewed output is committed unchanged and the qualifying standalone repeat Actions build leaves it unchanged.
- [x] Standalone migration commit `39a043c` passes the complete host suite and version-pinned Actions firmware/package/checksum/release path with resolved IDF identity, dependency lock, effective config, and artifact metadata retained together.
- [ ] The combined Map/Wi-Fi + ESP-IDF 5.5.4 Actions run passes, and its matching artifact is flashed to exact COM12; the P0 flash receipt binds the full commit, numeric run, complete checksum manifest, exact contained offset/file command set, required `0x0`/`0x8000`/`0x10000` roles, successful non-erasing esptool result, and no extra pair. Serial `version` reports `"idf":"v5.5.4"` plus the exact 40-hex `build_commit`, and board/display/touch/Wi-Fi/RF/RP2040-SD/Map/health/reboot/post-power-cycle smoke passes without weakening safety policy. COM16 is used only for an explicitly required RP2040 proof.
- [ ] COM12 smoke records the selected full commit consistently as `expected_firmware_commit`, `device_build_commit`, and `results[].build_commit`; `--skip-esp32-flash`, a stale/wrong-run receipt, a different port, a missing required image, an extra flash pair, or a checksum/path mismatch leaves the release gate failed.
- [ ] Relevant commit-matched release evidence is refreshed and every P0 gate remains green before issue #63 is closed or v5.5.4 is called the qualified production baseline.
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
- [x] Issue #65 has a separate Ubuntu 24.04, recursively checked-out,
  Clang-18 ASan+UBSan/libFuzzer wire-envelope job that gates firmware build
  and uploads a commit-named JSON artifact.
- [ ] The current commit's wire-envelope artifact passes with
  `coverage_level="wire_envelope_only"`, `closure_ready=false`, the exact
  upstream gitlink, 100,000 deterministic fuzz inputs, and zero sanitizer or
  canary failures. This is structural evidence only.
- [ ] Issue #65 full conformance closes only after semantic,
  production-cryptography, duplicate/replay/lifetime, retained-state, and
  real-peer coverage passes for the entire declared MeshCore 1.0 surface; the
  wire-envelope artifact cannot satisfy this item. The separate P0
  `meshcore_full_conformance_complete` release gate remains deliberately failed
  until that distinct closure artifact exists.
- [x] Smoke JSON and monitor logs archived. Persistence smoke snapshots and restores only node name/path-hash, preserves saved Wi-Fi/profile metadata and Wi-Fi/BLE enable intent, and never uses `settings reset` as cleanup.

## Phase 3 UI Shell

- [x] 480x480 dark shell replaces the bring-up tile home.
- [x] Top status, home dashboard, bottom dock, packet view, Settings setup dashboard, advert sheet, toast, and lock overlay implemented.
- [x] Screenshot-wide navigation audit consolidated the shell into Home, Messages, Network, Map, and More; Home exposes four task destinations plus one Device status card, and More uses disclosure categories instead of a flat button grid.
- [x] Modal chrome invariant hides background dock navigation for every sheet, restores it only after the active modal closes, and fails the simulator when any of the 26 modal states exposes a dock target.
- [x] First persisted onboarding sheet implemented with node name, Canada/USA preset, Desk Companion role, offline radio defaults, and identity generation.
- [x] Touch Public `test` action routes through the app model.
- [x] Phase 3 shell host contract tests pass.
- [x] Phase 3 shell firmware build passes in GitHub Actions.
- [x] Phase 3 shell flashed and smoke-tested on `COM7`.
- [x] Controlled Public `test` RF regression still receives local bot replies.
- [x] Simulator screenshots captured for the main shell views and current modal sheets.
- [x] Simulator layout report passes required-label and text-overflow checks.
- [x] Simulator touch-flow report passes expected-flow, 44x44 touch-target, RF/destructive flag, and no-format storage checks.
- [x] Built-in OpenStreetMap Map/Options/Location/Cache surfaces, manual center, fixed attribution, and active-view tile policy are covered by simulator required-label and flow checks.
- [x] The revised host simulator/contract suite passes for default zoom 10, zoom bounds 8 through 14, one-finger pan, direct `-`/`+`/`Center` controls, one zoom per visible generation, completed same-view frame reuse, and protection against unrelated live-data redraw churn.
- [x] First-open touch Map latitude/longitude keyboard entry and persisted D1L pin flow are covered by simulator required-label and flow checks.
- [x] Large simulated mesh UI stress passes with bounded message/node previews.
- [x] Bottom dock tab taps validated on D1L `COM12` after the deferred tab-switch fix; monitored touch samples and crashlog stayed clean.
- [ ] Manual visual review of the physical shell.
- [ ] Manual visual/touch review of the Public composer, DMs, persistent nodes/contact hierarchy/routes, and touch radio editing.
- [ ] #74 replaces the flat Messages surface with a Home-themed two-destination Public/DM root and chat-client conversation pages, then passes simulator, exact COM12 pixels, manual touch, and controlled-peer truthful-delivery proof.
- [ ] #75 renames user-visible Network to Nodes and proves compact heard/type totals plus a scan-friendly, role-truthful node list on simulator and COM12.
- [ ] #76 renames More to Tools/Settings, adds a compact icon-led bottom dock with smaller labels and 44x44 targets, changes Home Network to Nodes, and replaces the large Home Device status block with truthful status icons.
- [ ] #77 implements pinned-protocol authenticated repeater and room-server administration with secret redaction, bounded sessions, confirmed allowlisted mutations, simulator coverage, and controlled compatible-peer hardware proof.

## Phase 4 Messaging And Stores

- [x] Bounded NVS-backed Public message store implemented.
- [x] Public TX/RX events append persisted recent message rows.
- [x] `messages public` serial diagnostic added to smoke coverage.
- [x] `messages public search <text>` and bounded Public History/Search UI added to smoke and simulator coverage.
- [x] Public History and DM Thread retained-history paging implemented with Load Older buttons plus serial `offset <n>` diagnostics and page metadata.
- [x] Message Detail and DM Thread use full-height dockless pages with one Back control, scrollable content, and a sticky full-width Reply; Public text wraps before Technical details and opening a DM thread marks it read without a competing Read button.
- [x] Contact Detail host UI exposes Message only for canonical chat contacts and keeps Contact Options available for all contacts; non-chat/unknown roles have no dead Message control, unknown or malformed roles show a non-clickable Export unavailable row, Route/valid Export/Rename return to Contact Options, and deletion exists only behind a dedicated confirmation callback whose Cancel/Back paths are non-destructive.
- [x] Merged PR #59 / source `d24552e` / Actions `29064260772` was flashed on COM12. Non-destructive page-open probes captured Contact Detail (`4DE99F9D`), Contact Options (`E1728433`), and confirmation-only Forget (`5A6D0604`) with exact firmware/host CRC matches, passing simulator diffs, `public_rf_tx=false`, and `formats_sd=false`; the companion three-round all-tab probe also passed. No removal callback was invoked. Manual Route/Export/Rename touch-return review remains separate.
- [x] PR #60 / source `0b138be` passed Actions `29068006554` and `29068007961`; exact COM12 captures proved Mesh Roles (`63DE54FB`), Rooms (`FD538D71`), and Repeaters (`5C41EE08`) with matching firmware/host CRCs, passing simulator diffs, a clean three-round probe, `public_rf_tx=false`, and `formats_sd=false`.
- [ ] Physically verify Mesh Roles root Back closes to Packets, child Back returns to Mesh Roles, long lists scroll inside their bounded panel, and observation rows perform no action.
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
- [x] Contact favorite/mute flags persist across reboot and are exposed through serial diagnostics plus Contact Options.
- [x] Public unread/read state persists across reboot and is exposed through serial diagnostics plus the Messages UI.
- [x] Route detail diagnostics and first Packet-tab route detail sheet are implemented, built, flashed, and Public-RF probed on `COM7`.
- [x] Packet log persists newest 8 rows across reboot and exposes serial/touch packet detail.
- [x] Packet log filter/search/raw-hex developer mode implemented in serial diagnostics and Packet-tab UI.
- [x] Packet log active RAM capacity increased to 128 rows with compact 8-row NVS fallback and split SD/NVS flush foundation.
- [x] First `storage status`/`storage setup` fallback surface implemented: probes the RP2040 SD status protocol, reports protocol/card/root/setup state, never auto-formats, keeps onboard NVS store backends, and has SD Card tile/sheet plus simulator visibility.
- [x] ESP32 SD format request path removed; `storage setup` is policy/status only and reports `policy="no_device_format"`.
- [x] Host Storage hierarchy separates the full-height read-only root into Card status and scrollable Data locations pages, keeps a fixed no-format footer, uses plain-language guidance instead of raw setup slugs, and exposes no mutating touch callback.
- [x] Merged PR #61 / source `4d9f384` passed push Actions `29081187314` and PR Actions `29081214738`; exact Actions-built COM12 captures proved Storage/Card/Data CRCs `9878ADFB`/`77726394`/`42619B0E`, passing simulator diffs, moved-and-restored Data scrolling, and a clean three-round probe with `public_rf_tx=false` and `formats_sd=false`. This is UI evidence, not a replacement SD-card matrix.
- [x] Host Map hierarchy follows `Map -> Map options -> Set location or Cache status`; OpenStreetMap Standard is built in, no provider/source editor remains, and `(c) OpenStreetMap contributors` is always visible on Map.
- [ ] On the combined candidate, capture the network-suppressed `map|map_options|map_location|map_cache` surfaces on COM12, prove unchanged `map tiles status.network_requests` plus `network_tx=false`/`map_network_requests=false`, then separately prove default zoom 10, one-finger pan, `-`/`+`/`Center` through zooms 8 to 14, no requests during drag, at most one 3x3 plan per committed view, cancelable same-generation wait/resume when SD becomes ready, leave-Map cancellation, completed exact-view Home-to-Map retained-frame reuse without network/SD reread, reboot SD-cache reuse without duplicate downloads, stable heap, useful local-dark street detail, and manual touch/photos.
- [ ] Prove signed peer-advert coordinates render as bounded bright markers with readable names below them, marker-only updates do not acquire/repaint tiles, marker taps open the correct retained Node Detail with `Advert location`, and no UI, status, or release copy claims onboard/local GPS support. Map center remains manual.
- [x] CI-buildable RP2040 SD bridge target added for `DESKOS_SD_STATUS`, `DESKOS_SD_MOUNT`, `DESKOS_SD_DIAG`, and bounded `DESKOS_SD_FILE` operations with no SD formatting command.
- [x] FAT32-only SD policy implemented: users prepare FAT32 cards on a computer, and DeskOS only creates missing `/deskos` folders/manifests on mounted FAT32 media.
- [ ] #78 reproduces and fixes the intermittent inserted-card disappearance/false no-card state, distinguishes bridge/protocol/media/removal causes, and passes exact Actions-built COM12 reboot, Map/cache, removal/reinsertion, idle, and active windows without formatting or reset storms.
- [x] #78 source boundary is covered: only a strict parsed `no_card` reply clears confirmed presence; malformed/error/pending replies cannot masquerade as removal; three stale refreshes fail closed to onboard fallback; Map/Storage copy distinguishes starting, reconnecting, missing, FAT32, and attention states; release artifacts reject stale/presence-stale evidence; and COM12 host tools set DTR/RTS false before opening. Hardware acceptance above remains open.
- [x] Generic RP2040 SD file-operation protocol foundation added for future SD-backed stores: bounded `DESKOS_SD_FILE v=1` stat/read/write/append/delete/rename with path validation, base64url payloads, CRC32 checks, and simulator coverage.
- [x] Retained blob-store abstraction can use the RP2040 SD file protocol for retained Public/DM message history, route history, and packet history when a ready card reports file operations and atomic rename; guarded commits cannot replace data after a detected card-generation change.
- [ ] Exact candidate proves `storage status.retained_nvs` has `marker_ready=true` and is ready on the dedicated 124 KiB `d1l_retained` partition with `migration_error="ESP_OK"`; first use requires both redundant marker slots and the entire newly carved retained region to be erased before initialization. Corrupt, missing-with-data, or future-version markers fail closed without an erase. Scoped legacy retained keys migrate from default NVS only after the new commit succeeds, divergent copies remain preserved and fail closed, all four NVS-mirror errors stay `ESP_OK` immediately after canary writes and after reboot, and controlled reboot returns `route_flush="ESP_OK"`. No whole-default-NVS erase is permitted.
- [ ] #69 adds route-equivalent late-SD read/merge reconciliation and retryable NVS-mirror dirty state for Public, DM, and packet stores. Until then, a card that becomes ready after those stores initialize, or an NVS mirror write that fails after SD succeeds, remains a production-release blocker rather than a complete fallback claim.
- [x] Serial-only SD file-operation proof command added: `storage filecanary` performs guarded temp write, read-back compare, `rename replace=1`, stat, final read, delete, and deleted-stat verification without Public RF or formatting.
- [x] Serial-only retained-history SD acceptance command added: `storage retained-canary <token>` appends synthetic Public/DM/route/packet rows only after all retained-history stores report SD backends, and `scripts/sd_retained_history_acceptance_d1l.py` requires the returned token/fingerprint, exact four sequence IDs, four SD backends, non-empty exact per-store rows, consistent page/total/route counters, and derived no-RF/no-format results before and after a nonce-changing reboot with `route_flush="ESP_OK"`. Echoed query fields alone cannot pass.
- [x] Serial-only diagnostic export SD canary added: `storage export-canary <token>` commits `exports/diagnostics/export-canary-<token>.json` through temp write/read plus atomic rename and `scripts/sd_export_canary_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only diagnostic export SD bundle added: `storage export-diagnostics <token>` commits bounded chunked storage/health/crashlog JSON to `exports/diagnostics/diagnostic-export-<token>.json`, reports map tile cache readiness without bundling actual tiles, and `scripts/sd_diagnostic_export_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only sampled data export SD bundle added: `storage export-data <token>` commits bounded chunked user-data JSON to `exports/data/data-export-<token>.json`, includes recent messages/DMs/routes/packets/contacts/nodes/read-state, excludes private identity material, and `scripts/sd_data_export_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only map tile cache SD canary added: `storage map-tile-canary <token>` commits `map/tiles/z12/x1/y2-<token>.tile` through temp write/read plus atomic rename and `scripts/sd_map_tile_canary_d1l.py` proves the path without Public RF or formatting.
- [x] Serial-only SD reboot/remount acceptance runner added: `storage map-tile-check <token>` verifies the previous synthetic tile read-only after remount, and `scripts/sd_reboot_remount_acceptance_d1l.py` proves retained Public/DM/routes/packets plus map-tile cache readback and SD backends before and after a nonce-changing reboot with `route_flush="ESP_OK"`, without Public RF or formatting. Generic remount errors fail even when cached fields look ready; only manager-busy followed by fresh `READY_SD` convergence is accepted. The release audit rejects missing/non-success route-flush, remount, or retained-backend evidence.
- [x] Map cache foundation, serial `storage map-policy`, and `map center` commands added: they expose `map/tiles/z{z}/x{x}/y{y}.tile`, current cache readiness/backend, and optional manual center without Public RF or formatting.
- [x] Touch `Set Pin`/`Move Pin` Map entry added: it uses decimal latitude/longitude fields with an onscreen keyboard, persists through the app model/settings path shared with `map center set`, and does not send Public RF or touch/format SD.
- [x] Non-destructive RP2040 SD bridge preflight added: `scripts/rp2040_sd_bridge_preflight_d1l.py` verifies the Actions UF2 artifact, lists UF2 bootloader volumes, queries only the selected D1L port, records optional `storage diag` probe evidence, and reports the next bridge-flash/SD-acceptance action without Public RF, formatting, or UF2 copying.
- [x] SD boot/use acceptance runner added: `scripts/sd_boot_prepare_acceptance_d1l.py` covers no-card, correct-structure, missing-structure, unformatted, existing-data, and bridge-unavailable scenarios with no format command path.
- [x] Actions-built RP2040 SD bridge protocol and no-card fallback hardware-validated on COM12/COM16 from run `28478756887`; follow-up COM12 evidence through `8150b7b` proves RP2040 UART/ping/protocol/diag responsiveness and inserted-card `setup_required` detection without Public RF or formatting.
- [x] Actions-built RP2040 bridge restored after official Seeed SD smoke on COM16; current COM12 preflight from `1a29876` / Actions `28714355561` reports the FAT32 card ready for file operations without Public RF or formatting.
- [ ] Optional SD-card data storage implemented: boot detect/validate, FAT32-only auto-provision without device formatting, onboard fallback, SD-backed message/packet/route/export/map-tile stores, and reboot/remount proof. The synthetic `storage map-tile-canary` remains a storage-only test and never performs network I/O. Production network fetch is a separate guarded path: only while the actual Map is visible, at most its visible current-view 3x3 at one zoom per visible generation, with tile-cache reuse, completed same-view retained-frame reuse, and fixed attribution. There is no area download, background fetch, multi-zoom prefetch, off-screen batch, or arbitrary URL command. Current COM12/COM16 evidence proves the installed FAT32 card path and synthetic cache canary; revised live Map controls/network/render/heap proof remains pending alongside the physical SD matrix and power/electrical evidence.
- [x] Signal/room-server/repeater mesh visibility commands and summary cards are flashed, smoke-tested, and Public `test` RF-regression tested on `COM7`.
- [x] The earlier flat Mesh Roles browser was built, flashed, smoke-tested, and RF-regression tested on `COM7`.
- [x] The replacement full-height Mesh Roles root/Rooms/Repeaters hierarchy has exact PR #60 Actions/COM12 page captures; physical Back/scroll review remains open.
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
- [x] COM12 Home pixel capture for the prior PR #40 small-title/full-screen icon Home layout captured from Actions-built `b2ca00a` / run `28731948363`: `ui_pixel_capture_b2ca00a_actions_28731948363_COM12.json` passed with 480x480 PNG reconstruction, CRC `0DB2C27A`, simulator diff `ok=true`, `public_rf_tx=false`, and `formats_sd=false`.
- [x] COM12 Home pixel capture for the tightened PR #51 layout captured from Actions-built `485744e` / run `29055441069`: `ui_pixel_capture_485744e_actions_29055441069_COM12.json` passed with 480x480 RGB565 reconstruction, matching firmware/host CRC `BB15A654`, simulator diff `ok=true`, `public_rf_tx=false`, and `formats_sd=false`.
- [x] COM12 Home pixel capture for the PR #56 five-destination hierarchy captured from Actions-built `51258ba` / run `29060900359`: `ui_pixel_capture_51258ba_actions_29060900359_COM12.json` passed with 480x480 RGB565 reconstruction, matching firmware/host CRC `E72745BA`, simulator diff `ok=true`, `public_rf_tx=false`, and `formats_sd=false`; a three-round all-tab probe also passed with zero failures and empty crashlog.
- [x] Historical COM12 compose keyboard capture artifact captured from Actions-built `59610ab` / run `28723265336` for Public short/long and DM short/long targets with PNG/RGB565 captures, `public_rf_tx=false`, and `formats_sd=false`.
- [x] Expanded issue #2 compose/input keyboard capture from PR #35 (`fce5d82` / Actions `28727064923`) passed the historical 12-input set on COM12. The provider keyboard is removed with the built-in map source; the active `--targets all` contract now covers the retained 11 inputs and new artifacts report `capture_count=11`, `network_tx=false`, `public_rf_tx=false`, and `formats_sd=false`.
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
- [x] Evidence-based release gate audit script added and uploaded by CI; hardware evidence is metadata-strict for commit-matched UI/scroll/DM/SD/RF/manual/soak artifacts so stale evidence cannot pass a final release audit. A matching filename is never sufficient: at least one canonical commit claim is required and every recognized claim must agree. A downloaded package requires an explicit selected run ID plus that same run's exact-commit host-success marker, emitted only after all host steps pass; the package job is also blocked when host checks fail. The SD gate rejects obsolete format-capable preflight guidance and requires no-device-format evidence (`formats_sd=false`). The keyboard gate is closed by PR #35 / issue #2, and PR #56 closes current hierarchy Home pixel/navigation proof. The SDK gate checks the exact selected workflow tag and committed lock target, but does not prove lock provenance or replace issue #63 clean-build, COM12 `version.idf`, behavioral, or refreshed-evidence qualification. The overall audit still intentionally fails closed while SDK qualification, RF/DM, SD matrix, manual review, soak, and remaining UI modular ownership evidence stay open.
- [x] Older COM12 smoke, shorter historical tab stress, scroll probe, outbound DM proof, supplemental route-probe proof, RP2040 preflight, UF2 scan, full RF partial proof, Actions checksums, and packaged notices captured for `a1afd4b` after flashing the verified Actions package from run `28569851955`.
- [x] Current `1a29876` / Actions run `28714355561` COM12/COM16 core SD evidence captured: autonomous validation, official Seeed smoke, COM12 smoke, preflight, raw diagnostic, file canary, safe boot scenarios, RP2040-unavailable fallback, retained-history canary, reboot/remount, map-tile canary, export canary, diagnostic export, and sampled data export all pass without Public RF or formatting.
- [ ] Fresh issue-sized evidence remains for the combined ESP-IDF/Map/Wi-Fi qualification; MeshCore conformance; reliable DM ACK/retry/delivery state and replay control; contact/channel parity; real PATH/trace and route fallback; service/UI ownership, write coalescing, schema recovery, and truthful time/roles; physical Map/RF/SD/UI matrices; idle and active soaks; packaging; and the final exact-commit release gate. PR #61 already closes Storage/Card/Data pixel proof; PR #60 proves Mesh Roles, PR #59 contacts, PR #56 Home, and PR #35 retained keyboard callers. Do not rerun closed proof for an unrelated pixel-neutral slice.
- [ ] Physical screen photos captured.
- [x] Manual physical UI review artifact helper added: `scripts/manual_ui_review_d1l.py` requires current photos plus explicit display/touch/navigation/workflow confirmations before producing an `ok=true` review JSON.
- [ ] Final full-duration soak evidence added.
- [ ] Final manual touch review added.
