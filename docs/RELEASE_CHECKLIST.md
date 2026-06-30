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
- [x] Large simulated mesh UI stress passes with bounded message/node previews.
- [ ] Manual visual review of the physical shell.
- [ ] Manual visual/touch review of the Public composer, DMs, persistent nodes/contacts/routes, and touch radio editing.

## Phase 4 Messaging And Stores

- [x] Bounded NVS-backed Public message store implemented.
- [x] Public TX/RX events append persisted recent message rows.
- [x] `messages public` serial diagnostic added to smoke coverage.
- [x] `messages public search <text>` and bounded Public History/Search UI added to smoke and simulator coverage.
- [x] Public History/Search build flashed, standard-smoked on `COM7`, and targeted with COM11 hardware DM receive proof without Public-channel RF.
- [x] Messages tab reads persisted Public rows from the app snapshot.
- [x] Public message store survives reboot on `COM7`.
- [x] Free-text Public composer implemented, built, flashed, and RF-regression tested.
- [x] Heard-node store survives reboot on `COM7`.
- [x] Contact store promotion from heard node survives reboot on `COM7`.
- [x] Route store survives reboot on `COM7`.
- [x] Heard-node and contact public-key retention survives reboot on `COM7`.
- [x] Contact favorite/mute flags persist across reboot and are exposed through serial diagnostics plus the contact detail UI.
- [x] Public unread/read state persists across reboot and is exposed through serial diagnostics plus the Messages UI.
- [x] Route detail diagnostics and first Packet-tab route detail sheet are implemented, built, flashed, and Public-RF probed on `COM7`.
- [x] Packet log persists newest 8 rows across reboot and exposes serial/touch packet detail.
- [x] Packet log filter/search/raw-hex developer mode implemented in serial diagnostics and Packet-tab UI.
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
- [ ] Full DM workflow: manual touch review, controlled ACK/PATH RF proof, direct-route RF proof, and controlled inbound DM proof.

## Phase 7 Polish And Soak

- [x] Crash ring and reset reason diagnostics implemented and hardware-smoke validated.
- [x] Health telemetry reports heap/PSRAM, LVGL, reset reason, board/UI readiness, and task stack watermarks.
- [x] Repeatable idle/active soak runner added with JSON artifact output.
- [x] Short active Public `test` soak passed on `COM7`.
- [ ] 12-hour idle/listening soak without crash.
- [x] 1-hour active Public messaging soak without UI freeze passed on `COM7`.

## Major Version Release

- [x] Private GitHub repo exists under the user's account.
- [x] CI artifacts produced.
- [x] Firmware binaries and SHA256 checksums attached.
- [x] Release package artifact generation added to GitHub Actions.
- [x] Release package includes normal flash set, app update image, full 8MB image, manifest, checksums, and explicit-port flash helpers.
- [x] First user guide added.
- [x] First developer guide added.
- [x] Known limitations updated.
- [x] Hardware validation notes include exact port, board, and date.
- [x] Host simulator screenshots captured.
- [ ] Physical screen photos captured.
- [ ] Final full-duration soak evidence added.
- [ ] Final manual touch review added.
