# D1L Test Plan

## Host Tests

Run:

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
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
- Phase 2 MeshCore service command surface.
- Phase 4 Public message store contract, DM store contract, unread/read-state contract, heard-node store contract, contact store contract, route store contract, Public composer UI contract, and serial diagnostics.

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
- `health`

Hardware success must include manual confirmation for the display/touch test until automated screen capture exists.

## Message Store Persistence

For Phase 4 Public message-store validation:

1. Run `messages clear`.
2. Run `mesh send public test`.
3. Wait for a local MeshCore bot response.
4. Verify `messages public` contains at least one TX row and one RX row.
5. Reboot.
6. Verify `messages public` retains the rows and `packets` starts over as the volatile RF log.

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
6. Reboot.
7. Verify `routes` retains the rows.
