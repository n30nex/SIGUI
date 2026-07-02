# D1L Test Plan

## Host Tests

Run:

```powershell
python -m pytest tests
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large
python .\tools\ui_simulator.py --scenario storage-states --out artifacts\ui-sim-storage
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\ui_tab_abuse_d1l.py --dry-run --cycles 100
python .\scripts\scroll_probe_d1l.py --dry-run --screens messages,nodes,packets,settings,map
python .\scripts\probe_d1l_dm.py --dry-run
python .\scripts\sd_reboot_remount_acceptance_d1l.py --dry-run --token dryrun
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --sample-storage --sd-file-canary --allow-sd-unavailable
python .\scripts\release_gate_audit_d1l.py --out artifacts\release-gate\d1l-release-gate-audit-local.json
```

Coverage:

- Canada/USA radio defaults.
- D1L pin map and TCXO `NONE`.
- RP2040 bridge pin contract.
- No hardcoded executable COM ports.
- Smoke JSONL parser.
- Targeted DM-only hardware probe can validate D1L-to-bot DM paths without Public-channel RF when the operator assigns both ports.
- Flash/monitor scripts require an explicit port.
- Backup command builder.
- Checksum verifier.
- MeshCore 3-byte companion transport codec.
- NVS settings contract and default-off Wi-Fi/BLE/observer policy.
- Phase 5/Phase D connectivity contract: `wifi status`, `wifi scan`, `wifi save <ssid> [password]`, `wifi connect`, `wifi clear`, `wifi on|off`, `ble status`, and `ble on|off` must be machine-readable. `wifi save` must persist SSID/password intent without printing the password, `wifi scan` must return bounded network records only when Wi-Fi is enabled and compiled in, `wifi connect` must never echo the password, `wifi clear` must remove the saved profile and disable Wi-Fi intent, and BLE remains gated until a measured runtime is enabled.
- Phase 7 diagnostics contract: `crashlog` must return a bounded persisted reset ring, `crashlog clear` must clear it, and `health` must include heap/PSRAM largest blocks, task stack watermarks, LVGL usage, reset reason, and board/UI readiness.
- Phase 7 soak harness contract: `scripts/soak_d1l.py` must have a dry-run path, must sample `health`, `mesh status`, `signal`, `messages unread`, `packets`, and `crashlog`, and must summarize uptime monotonicity, readiness, packet deltas, heap/PSRAM deltas, stack floors, LVGL peak usage, command retries, and crash-like reset entries. With `--sample-storage`, it must also sample `storage status` and summarize SD state/backend stability plus store backend stability. With `--sd-file-canary`, it must also run `storage filecanary`; pre-RP2040-flash `ESP_ERR_NOT_SUPPORTED` preflight refusals are accepted only when `--allow-sd-unavailable` is explicitly set. The SD-aware soak must report `public_rf_tx=false` and `formats_sd=false` unless the operator explicitly enables an RF mode through a separate flag.
- Phase 8 release package contract: `scripts/package_release_d1l.py` must emit a normal flash set, app update image, full 8MB image, manifest, SHA256SUMS, README, and explicit-port flash helpers.
- Release gate audit contract: `scripts/release_gate_audit_d1l.py` must fail closed when P0 production evidence is missing, must not require hardware or ports in CI, must reject obsolete SD preflight evidence that recommends any device-format action, must require SD evidence to report `formats_sd=false`, and must report `ready_for_public_release=false` until current-commit smoke, SD matrix, 12-hour soak, manual physical UI/photos, and full RF/DM evidence are present.
- UI simulator contract: `tools/ui_simulator.py` must render deterministic 480x480 PNGs plus schema-v2 `ui-sim-report.json`, cover the main touch surfaces, the Settings setup dashboard, Public History/Search sheets, advert sheet, first-boot onboarding, lock overlay, Map, manual Map center, Map Tiles provider/download sheet, Storage/Radio/Contact/Packet/Mesh Roles sheets, fail on missing required labels or measured text overflow, emit a touch-target map with expanded 44x44 boxes, flag RF/destructive/format-capable actions, keep `formats_sd=false` for storage setup and map tile download actions, and include `large-mesh` and `storage-states` scenarios that prove oversized node/message stores and storage copy fit before rendering.
- P0 UI hardware-script contract: `scripts/ui_tab_abuse_d1l.py --dry-run` and `scripts/scroll_probe_d1l.py --dry-run` must stay host-only, explicit-port for hardware mode, and must exercise `ui status` plus `ui tab <home|messages|nodes|map|packets|settings>` without hardcoded COM ports.
- First-boot onboarding contract: settings schema v6 must persist `onboarding_complete`, optional manual map center, saved Wi-Fi profile metadata, and optional map tile provider/attribution/zoom metadata, migrate schema v2/v3/v4/v5 settings without dropping identity, expose `settings onboarding status|complete|reset`, and present a blocking touch setup sheet until onboarding is complete.
- Map location contract: first Map open with no saved center must show a touch `Set D1L Location` sheet with decimal latitude/longitude fields and an onscreen keyboard, the user must be able to Save/Clear/Skip without dock overlap, the saved pin must reappear as `Move Pin`/manual center on the next Map open, and the serial `map center set|clear` commands must share the same app-model/settings persistence path without Public RF, SD writes, or formatting.
- Phase 6 packet filter/raw-hex contract: packet log entries must carry a bounded raw hex preview, expose `packets filter`, `packets search`, `packets raw`, and render Packet-tab filter/search/raw-hex UI surfaces in the simulator.
- Live RF receive UI stability contract: packet, Public message, DM, node, route, contact, and mesh-inspector stores must serialize RAM ring/static scratch reads and writes so UI snapshots cannot copy partially mutated packet-receive state. The periodic UI refresh timer must update chrome/status only and must not redraw active page content during live packet arrival.
- Phase 6 mesh visibility contract: `signal`, `roomservers`, and `repeaters` must be machine-readable, read from bounded packet/route/node stores, avoid new NVS writes, and appear in the smoke command list.
- Phase 6 contact export contract: promoted contacts with retained 64-hex public keys must export MeshCore-compatible `meshcore://contact/add?...` URIs through serial and a touch Contact Export QR sheet, with no failure from the smokeable list form when no contact is available.
- Phase 6 radio settings contract: `settings get` and `radio get` must expose the persisted radio profile, serial `radio set txpower` and `radio set rxboost` must validate and persist safe values without live RF apply, and the Settings tab must open a simulator-covered Radio Settings sheet with staged edits, US/CAN defaults, explicit Save, and reboot/apply warning.
- Optional SD-card data storage contract: `rp2040 ping` must prove the flashed RP2040 bridge app answers without touching SD (`sd_touched=false`, `public_rf_tx=false`, `formats_sd=false`) before SD-specific checks. `storage status` must be safe for boot/UI polling and must not probe, mount, format, or write SD; before an explicit mount it may report `state="mount_required"`, while an explicit async mount is running it may report `state="mount_pending"`, and after a mount/file result it must report the cached fallback backend, direct ESP32 SD support, RP2040 bridge/protocol/card/root state, per-store backend labels, setup action, and cached bridge probe fields such as `probe_power`, `probe_mode`, `probe_error`, `probe_data`, `mount_error`, and `mount_data`. `storage mount` is the explicit SD-touch path; it may return immediately with `state="mount_pending"`, may mount a usable FAT32 filesystem and create `/deskos/manifest.json` plus required DeskOS directories, must report `public_rf_tx=false` and `formats_sd=false`, and must not format. Unmountable cards must report `needs_fat32`/`prepare_fat32_on_computer` style guidance and keep NVS fallback active; when a card is independently confirmed FAT32, that result is a firmware-side mount blocker until resolved. `storage diag` must issue only the non-formatting `DESKOS_SD_DIAG` probe and report high/dedicated, high/shared, low/dedicated, and low/shared results with `public_rf_tx=false` and `formats_sd=false`. `storage setup` must be non-destructive and report `policy="no_device_format"`. The RP2040 bridge file protocol must preserve the `DESKOS_SD_FILE v=1` line grammar, sanitized relative paths, CRC32-checked base64url payloads, 512-byte line cap, and 192-byte chunk cap. Retained Public/DM message history, route history, and packet history may report `sd` backends only when the bridge reports ready data, file operations, matching limits, and atomic rename; NVS remains mirrored as fallback. Diagnostic and sampled data exports may report `export_backend="sd_diagnostic_exports_ready"` only when the same file-operation gate is ready, and map tile cache may report `map_tile_backend="sd_map_tiles_ready"` behind that same file gate. `storage map-tile-download <z> <x> <y> <url-template> <attribution>` and the touch Map Tiles `Download` action must require connected Wi-Fi, a ready SD cache, an allowed HTTPS provider template that is not a public OSM bulk endpoint, attribution metadata, bounded response bytes, and `public_rf_tx=false`/`formats_sd=false`.
- RP2040 bridge preflight contract: `scripts/rp2040_sd_bridge_preflight_d1l.py --port <D1L_PORT> --artifact-dir <rp2040-sd-bridge-firmware>` must verify the Actions-built UF2 artifact when supplied, list UF2 bootloader candidate volumes, query only `rp2040 status`, `rp2040 ping`, safe `storage status`, explicit non-formatting `storage mount`, bounded safe-status polling while `state="mount_pending"`, optional `storage diag`, and `health` on the selected D1L serial port, and emit `public_rf_tx=false`, `formats_sd=false`, `copies_uf2=false`, `rp2040_ping_ok`, `storage_mount_ok`, `storage_diag_ok`, `ready_for_sd_acceptance`, and a machine-readable next action such as `put_rp2040_in_uf2_bootloader`, `dry_run_then_copy_rp2040_uf2`, `run_storage_mount`, `wait_for_storage_mount_or_reset_rp2040_bridge`, `inspect_rp2040_sd_mount_timeout_and_reset_bridge`, `prepare_fat32_card_on_computer_or_swap_known_good_sd_card`, or `run_sd_file_and_export_acceptance`.
- SD boot/use acceptance contract: `scripts/sd_boot_prepare_acceptance_d1l.py --port <D1L_PORT> --scenario <scenario>` must cover `no-card`, `correct-structure`, `missing-structure`, `unformatted`, `existing-data`, and `rp2040-unavailable` without hardcoded ports, Public RF, or any formatting command. Users must supply FAT32 cards prepared on a computer.
- Phase 2 MeshCore service command surface.
- Phase 4 Public message store contract including retained-history search, DM store contract including thread-filtered retained history, unread/read-state contract including per-thread DM read cursors, heard-node store contract, contact store contract, route store contract, persistent packet log contract, Public composer UI contract, and serial diagnostics.

## Hardware Smoke

Run only when the D1L port is known:

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
python .\scripts\ui_tab_abuse_d1l.py --port $env:D1L_PORT --cycles 100 --clear-crashlog-before-start
python .\scripts\scroll_probe_d1l.py --port $env:D1L_PORT --screens messages,nodes,packets,settings,map --manual-touch --clear-crashlog-before-start
```

Expected commands:

- `version`
- `board`
- `settings get`
- `settings onboarding status`
- `identity status`
- `i2c`
- `display test`
- `touch test`
- `button`
- `radiohw`
- `radio get`
- `map center`
- `mesh status`
- `companion status`
- `wifi status`
- `wifi scan`
- `ble status`
- `rp2040 status`
- `storage status`
- `storage map-policy`
- `storage setup`
- `packets`
- `packets filter any any`
- `packets search test`
- `messages public`
- `messages public offset 8`
- `messages public search test`
- `messages public search test offset 8`
- `messages dm offset 8`
- `messages dm <fingerprint> offset 8`
- `messages unread`
- `nodes`
- `contacts`
- `contacts export`
- `routes`
- `routes trace 0BF0A701D5AE2DB6`
- `signal`
- `roomservers`
- `repeaters`
- `crashlog`
- `health`

Hardware success must include manual confirmation for the display/touch test until automated screen capture exists.

## Hardware Soak

Use the soak runner for Phase 7 stability evidence after smoke passes. The runner writes a JSON artifact under `artifacts/soak` unless `--out` is supplied.

Short active RF probe:

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 180 --sample-interval-sec 45 --active-public-text test --active-interval-sec 60 --require-rx-delta --min-tx-delta 1
```

Full idle/listening acceptance window:

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 43200 --sample-interval-sec 300 --out artifacts\soak\d1l-soak-idle-12h-COMx.json
```

Full active messaging acceptance window, assuming the local Public bots are available and respond to `test`:

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 3600 --sample-interval-sec 60 --active-public-text test --active-interval-sec 120 --require-rx-delta --min-tx-delta 1 --clear-crashlog-before-start --out artifacts\soak\d1l-soak-active-1h-COMx.json
```

Success requires every sampled command to return `ok=true` after bounded retries, no unrecovered command retries, no uptime rollback, `board_ready=true`, `ui_ready=true`, ready mesh state, nonzero task stack watermarks, zero crash-like reset entries, and no required packet delta threshold failures. For active RF probes, `mesh_tx_packet_delta` must increase and `mesh_rx_packet_delta` must increase when `--require-rx-delta` is used.

SD-aware passive validation, with no Public RF and no format request:

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 300 --sample-interval-sec 60 --sample-storage --sd-file-canary --allow-sd-unavailable --out artifacts\soak\d1l-passive-soak-sd-aware-COM12.json
```

Before the RP2040 bridge file protocol is flashed, success may include `sd_file_canary_unavailable_count > 0` only because `--allow-sd-unavailable` was set. After the RP2040 bridge is flashed with a ready card, rerun without `--allow-sd-unavailable`; success then requires `storage_file_ops_ready_all=true` and every `storage filecanary` sample to pass.

## Release Package

After downloading the matching GitHub Actions ESP32 firmware build artifact:

```powershell
python .\scripts\package_release_d1l.py --build-dir build --out-dir artifacts\release --package-name d1l-release-local-smoke
```

Verify:

1. `manifest.json` lists bootloader offset `0x0`, partition offset `0x8000`, and app offset `0x10000`.
2. `firmware/meshcore_deskos_d1l.bin` and `update/meshcore_deskos_d1l-app.bin` have matching SHA256 hashes.
3. `full-flash/meshcore_deskos_d1l-full-8mb.bin` is exactly 8MB.
4. `SHA256SUMS.txt` includes every package file except itself.
5. `flash_project.ps1`, `flash_project.sh`, and `flash_full_8mb.ps1` require an explicit D1L port.
6. `flash_full_8mb.ps1` requires typed confirmation.

## Message Store Persistence

For Phase 4 Public message-store validation:

1. Run `messages clear`.
2. Run `mesh send public test`.
3. Wait for a local MeshCore bot response.
4. Verify `messages public` contains at least one TX row and one RX row.
5. Verify `messages public search test` returns `filtered=true` and only retained rows whose author, direction, or text matches `test`.
6. When more than one serial page is retained, verify `messages public offset 8` and `messages public search test offset 8` report page metadata (`offset`, `page_size`, `total_matches`, `has_older`, `next_offset`) and return older retained rows in chronological order.
7. Reboot.
8. Verify `messages public` retains the rows and `packets` either retains the newest packet evidence rows or starts a new evidence window if `packets clear` was run for the packet-log test.

## Unread State

For Phase 4 unread/read-state validation:

1. Run `messages read all`.
2. Verify `messages unread` reports `public_unread=0`, `dm_unread=0`, and `muted_dm_unread=0`.
3. Run `mesh send public test`.
4. Wait for a local MeshCore bot response.
5. Verify `messages public` contains fresh RX rows with seq values greater than the baseline `newest_public_rx_seq`.
6. Verify `messages unread` reports `public_unread` greater than zero and advances `newest_public_rx_seq`.
7. Run `messages read public`.
8. Verify `messages unread` reports `public_unread=0`.
9. Reboot.
10. Verify `messages unread` still reports `public_unread=0` and `health` reports `board_ready=true` and `ui_ready=true`.
11. For DM-thread read-state validation, when a DM thread has an inbound RX row, run `messages unread`, note the thread entry under `dm_threads`, run `messages read dm <fingerprint>`, and verify only that thread's unread count clears while other unread DM threads remain counted.
12. For physical touch review, open the Messages tab, verify new RX rows are highlighted as `new`, tap global `Read`, and verify the unread count clears; then open a DM thread and verify its `Read` action clears only that thread.
13. For muted DM behavior when an inbound DM source is available, mute that contact, receive a DM, and verify the unread row is counted under `muted_dm_unread` rather than audible `dm_unread`.

## DM Store And Serial TX

For Phase 4 direct-message store validation:

Operator note: the other local MeshCore bot may be used as the controlled DM RF target for production validation. Use the targeted DM path when Public-channel RF should stay quiet.

1. Verify `contacts` contains a promoted contact with a full 64-hex `public_key`.
2. Run `messages dm clear`.
3. Run `mesh send dm <fingerprint> <text>`.
4. Verify `messages dm` contains a TX row with the contact fingerprint, alias, text, `direction="tx"`, `persisted=true`, and a nonzero `ack_hash`.
5. Verify `messages dm <fingerprint>` returns `filtered=true`, the same fingerprint, and only rows for that retained thread.
6. When more than one serial page is retained, verify `messages dm offset 8` and `messages dm <fingerprint> offset 8` report page metadata and return older retained rows in chronological order.
7. If the target contact is the local COM11 Meshcorebot, verify its status counters include fresh `rx_contact_total` movement. Use this targeted DM path instead of Public-channel RF when the operator asks to keep Public quiet.
8. If the peer emits MeshCore ACK/PATH returns, verify `messages dm` marks the TX row `acked=true` and `contacts` shows `out_path_known=true`.
9. Send a second DM to the same contact and verify `routes` records `kind="dm_text"`, `direction="tx"`, and `route="direct"`.
10. Reboot.
11. Verify `messages dm` and `messages dm <fingerprint>` retain the TX row and `health` reports `board_ready=true`, `ui_ready=true`, and increasing uptime.

Repeatable local COM11 DM proof:

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\probe_d1l_dm.py --port $env:D1L_PORT --bot-status F:\Meshcorebot\logs\meshcorebot.status.json --bot-port COM11 --out artifacts\smoke\d1l-dm-probe-COM12-COM11.json
```

Success requires `public_rf_transmit=false`, no command beginning with `mesh send public`, `mesh send dm` returning `ok=true`, `messages dm <fingerprint>` and `packets search <token>` retaining the token, `routes trace <fingerprint>` matching the target, `health` ready, and the COM11 Meshcorebot status showing at least `rx_contact_total +1`.

Repeatable full COM11-controlled RF acceptance:

```powershell
$env:D1L_PORT = "COM12"
python .\scripts\rf_full_acceptance_d1l.py --port $env:D1L_PORT --commit <short> --token rf_accept_<short> --bot-status F:\Meshcorebot\logs\meshcorebot.status.json --bot-port COM11 --out artifacts\hardware\com12\rf_full_acceptance_<short>.json
```

Keep the runner attached only to the D1L serial port. When it prints the
`+dm <D1L public key> rf_accept_<short>_in` Discord command, send that command
through the Meshcorebot control channel, the other Meshbot, or a separate
allowlisted Discord sender to create the controlled inbound DM. Do not open the
COM11 Meshcorebot serial port directly, and do not use Meshcorebot's own bot
token because its runtime ignores its own messages. Success requires one
complete newest `rf_full_acceptance_*.json` hardware artifact with
`identity_public_key_matches`, `meshbot_on_expected_port`, `outbound_dm`,
`inbound_dm`, `ack_path`, `direct_route`, `health_ready`, and
`no_public_commands` all true, plus `public_rf_transmit=false`.

## Touch DM Compose

For Phase 4 touch direct-message compose validation:

1. Verify `contacts` contains at least one contact with a full 64-hex `public_key`.
2. Open the Nodes tab and verify the keyed contact row exposes a `DM` action.
3. Tap `DM`, type a short message on the compose keyboard, and tap `Send`.
4. Verify the toast reports the DM queued.
5. Verify `messages dm` contains a TX row for the same contact and message text with a nonzero `ack_hash`.
6. If physical touch cannot be observed in the current test session, run the backend precondition probe by sending the same target through `mesh send dm <fingerprint> <text>` and recording `contacts`, `messages dm`, and `health`.

For Phase 4 touch direct-message thread validation:

1. Verify `messages dm` contains at least one row for a contact with a full public key.
2. Open the Messages tab and tap the DM preview row.
3. Verify the DM thread sheet opens with the contact alias, fingerprint metadata, retained rows for the same fingerprint, and `Reply`/`Read`/`Close` actions.
4. Tap `Reply`, type a short message, and tap `Send`.
5. Verify `messages dm` contains the new TX row for that fingerprint and `health` remains `board_ready=true` and `ui_ready=true`.
6. If the long hardware validation session causes `ESP_ERR_NVS_NOT_ENOUGH_SPACE`, erase only the NVS partition from the current partition table and rerun smoke before continuing persistence checks.
7. For Phase 5/Phase D connectivity validation, run smoke with `--persistence-test` and verify `settings get`, `wifi status`, `wifi scan`, and `ble status` report Wi-Fi/BLE disabled after reboot; then, on the downloaded Actions artifact only, explicitly test `wifi on`, `wifi scan`, and `wifi connect` with a local 2.4 GHz network when hardware/network validation is in scope.
8. For Phase 7 diagnostics validation, run `crashlog clear`, `reboot`, then verify `crashlog` contains a new `SW` reset entry and `health` reports nonzero stack watermarks with `board_ready=true` and `ui_ready=true`.

## Heard Node Store

For Phase 4 heard-node validation:

1. Run `nodes clear`.
2. Wait for or trigger a signed MeshCore advert from a local node.
3. Verify `nodes` reports `active_capacity=64`, `capacity=64`, `sd_history_capacity=512`, `sort="last_heard"`, and at least one row with fingerprint, full 64-hex `public_key`, name or fingerprint fallback, `display_name`, `type`, production `role`, RSSI/SNR, path metadata, `favorite`, `keyed`, `reachable`, and `persisted=true`. NVS remains the compact fallback and retains the newest 16 heard-node rows until SD node history is implemented.
4. Reboot.
5. Verify `nodes` retains the row and its `public_key`.
6. Verify the host contracts cover `d1l_node_store_query()` filters for companions, repeaters, room servers, sensors, favorites, keyed-only, reachable-only, and sort modes for last heard, signal, name, role, and favorites.
7. Until the touch sort/filter sheet is complete, use serial `nodes`, `repeaters`, `roomservers`, `contacts set <fingerprint> favorite <0|1>`, and the UI simulator large-mesh view as the production proof that the query foundation and visual summaries agree.

## Contact Store

For Phase 4 contact-store validation:

1. Run `contacts clear`.
2. Verify `contacts` reports `count=0`.
3. Verify `nodes` contains a heard node with a 16-hex fingerprint.
4. Run `contacts add <fingerprint>`.
5. Verify `contacts add` reports `source="heard_node"`.
6. Verify `contacts` contains the promoted alias, full 64-hex `public_key`, heard name, type, RSSI/SNR, path metadata, `out_path_known`, `out_path_len`, and `persisted=true`.
7. Run `contacts rename <fingerprint> <alias>` and verify `contacts` shows the new alias with `persisted=true`.
8. Reboot.
9. Verify `contacts` retains the renamed row and its copied `public_key`.
10. If deleting a promoted contact is in scope for the test unit, run `contacts delete <fingerprint>` and verify `contacts` removes only the contact row while retained DM/message/route/packet history remains available.

For Phase 4 contact detail and favorite/mute validation:

1. Verify `contacts` contains at least one promoted contact.
2. Run `contacts set <fingerprint> favorite 1`.
3. Run `contacts set <fingerprint> mute 1`.
4. Verify `contacts` shows `favorite=true` and `muted=true`.
5. Reboot and verify both flags are still true.
6. Run `contacts set <fingerprint> favorite 0` and `contacts set <fingerprint> mute 0`.
7. Verify `contacts` shows both flags false.
8. For physical touch review, open the Nodes tab, tap the contact row, verify the detail sheet shows signal/key/path metadata, and verify `Fav`, `Mute`, `DM`, `Edit`, and `Close` actions respond. In `Edit`, verify alias save updates only the contact alias and `Forget` removes only the promoted contact.

For Phase 6 contact export validation:

1. Verify `contacts` contains at least one promoted contact with a full 64-hex `public_key`.
2. Run `contacts export` and verify it returns `ok=true`, `format="meshcore://contact/add"`, and a list of entries whose `shareable` field reflects public-key availability.
3. Run `contacts export <fingerprint>` for the keyed contact.
4. Verify `meshcore_uri` starts with `meshcore://contact/add?`, includes URL-encoded `name`, the 64-hex `public_key`, and the correct numeric MeshCore `type`.
5. Open the contact detail sheet, tap `Export`, and verify the Contact Export sheet shows a MeshCore QR plus URI metadata without crashing or hiding the UI.
6. If a MeshCore phone/client is available, scan the QR and verify it imports the same contact name/public key/type.

## Radio Settings

For Phase 6 radio settings validation:

1. Run `radio get` and verify the default Canada/USA profile reports 910.525 MHz, BW62.5, SF7, CR5, 20 dBm, RX boost enabled, TCXO `NONE`, and `applied_to_radio=false`.
2. Run `radio set txpower 19`, then `radio get`, and verify `tx_power_dbm=19` with `persisted=true` and `applied_to_radio=false`.
3. Run `radio set rxboost 0`, then `settings get`, and verify the nested `radio.rx_boost=false` field is present.
4. Run `radio set preset uscan`, then `radio get`, and verify the US/CAN defaults are restored before any RF regression.
5. Open Settings, tap `Radio`, change at least one staged value, tap `US/CAN`, tap `Save`, and verify the Radio Settings sheet stays readable and reports that reboot/apply is required.
6. Verify `health` remains `board_ready=true` and `ui_ready=true`.

## Route Store

For Phase 4 route-store validation:

1. Run `routes clear`.
2. Verify `routes` reports `count=0`.
3. Run `mesh send public test`.
4. Wait for a local MeshCore bot response.
5. Verify `routes` contains Public TX and RX rows with route name, direction, path hash bytes, hops, confidence, RSSI/SNR, payload length, and `persisted=true`.
6. Pick a fresh route `seq` and run `routes detail <seq>`.
7. Verify the detail response matches the selected route row, including target, kind, route, direction, path metadata, signal metadata, and payload length.
8. Reboot.
9. Verify `routes` retains the rows.
10. For physical touch review, open the Packet tab, tap a route row, verify the route detail sheet opens with the same fields, and close it.

For Phase 6 retained route trace validation:

1. Verify `contacts` contains a promoted contact or use a known 16-hex fingerprint.
2. Run `routes trace <fingerprint>`.
3. Verify the response returns `cmd="routes trace"`, the requested `fingerprint`, `known_contact`, `contact_route`, `route_count`, `best_route`, `best_confidence`, and an `entries` array filtered to that target.
4. Verify `active_probe_supported=true` and `active_probe_command="routes probe <fingerprint>"`; plain `routes trace` still summarizes retained evidence and does not transmit RF.
5. Run `routes probe <fingerprint>` only when an opt-in DM RF trace is allowed. Verify the response has `cmd="routes probe"`, `queued=true`, a generated `trace_` token, `dm_rf_tx=true`, and `public_rf_tx=false`.
6. For physical touch review, open the contact detail sheet, tap `Trace`, verify the Route Trace sheet opens with contact path, best evidence, retained route rows, and a `Ping` action. Tap `Ping` only during RF-allowed validation and verify it queues the same DM-only active trace behavior without sending Public RF.

## Packet Log

For Phase 6 packet-log validation:

1. Run `packets clear`.
2. Verify `packets` reports `count=0` and `persisted=true`.
3. Run `mesh send public test`.
4. Wait for a local MeshCore bot response or relayed Public echo.
5. Verify `packets` contains TX and RX rows with direction, kind, RSSI/SNR, path hash bytes, hops, payload length, and note text.
6. Pick a fresh packet `seq` and run `packets detail <seq>`.
7. Verify the detail response matches the selected packet row.
8. Reboot.
9. Verify `packets` retains the selected row. The RAM ring keeps 128 active rows; NVS persists the newest 8 fallback rows.
10. When `packet_log_backend="sd"`, verify `packets` reports `sd_history.enabled=true`, `sd_history.capacity=4096`, and `sd_history.failed_writes=0`; packet append must keep the NVS fallback fresh, commit the compact SD snapshot through `d1l_packet_log_flush()` or dirty/interval thresholds, and append fixed-size records into `stores/packet_log/segments/sNN.bin` for the 24h-target bounded SD history window.
11. For physical touch review, open the Packet tab, tap a packet row, verify the packet detail sheet opens with the same fields, and close it.

## Optional SD-Card Data Storage

Important pending production feature:

Use `docs/RP2040_SD_BRIDGE_FLASH_D1L.md` for the RP2040 UF2 flash and post-flash proof sequence.

1. On the current D1L build, run `storage status` and verify `sd.interface="rp2040"`, `sd.direct_supported=false`, `sd.rp2040_protocol_supported` reflects whether the RP2040 bridge answered `DESKOS_SD_STATUS`, `sd.file_ops`, `sd.file_line_max`, `sd.file_chunk_max`, `sd.path_max`, and `sd.atomic_rename` expose the exact file-operation gate, `data_backend` is either `nvs` or `mixed`, `setup_action` is machine-readable, retained store backends keep settings, identity, contacts, read-state, and crashlog on onboard/fallback storage, and `export_backend`/`stores.exports` report either `serial` or `sd_diagnostic_exports_ready`.
2. Run `storage setup` and verify it reports `will_format=false`, `format_requested=false`, `format_performed=false`, `policy="no_device_format"`, and `fallback="nvs"` without modifying onboard data.
3. Verify no serial command, UI action, RP2040 request, script flag, or hardware test path can format an SD card.
4. Verify the Settings setup dashboard, SD Card sheet, and UI simulator expose the same storage fallback/setup state while still exposing Wi-Fi, BLE, Radio, Map Tiles, Display, Identity, Diagnostics, About, and Advanced/Advert paths.
5. Boot with no card and verify firmware continues with onboard storage defaults.
6. After the RP2040 SD bridge firmware is flashed through a documented RP2040 path, boot with a valid DeskOS-formatted card and verify serial/UI status reports card/root readiness. If boot prepare reaches the ready file gate before store init, verify `message_store_backend="sd"`, `dm_store_backend="sd"`, `route_store_backend="sd"`, `packet_log_backend="sd"`, `data_backend="mixed"`, and `setup_action="retained_history_sd_enabled"` while settings/identity/contacts/read-state/crashlog remain onboard-backed. `export_backend` may report `sd_diagnostic_exports_ready`, and map tiles may report `sd_map_tiles_ready` when the map-tile file-operation gate is ready. If boot prepare times out or remains `mount_pending`, fallback must remain NVS until an explicit later reload/reboot path exists.
7. Run `python .\scripts\sd_boot_prepare_acceptance_d1l.py --port COM12 --scenario correct-structure` and `python .\scripts\sd_boot_prepare_acceptance_d1l.py --port COM12 --scenario missing-structure` to prove correct DeskOS cards and valid filesystems with missing `/deskos` are prepared without formatting.
8. With a non-FAT32 or unmountable card inserted, run `python .\scripts\sd_boot_prepare_acceptance_d1l.py --port COM12 --scenario unformatted` and verify it records `formats_sd=false`, does not send a setup-confirm command, keeps NVS fallback active, and tells the user to prepare FAT32 on a computer.
9. With the RP2040 bridge file protocol flashed, run `storage filecanary` or `python .\scripts\sd_file_canary_d1l.py --port COM12` to perform the serial-only file-operation canary under `/deskos`: temp write, read-back compare, `rename replace=1`, stat, final read, delete, and deleted-stat verification. Then run `storage export-canary <token>` or `python .\scripts\sd_export_canary_d1l.py --port COM12 --token export1` to prove the diagnostic export path writes `exports/diagnostics/export-canary-<token>.json` by temp write/read plus atomic rename and leaves the final file present. Then run `storage export-diagnostics <token>` or `python .\scripts\sd_diagnostic_export_d1l.py --port COM12 --token diag1` to prove a chunked diagnostic export JSON bundle writes to `exports/diagnostics/diagnostic-export-<token>.json`, verifies temp and final readback, and reports map tile cache readiness without bundling actual tiles. Then run `storage export-data <token>` or `python .\scripts\sd_data_export_d1l.py --port COM12 --token data1` to prove a sampled user-data JSON bundle writes to `exports/data/data-export-<token>.json`, verifies temp and final readback, and reports `private_identity_exported=false`. Then run `storage map-tile-canary <token>` or `python .\scripts\sd_map_tile_canary_d1l.py --port COM12 --token map1` to prove the map-tile cache path writes `map/tiles/z12/x1/y2-<token>.tile` through temp write/read plus atomic rename and leaves the final synthetic tile present. Then run `python .\scripts\sd_retained_history_acceptance_d1l.py --port COM12` to append synthetic retained Public, DM, route, and packet rows through `storage retained-canary <token>` and prove they are readable before and after reboot. Follow with `python .\scripts\soak_d1l.py --port COM12 --duration-sec 300 --sample-interval-sec 60 --sample-storage --sd-file-canary` so the file canary is repeated during a passive stability window. Do not send Public RF for this validation.
10. Boot with a present but unrelated existing-data card and verify firmware offers NVS fallback or clear backup/reformat-on-computer guidance without silently wiping data.
11. Confirm incidental boot/touch events never write, delete, or format SD data.
12. Use `tools/rp2040_sd_protocol.py` to verify the reference ping, status, diagnostic, and file-operation line grammar for `no-card`, `ready`, and `needs-fat32` scenarios before implementing retained-store migration. Use `python .\tools\rp2040_sd_protocol.py --request DESKOS_SD_PING` to print the no-SD-touch bridge ping, `python .\tools\rp2040_sd_protocol.py --request DESKOS_SD_DIAG --scenario no-card` to print the bridge diagnostic line, `python .\tools\rp2040_sd_protocol.py --scenario ready --file-canary-transcript` to print the deterministic host transcript that mirrors `storage filecanary`, `python .\tools\rp2040_sd_protocol.py --scenario ready --export-canary-transcript --token export1` to print the export-canary transcript, `python .\tools\rp2040_sd_protocol.py --scenario ready --diagnostic-export-transcript --token diag1` to print the chunked diagnostic-export transcript, and `python .\tools\rp2040_sd_protocol.py --scenario ready --map-tile-canary-transcript --token map1` to print the map tile cache transcript.
13. With no card, protocol timeout, `file_ops=0`, `atomic_rename=0`, or smaller-than-required line/path/chunk limits, verify packet-log persistence remains on NVS and survives reboot with the existing `d1l_packets`/`ring` fallback data location.
14. When configured, verify retained Public history writes `stores/messages/public/public.tmp` then commits `stores/messages/public/public.bin`, DM history writes `stores/messages/dm/threads.tmp` then commits `stores/messages/dm/threads.bin`, route history writes `stores/routes/routes.tmp` then commits `stores/routes/routes.bin`, and packet compact history writes `stores/packet_log/ring.tmp` then commits `stores/packet_log/ring.bin`; all compact blobs must use `rename replace=1`, keep the NVS mirror, and fall back to NVS on SD absence, timeout, or corrupt blobs. Packet history also writes a bounded 64 x 64 record SD journal under `stores/packet_log/segments/sNN.bin`; new segment starts may truncate that DeskOS-owned segment file, later records append, and `packets clear` may delete those DeskOS-owned segment files without formatting the card.
15. With Wi-Fi connected and an allowed non-public-OSM provider configured for a small user-scoped area, run `storage map-tile-download <z> <x> <y> <url-template> <attribution>` for one test tile and verify it reports `provider_allowed=true`, `attribution_saved=true`, `public_rf_tx=false`, `formats_sd=false`, a bounded `bytes` value under `max_bytes`, final path `map/tiles/z.../x.../y....tile`, and attribution metadata under `map/tiles/attribution.json`.
16. On touch hardware, open Map or Settings > Map Tiles, enter the same allowed provider template and attribution, adjust zoom if needed, tap `Download`, and verify the center-tile SD cache write succeeds only when Wi-Fi is connected, SD cache is ready, D1L location is saved, and provider/attribution are valid. Verify `settings get` reports `map_tiles.provider_saved=true`, visible attribution remains available, no Public RF is sent, no format action occurs, and the cached tile/attribution survive reboot/card remount. The diagnostic export bundle reports map tile cache readiness but does not bundle actual tiles, and `storage export-data <token>` covers sampled user data rather than map tiles.
17. Verify settings, identity, and minimum boot-critical state remain available from onboard storage if the card is removed.
18. Verify the `rp2040-sd-bridge-firmware` artifact checksum manifest before any RP2040 hardware flash attempt.
18. Before copying the RP2040 UF2, run `python .\scripts\rp2040_sd_bridge_preflight_d1l.py --port COM12 --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --out artifacts\rp2040-preflight\d1l-rp2040-sd-bridge-preflight-COM12.json`. If it reports `rp2040_protocol_pending` or `sd_card_not_present_diag_pending` and no UF2 volume, put the RP2040 into UF2/BOOTSEL mode. If it reports `sd_bridge_ready`, proceed directly to the SD file/export canaries.

## Mesh Visibility

For Phase 6 signal/room-server/repeater validation:

1. Run `signal` and verify `sample_count` is nonzero after live RX, `latest.rssi_dbm` is nonzero, and RSSI/SNR values reflect recent packet, route, or heard-node evidence.
2. Run `roomservers` and verify `total_known` and `entries` reflect signed heard-node adverts whose stored role is `room`.
3. Run `repeaters` and verify entries are inferred only from nonzero path-hop route or heard-node evidence; Public route rows should not by themselves become repeater candidates.
4. Run `mesh send public test`, wait for local MeshCore bot replies, and verify D1L packet count increases while the COM11 bot status counters show fresh Public movement.
5. Verify `health` remains `board_ready=true`, `ui_ready=true`, and reports nonzero task stack watermarks after the probe.
6. For physical touch review, open the Packet tab, tap the `Mesh Roles` card, verify the role browser sheet lists room servers and repeater candidates, scroll if the list exceeds the sheet, then close it.
