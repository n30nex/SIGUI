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
- Phase 4 Public message store contract, DM store contract, heard-node store contract, contact store contract, route store contract, Public composer UI contract, and serial diagnostics.

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

## Route Store

For Phase 4 route-store validation:

1. Run `routes clear`.
2. Verify `routes` reports `count=0`.
3. Run `mesh send public test`.
4. Wait for a local MeshCore bot response.
5. Verify `routes` contains Public TX and RX rows with route name, direction, path hash bytes, hops, confidence, RSSI/SNR, payload length, and `persisted=true`.
6. Reboot.
7. Verify `routes` retains the rows.
