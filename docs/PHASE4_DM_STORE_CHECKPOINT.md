# Phase 4 DM Store Checkpoint

Date: 2026-06-29

## Completed

- Added a bounded NVS-backed direct-message store with 16 rows.
- Added `messages dm` and `messages dm clear` serial diagnostics.
- Added `mesh send dm <fingerprint> <text>` for promoted contacts that retain a full MeshCore public key.
- Added MeshCore private text flood TX using retained contact public keys and Ed25519 key exchange.
- Added best-effort DM RX decode for known contacts with retained public keys.
- Added recent DM rows to the app snapshot and Messages screen preview.
- Added DM commands to the hardware smoke command list.
- Follow-up ACK/path backend is covered in `PHASE4_DM_ACK_PATH_CHECKPOINT.md`.

## Results

- Host tests: passed locally with 50 tests.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0xa03f0`, 37% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification.
- Hardware smoke on the final local image on `COM7`: passed; evidence is archived at `artifacts/smoke/d1l-smoke-phase4-dm-store-final-local-COM7.json`.
- DM TX/store/reboot persistence: passed; `artifacts/smoke/d1l-dm-store-tx-persistence-local-COM7.json` sent `d1l dm store pass` to `YKF Corebot` fingerprint `0BF0A701D5AE2DB6`, wrote a TX row with nonzero ACK hash `2069692642`, rebooted, retained the row, and showed post-reboot uptime increasing with `reset_reason=SW`.
- Local COM11 Meshcorebot observed the DM contact packet: `rx_contact_total +1`, `relay_success_total +1`, and `discord_send_success_total +1`.
- Public `test` regression still proves D1L TX reaches the local COM11 bot: `artifacts/smoke/d1l-public-test-response-window-local-COM7.json` moved `rx_channel_total +3`, `relay_success_total +3`, and `discord_send_success_total +3`.

## Remaining Gaps

- D1L did not hear a fresh local bot `Test OK` Public reply during the latest three-send response window, even though COM11 received and relayed all three Public `test` packets.
- DM ACK/PATH receive parsing and direct-path TX selection are now implemented; controlled RF proof still needs a peer that emits ACK/PATH returns.
- Touch DM composer, contact detail actions, unread state, retry/resend, and full thread UI are still pending.
- RX DM decode is implemented for known retained public keys but still needs a controlled inbound DM artifact.
