# D1L Test Plan

## Host Tests

Run:

```powershell
python -m pytest tests
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test --active-interval-sec 30 --require-rx-delta --min-tx-delta 1
```

Coverage:

- Canada/USA radio defaults.
- D1L pin map and TCXO `NONE`.
- RP2040 bridge pin contract.
- No hardcoded executable COM ports.
- Smoke JSONL parser.
- Flash/monitor scripts require an explicit port.
- Backup command builder.
- Checksum verifier.
- MeshCore 3-byte companion transport codec.
- NVS settings contract and default-off Wi-Fi/BLE/observer policy.
- Phase 5 connectivity status contract: `wifi status`, safe `wifi scan`, `wifi off`, `ble status`, and `ble off` must be machine-readable and reflect runtime-pending/build-disabled companion radios.
- Phase 7 diagnostics contract: `crashlog` must return a bounded persisted reset ring, `crashlog clear` must clear it, and `health` must include heap/PSRAM largest blocks, task stack watermarks, LVGL usage, reset reason, and board/UI readiness.
- Phase 7 soak harness contract: `scripts/soak_d1l.py` must have a dry-run path, must sample `health`, `mesh status`, `signal`, `messages unread`, `packets`, and `crashlog`, and must summarize uptime monotonicity, readiness, packet deltas, heap/PSRAM deltas, stack floors, and LVGL peak usage.
- Phase 8 release package contract: `scripts/package_release_d1l.py` must emit a normal flash set, app update image, full 8MB image, manifest, SHA256SUMS, README, and explicit-port flash helpers.
- UI simulator contract: `tools/ui_simulator.py` must render deterministic 480x480 PNGs plus `ui-sim-report.json`, cover the main touch surfaces and sheets, and fail on missing required labels or measured text overflow.
- Phase 6 mesh visibility contract: `signal`, `roomservers`, and `repeaters` must be machine-readable, read from bounded packet/route/node stores, avoid new NVS writes, and appear in the smoke command list.
- Phase 2 MeshCore service command surface.
- Phase 4 Public message store contract, DM store contract, unread/read-state contract, heard-node store contract, contact store contract, route store contract, persistent packet log contract, Public composer UI contract, and serial diagnostics.

## Hardware Smoke

Run only when the D1L port is known:

```powershell
$env:D1L_PORT = "COMx"
python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
```

Expected commands:

- `version`
- `board`
- `settings get`
- `identity status`
- `i2c`
- `display test`
- `touch test`
- `button`
- `radiohw`
- `radio get`
- `mesh status`
- `companion status`
- `rp2040 status`
- `packets`
- `messages public`
- `messages dm`
- `messages unread`
- `nodes`
- `contacts`
- `routes`
- `signal`
- `roomservers`
- `repeaters`
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
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 3600 --sample-interval-sec 60 --active-public-text test --active-interval-sec 120 --require-rx-delta --min-tx-delta 1 --out artifacts\soak\d1l-soak-active-1h-COMx.json
```

Success requires every sampled command to return `ok=true`, no uptime rollback, `board_ready=true`, `ui_ready=true`, ready mesh state, nonzero task stack watermarks, and no required packet delta threshold failures. For active RF probes, `mesh_tx_packet_delta` must increase and `mesh_rx_packet_delta` must increase when `--require-rx-delta` is used.

## Release Package

After firmware build:

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
5. Reboot.
6. Verify `messages public` retains the rows and `packets` either retains the newest packet evidence rows or starts a new evidence window if `packets clear` was run for the packet-log test.

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
11. For physical touch review, open the Messages tab, verify new RX rows are highlighted as `new`, tap `Read`, and verify the unread count clears.
12. For muted DM behavior when an inbound DM source is available, mute that contact, receive a DM, and verify the unread row is counted under `muted_dm_unread` rather than audible `dm_unread`.

## DM Store And Serial TX

For Phase 4 direct-message store validation:

1. Verify `contacts` contains a promoted contact with a full 64-hex `public_key`.
2. Run `messages dm clear`.
3. Run `mesh send dm <fingerprint> <text>`.
4. Verify `messages dm` contains a TX row with the contact fingerprint, alias, text, `direction="tx"`, `persisted=true`, and a nonzero `ack_hash`.
5. If the target contact is the local COM11 Meshcorebot, verify its status counters include fresh `rx_contact_total` movement.
6. If the peer emits MeshCore ACK/PATH returns, verify `messages dm` marks the TX row `acked=true` and `contacts` shows `out_path_known=true`.
7. Send a second DM to the same contact and verify `routes` records `kind="dm_text"`, `direction="tx"`, and `route="direct"`.
8. Reboot.
9. Verify `messages dm` retains the TX row and `health` reports `board_ready=true`, `ui_ready=true`, and increasing uptime.

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
3. Verify the DM thread sheet opens with the contact alias, fingerprint metadata, recent rows for the same fingerprint, and `Reply`/`Close` actions.
4. Tap `Reply`, type a short message, and tap `Send`.
5. Verify `messages dm` contains the new TX row for that fingerprint and `health` remains `board_ready=true` and `ui_ready=true`.
6. If the long hardware validation session causes `ESP_ERR_NVS_NOT_ENOUGH_SPACE`, erase only the NVS partition from the current partition table and rerun smoke before continuing persistence checks.
7. For Phase 5 connectivity validation, run smoke with `--persistence-test` and verify `settings get`, `wifi status`, `wifi scan`, and `ble status` report Wi-Fi/BLE disabled after reboot.
8. For Phase 7 diagnostics validation, run `crashlog clear`, `reboot`, then verify `crashlog` contains a new `SW` reset entry and `health` reports nonzero stack watermarks with `board_ready=true` and `ui_ready=true`.

## Heard Node Store

For Phase 4 heard-node validation:

1. Run `nodes clear`.
2. Wait for or trigger a signed MeshCore advert from a local node.
3. Verify `nodes` contains a row with fingerprint, full 64-hex `public_key`, name or fingerprint fallback, type, RSSI/SNR, path metadata, and `persisted=true`.
4. Reboot.
5. Verify `nodes` retains the row and its `public_key`.

## Contact Store

For Phase 4 contact-store validation:

1. Run `contacts clear`.
2. Verify `contacts` reports `count=0`.
3. Verify `nodes` contains a heard node with a 16-hex fingerprint.
4. Run `contacts add <fingerprint>`.
5. Verify `contacts add` reports `source="heard_node"`.
6. Verify `contacts` contains the promoted alias, full 64-hex `public_key`, heard name, type, RSSI/SNR, path metadata, `out_path_known`, `out_path_len`, and `persisted=true`.
7. Reboot.
8. Verify `contacts` retains the row and its copied `public_key`.

For Phase 4 contact detail and favorite/mute validation:

1. Verify `contacts` contains at least one promoted contact.
2. Run `contacts set <fingerprint> favorite 1`.
3. Run `contacts set <fingerprint> mute 1`.
4. Verify `contacts` shows `favorite=true` and `muted=true`.
5. Reboot and verify both flags are still true.
6. Run `contacts set <fingerprint> favorite 0` and `contacts set <fingerprint> mute 0`.
7. Verify `contacts` shows both flags false.
8. For physical touch review, open the Nodes tab, tap the contact row, verify the detail sheet shows signal/key/path metadata, and verify `Fav`, `Mute`, `DM`, and `Close` actions respond.

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
9. Verify `packets` retains the selected row. The RAM ring keeps 32 rows; NVS persists the newest 8 rows.
10. For physical touch review, open the Packet tab, tap a packet row, verify the packet detail sheet opens with the same fields, and close it.

## Mesh Visibility

For Phase 6 signal/room-server/repeater validation:

1. Run `signal` and verify `sample_count` is nonzero after live RX, `latest.rssi_dbm` is nonzero, and RSSI/SNR values reflect recent packet, route, or heard-node evidence.
2. Run `roomservers` and verify `total_known` and `entries` reflect signed heard-node adverts whose stored role is `room`.
3. Run `repeaters` and verify entries are inferred only from nonzero path-hop route or heard-node evidence; Public route rows should not by themselves become repeater candidates.
4. Run `mesh send public test`, wait for local MeshCore bot replies, and verify D1L packet count increases while the COM11 bot status counters show fresh Public movement.
5. Verify `health` remains `board_ready=true`, `ui_ready=true`, and reports nonzero task stack watermarks after the probe.
6. For physical touch review, open the Packet tab, tap the `Mesh Roles` card, verify the role browser sheet lists room servers and repeater candidates, scroll if the list exceeds the sheet, then close it.
