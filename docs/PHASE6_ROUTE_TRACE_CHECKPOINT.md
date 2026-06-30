# Phase 6 Route Trace Checkpoint

Date: 2026-06-30

## Scope

This checkpoint adds a read-only retained-evidence route trace helper. It does not transmit RF and does not claim active ping/trace support.

## Implementation

- `d1l_route_store_copy_for_target` returns bounded newest route rows for one target fingerprint.
- `d1l_app_model_copy_route_trace` exposes the same query to UI code.
- `routes trace <fingerprint>` returns contact state, best retained route evidence, filtered route rows, and `active_probe_supported=false`.
- Contact Detail now exposes `Trace`, opening a Route Trace sheet with contact path, best evidence, retained route rows, and an active-ping pending note.
- `tools/ui_simulator.py` renders the Route Trace sheet in the default and `large-mesh` scenarios.

## Validation

- Host tests: `python -m pytest -q` passed with 83 tests.
- Simulator:
  - `python .\tools\ui_simulator.py --out artifacts\ui-sim`
  - `python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large`
  - Both reports passed with 19 rendered views.
- Dry run: `python .\scripts\smoke_d1l.py --dry-run` includes `routes trace 0BF0A701D5AE2DB6`.
- DM-only probe dry run: `python .\scripts\probe_d1l_dm.py --dry-run` includes `mesh send dm 0BF0A701D5AE2DB6 <token>` and no Public transmit command.
- Build:
  - Podman ESP-IDF v5.1 build passed.
  - App size: `0xa9de0` (`695776` bytes), 34% free.
  - App SHA256: `c1377a300fc6bcf4cd00f6b7f0f87e469eec8048349e3628a178d07d694e6117`.
- Flash:
  - Flashed `COM7` directly with no backup/readback.
  - esptool verified written hashes.
- Standard smoke:
  - Artifact: `artifacts/smoke/d1l-smoke-route-trace-local-COM7.json`.
  - Passed 34 commands.
  - `routes trace 0BF0A701D5AE2DB6` returned `known_contact=true`, alias `YKF Corebot`, `route_count=2`, best seq `291`, confidence `100`, and `active_probe_supported=false`.
  - No `mesh send public` command was sent.
- Targeted DM-only hardware probe:
  - Runs through `scripts/probe_d1l_dm.py` against D1L `COM7` and Meshcorebot `COM11`.
  - Sends `mesh send dm 0BF0A701D5AE2DB6 <token>`, then checks D1L retained DM/packet rows, `routes trace`, `health`, and COM11 `rx_contact_total` movement.
  - `public_rf_transmit=false` and the command list must contain no `mesh send public`.
  - Passing artifact: `artifacts/smoke/d1l-dm-probe-route-trace-pass-local-COM7-COM11.json`.
  - The passing run sent token `d1l_dm_probe_20260630_081602`, retained it in `messages dm 0BF0A701D5AE2DB6`, found the matching TX `dm_text` packet through `packets search`, and moved COM11 counters by `rx_contact_total +1`, `rx_log_total +3`, `relay_success_total +1`, and `discord_send_success_total +1`.
  - `routes trace 0BF0A701D5AE2DB6` then showed retained `dm_text`, `path_return`, and `dm_ack` rows for the target with `active_probe_supported=false`.
- Release package:
  - Package: `artifacts/release/d1l-release-route-trace-local`.
  - `scripts/verify_checksums.py` passed for `SHA256SUMS.txt`.

## Still Pending

- Manual physical touch review of Contact Detail `Trace` and Route Trace open/scroll/close.
- Active RF ping/trace helper.
- Controlled ACK/PATH RF proof and direct-route RF proof.
- Full 12-hour idle/listening soak.
