# Phase 4 DM ACK And Path Checkpoint

Date: 2026-06-29

## Completed

- Added persisted DM ACK marking by MeshCore ACK hash.
- Added MeshCore ACK RX parsing for standalone ACK packets and multipart ACK packets.
- Added encrypted MeshCore PATH return RX parsing for known contacts with retained public keys.
- Added contact-store return-path retention with v1/v2-to-v3 NVS migration.
- Added direct-route DM TX selection when a contact has a learned return path.
- Added `contacts` diagnostic fields for `out_path_known`, `out_path_len`, and `out_path_updated_ms`.

## Results

- Host tests: passed locally with 50 tests.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0xa0bd0`, 37% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification and no flash backup, per operator instruction.
- Hardware smoke: passed; `artifacts/smoke/d1l-smoke-dm-ack-path-local-COM7.json`.
- Public `test` RF sanity: passed for live RF path; `artifacts/smoke/d1l-public-test-bot-counter-delta-local-COM7.json` showed COM11 Meshcorebot counter movement of `rx_channel_total +2`, `relay_success_total +2`, `discord_send_success_total +2`, `rx_log_total +4`, and `rx_duplicate_total +2`. D1L stayed up past 116 seconds and decoded a fresh relayed Public `test` at RSSI `-43`, SNR `30`.
- DM TX to `YKF Corebot`: passed for TX and bot receipt; `artifacts/smoke/d1l-dm-ack-path-direct-local-COM7.json` showed `rx_contact_total +1`, `relay_success_total +1`, `discord_send_success_total +1`, `rx_log_total +3`, and `rx_duplicate_total +1`.
- Boot-loop check: no loop observed after this image; `health` reported increasing uptime through the smoke and RF probes, with `board_ready=true` and `ui_ready=true`.

## Remaining Gaps

- The local COM11 bot did not emit a MeshCore ACK/PATH return during the DM probe, so `messages dm` correctly retained the TX row with `acked=false`, and `contacts` kept `out_path_known=false`.
- Direct-route DM TX is implemented but needs a controlled peer that returns a MeshCore PATH packet before it can be proven over RF.
- Touch DM composer/thread UI, controlled inbound DM proof, retry/resend, unread state, and contact detail actions are still pending.
