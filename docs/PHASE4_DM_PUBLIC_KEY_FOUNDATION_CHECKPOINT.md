# Phase 4 DM Public-Key Foundation Checkpoint

Date: 2026-06-29

## Completed

- Added full 64-hex advertised public-key retention to heard-node rows.
- Added full public-key promotion from heard nodes into contact rows.
- Added schema-v1 migration for existing heard-node and contact NVS blobs.
- Exposed `public_key` in `nodes`, `contacts`, and `contacts add` serial diagnostics.
- Updated Nodes UI rows to show whether a node/contact has a retained key.

## Results So Far

- Host tests: passed locally with 46 tests.
- `git diff --check`: passed.
- Smoke dry run: passed and includes `nodes`, `contacts`, and `routes`.
- Local Podman ESP-IDF build: passed; `meshcore_deskos_d1l.bin` size `0x9eac0`, 38% free in the app partition.
- Flash to `COM7`: passed with esptool hash verification.
- Hardware smoke on `COM7`: passed; evidence is archived at `artifacts/smoke/d1l-smoke-phase4-dm-public-key-foundation-local-COM7.json`.
- Public-key retention and reboot persistence: passed; `artifacts/smoke/d1l-public-key-foundation-local-COM7.json` captured a signed advert for `YKF Corebot`, fingerprint `0BF0A701D5AE2DB6`, with full public key `0BF0A701D5AE2DB679C641EE999A70D4B55B61A2B77C47337CE35C16C9C19193`. The same key was copied into `contacts add`, and both heard-node and contact rows survived reboot with `reset_reason=SW`.
- Public RF regression after the COM11 bot restart: passed; local Meshcorebot counters moved by `rx_channel_total +2`, `relay_success_total +2`, and `discord_send_success_total +2`, and D1L retained `Krabs Node: Test OK CH0.` replies.

## Still Pending

- GitHub Actions artifact build/flash/revalidation for the next DM-store slice.
- Real MeshCore DM ACK/path handling, controlled inbound DM proof, and DM touch workflow. Serial DM flood TX and the bounded DM store are covered by `PHASE4_DM_STORE_CHECKPOINT.md`.
