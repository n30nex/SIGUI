# Release Checklist

## Phase 1

- [x] Host tests pass.
- [x] Firmware builds with ESP-IDF v5.1.x.
- [ ] Flash backup captured with SHA256. Skipped for current bring-up per operator instruction.
- [x] D1L flashes using explicit port only.
- [x] Boot banner includes firmware name/version/schema.
- [x] `i2c` detects D1L expander/touch.
- [ ] `display test` shows stable bars. Command passed on hardware; manual visual confirmation is still pending.
- [x] `wifi status`, `wifi scan`, and `ble status` report the supported companion-radio state for the release build.
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
- [x] Top status, home dashboard, bottom dock, packet view, settings view, advert sheet, toast, and lock overlay implemented.
- [x] First persisted onboarding sheet implemented with node name, Canada/USA preset, Desk Companion role, offline radio defaults, and identity generation.
- [x] Touch Public `test` action routes through the app model.
- [x] Phase 3 shell host contract tests pass.
- [x] Phase 3 shell local Podman ESP-IDF build passes.
- [x] Phase 3 shell flashed and smoke-tested on `COM7`.
- [x] Controlled Public `test` RF regression still receives local bot replies.
- [x] Simulator screenshots captured for the main shell views and current modal sheets.
- [x] Simulator layout report passes required-label and text-overflow checks.
- [x] Simulator touch-flow report passes expected-flow, 44x44 touch-target, RF/destructive flag, and no-format storage checks.
- [x] Offline Map tab, serial-configured manual center, and SD tile-cache policy are covered by simulator required-label and flow checks.
- [x] First-open touch Map location picker and persisted D1L pin flow are covered by simulator required-label and flow checks.
- [x] Large simulated mesh UI stress passes with bounded message/node previews.
- [x] Bottom dock tab taps validated on D1L `COM12` after the deferred tab-switch fix; monitored touch samples and crashlog stayed clean.
- [ ] Manual visual review of the physical shell.
- [ ] Manual visual/touch review of the Public composer, DMs, persistent nodes/contacts/routes, and touch radio editing.

## Phase 4 Messaging And Stores

- [x] Bounded NVS-backed Public message store implemented.
- [x] Public TX/RX events append persisted recent message rows.
- [x] `messages public` serial diagnostic added to smoke coverage.
- [x] `messages public search <text>` and bounded Public History/Search UI added to smoke and simulator coverage.
- [x] Public History/Search build flashed, standard-smoked on `COM7`, and targeted with COM11 hardware DM receive proof without Public-channel RF.
- [x] Repeatable DM-only COM11 hardware probe added and passed so Public-channel RF can stay quiet during targeted regressions; `artifacts/hardware/com12/dm_probe_b841621c.json` passed COM12-to-COM11 outbound DM proof with retained DM, packet, route, meshbot receive-counter, health, and no-Public-command checks all true.
- [x] Messages tab reads persisted Public rows from the app snapshot.
- [x] Public message store survives reboot on `COM7`.
- [x] Free-text Public composer implemented, built, flashed, and RF-regression tested.
- [x] Heard-node store survives reboot on `COM7`.
- [x] Contact store promotion from heard node survives reboot on `COM7`.
- [x] Route store survives reboot on `COM7`.
- [x] Read-only `routes trace <fingerprint>` diagnostic and contact-detail Route Trace sheet implemented with retained route/contact evidence.
- [x] Heard-node and contact public-key retention survives reboot on `COM7`.
- [x] Contact favorite/mute flags persist across reboot and are exposed through serial diagnostics plus the contact detail UI.
- [x] Public unread/read state persists across reboot and is exposed through serial diagnostics plus the Messages UI.
- [x] Route detail diagnostics and first Packet-tab route detail sheet are implemented, built, flashed, and Public-RF probed on `COM7`.
- [x] Packet log persists newest 8 rows across reboot and exposes serial/touch packet detail.
- [x] Packet log filter/search/raw-hex developer mode implemented in serial diagnostics and Packet-tab UI.
- [x] Packet log active RAM capacity increased to 128 rows with compact 8-row NVS fallback and split SD/NVS flush foundation.
- [x] First `storage status`/`storage setup` fallback surface implemented: probes the RP2040 SD status protocol, reports protocol/card/root/setup state, never auto-formats, keeps onboard NVS store backends, and has Settings/simulator visibility.
- [x] ESP32 confirmed-format request guard implemented: `DESKOS_SD_FORMAT FORMAT-DESKOS-SD` can only be sent after exact confirmation plus bridge-reported card presence, format support, and setup-required state.
- [x] CI-buildable RP2040 SD bridge target added for `DESKOS_SD_STATUS` and guarded `DESKOS_SD_FORMAT FORMAT-DESKOS-SD`.
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
- [x] Touch `Set Pin`/`Move Pin` Map picker added: it pans/zooms a manual D1L pin, persists through the app model/settings path shared with `map center set`, and does not send Public RF or touch/format SD.
- [x] Non-destructive RP2040 SD bridge preflight added: `scripts/rp2040_sd_bridge_preflight_d1l.py` verifies the Actions UF2 artifact, lists UF2 bootloader volumes, queries only the selected D1L port, records optional `storage diag` probe evidence, and reports the next bridge-flash/SD-acceptance action without Public RF, formatting, or UF2 copying.
- [x] SD boot/use acceptance runner added: `scripts/sd_boot_prepare_acceptance_d1l.py` covers no-card, correct-structure, missing-structure, unformatted, existing-data, and bridge-unavailable scenarios with a non-formatting default and an explicit operator-authorized `--allow-format-confirm` path for the D1L test SD card.
- [x] Actions-built RP2040 SD bridge protocol and no-card fallback hardware-validated on COM12/COM16 from run `28478756887`; follow-up COM12 evidence through `8150b7b` proves RP2040 UART/ping/protocol/diag responsiveness and inserted-card `setup_required` detection without Public RF or formatting.
- [ ] Flash the next verified RP2040 UF2 after the RP2040 is placed in UF2/BOOTSEL mode; latest copied-candidate evidence `artifacts/hardware/com12/rp2040_uf2_volumes_8150b7b_after_esp32_flash.json` reports `candidate_volumes=[]`, and the current follow-up patch must be built by GitHub Actions before the next copy attempt.
- [ ] Optional SD-card data storage implemented: boot detect/validate, user-confirmed format, onboard fallback, SD-backed message/packet/route/export/map-tile stores, reboot/remount proof, and live network tile downloads after Wi-Fi runtime/user opt-in.
- [x] Signal/room-server/repeater mesh visibility commands and summary cards are flashed, smoke-tested, and Public `test` RF-regression tested on `COM7`.
- [x] First touch Mesh Roles browser sheet is built, flashed, smoke-tested, and RF-regression tested on `COM7`.
- [x] Contact export QR-compatible serial command and touch sheet are implemented, host/simulator tested, flashed, smoke-tested, and targeted with a keyed contact on `COM7`.
- [x] Touch Radio Settings editor implemented, host/simulator tested, flashed, smoke-tested, targeted through serial profile changes, and Public-RF-regression tested on `COM7`.
- [ ] Manual contact-export QR scan/import proof with a MeshCore client.
- [x] DM store and serial flood-TX path survive reboot on `COM7`.
- [x] DM ACK/PATH receive parser and direct-route TX backend implemented, built, flashed, and smoke-tested on `COM7`.
- [x] First touch DM composer opens from keyed contact rows and routes through the DM backend.
- [x] First bounded DM thread/detail sheet opens from recent DM rows and offers `Reply`.
- [x] Per-thread DM read cursors are persisted, exposed through serial diagnostics, and surfaced in the DM thread sheet.
- [x] DM thread sheet renders bounded scrollable retained history and `messages dm <fingerprint>` filters one retained thread; host/simulator, COM7 smoke, targeted serial, and active Public RF regression pass.
- [x] Full RF acceptance runner added for the COM12 D1L plus local COM11 Meshcorebot path: `scripts/rf_full_acceptance_d1l.py` writes one release-gate artifact covering identity, Meshcorebot status, outbound DM, controlled inbound DM, ACK/PATH, direct route, health, and no-Public-command checks.
- [ ] Full DM workflow: manual touch review plus a passing newest `artifacts/hardware/com12/rf_full_acceptance_*.json` from the COM11-controlled inbound/ACK/PATH/direct-route proof.

## Phase 7 Polish And Soak

- [x] Crash ring and reset reason diagnostics implemented and hardware-smoke validated.
- [x] Health telemetry reports heap/PSRAM, LVGL, reset reason, board/UI readiness, and task stack watermarks.
- [x] Repeatable idle/active soak runner added with JSON artifact output.
- [x] Short active Public `test` soak passed on `COM7`.
- [x] Post-touch-fix 3-minute idle soak passed on `COM12` with monotonic uptime, ready board/UI/mesh, and empty crashlog after start clear.
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
- [x] First user guide added.
- [x] First developer guide added.
- [x] Known limitations updated.
- [x] Hardware validation notes include exact port, board, and date.
- [x] Host simulator screenshots captured.
- [x] Evidence-based release gate audit script added and uploaded by CI; current local audit reports `ready_for_public_release=false` until all P0 evidence is present.
- [x] COM12 smoke captured for `8150b7b` after flashing the verified Actions package from run `28551321662`; follow-up SD guarded format timed out before the file-operation gate became ready.
- [ ] Current-commit COM12 smoke artifact captured for the final release commit.
- [ ] Physical screen photos captured.
- [ ] Final full-duration soak evidence added.
- [ ] Final manual touch review added.
