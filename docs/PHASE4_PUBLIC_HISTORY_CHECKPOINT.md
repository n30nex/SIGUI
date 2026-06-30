# Phase 4 Public History/Search Checkpoint

Date: 2026-06-30

## Scope

This checkpoint adds bounded retained Public History/Search UI and serial filtering, then validates the slice with host tests, simulator screenshots, local Podman firmware build, COM7 flash, standard smoke, and targeted hardware DM proof through the local COM11 Meshcorebot.

Per operator instruction, this checkpoint did not run active Public-channel RF testing.

## Implementation

- `messages public [search <text>]` now returns retained Public rows and can filter by author, direction, or text.
- The Messages tab now has a bounded Public History sheet and a Public Search sheet.
- `tools/ui_simulator.py` renders Public History/Search and checks large-mesh bounded rendering.
- Smoke coverage includes `messages public search test`.

## Validation

- Host tests: `python -m pytest -q` passed with 83 tests.
- Simulator:
  - `python .\tools\ui_simulator.py --out artifacts\ui-sim`
  - `python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large`
  - Both reports passed with 18 rendered views, no missing labels, and no text overflow.
- Build:
  - Podman ESP-IDF v5.1 build passed.
  - App size: `0xa93e0` (`693216` bytes), 34% free.
  - App SHA256: `77e495fec80dc749c4359562a62474b6efd42b7efa05e023349245373bbf63a2`.
- Flash:
  - Flashed `COM7` directly with no backup/readback.
  - esptool verified written hashes.
- Standard smoke:
  - Artifact: `artifacts/smoke/d1l-smoke-public-history-local-COM7.json`.
  - Passed 33 commands, including `messages public search test`.
  - No `mesh send public` command was sent.
- Hardware DM proof:
  - Artifact: `artifacts/smoke/d1l-dm-public-history-target-pass-local-COM7-COM11.json`.
  - Target: `YKF Corebot` fingerprint `0BF0A701D5AE2DB6` on COM11.
  - D1L retained the exact DM token in `messages dm 0BF0A701D5AE2DB6`.
  - `packets search <token>` returned the matching TX `dm_text` row.
  - COM11 Meshcorebot status counters moved `rx_contact_total +1` and `rx_log_total +3`.
  - No Public-channel RF command was sent.
- Release package:
  - Package: `artifacts/release/d1l-release-public-history-local`.
  - `scripts/verify_checksums.py` passed for `SHA256SUMS.txt`.

## Still Pending

- Manual physical touch review of Public History/Search open/search/clear/close.
- Controlled inbound DM proof and inbound DM unread proof.
- Controlled ACK/PATH RF proof and direct-route RF proof.
- Full 12-hour idle/listening soak.
