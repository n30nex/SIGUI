# Phase 2 Foundation Checkpoint

Date: 2026-06-29

## Completed

- Added NVS-backed settings model for node name, desk companion role, default-off Wi-Fi/BLE/observer policy, contrast flags, path hash size, and Canada/USA radio settings.
- Validated settings persistence on real D1L flash by setting a temporary node name/path-hash value, rebooting, verifying both survived NVS, then resetting back to defaults.
- Added `settings get`, `settings reset`, `settings set name <name>`, and `settings set pathhash <1|2|3>` serial commands.
- Added `identity status` command that reports the persisted node label and honestly marks the MeshCore cryptographic local identity as pending the Phase 2 C++ MeshCore binding.
- Added a `meshcore_service` foundation with state, counters, path-hash mode, companion framing readiness, and explicit not-ready responses for advert/message commands until RF integration is complete.
- Added a narrow MeshCore Public group text TX/RX path using the D1L SX1262 on the Canada/USA preset.
- Added Public packet evidence to `packets`, including TX/RX direction, RSSI/SNR, path hash size, hop count, payload length, and sanitized message notes.
- Implemented serial handlers for `radio set freq`, `radio set bw`, `radio set sf`, and `radio set cr`.
- Added `mesh advert flood` handling alongside `mesh advert zero`.
- Added host contract tests for the settings, command surface, and Public RF packet/radio contract.

## Commands Run

```powershell
python -m pytest tests
python .\scripts\smoke_d1l.py --dry-run
podman run --rm -v "F:\SIGUI:/project" -w /project docker.io/espressif/idf:release-v5.1 bash -lc "git config --global --add safe.directory /project && . /opt/esp/idf/export.sh >/tmp/idf-export.log && idf.py fullclean build"
python .\scripts\smoke_d1l.py --port COM7 --timeout 8 --out artifacts\smoke\d1l-smoke-public-rf-COM7-after-lcd-config.json
```

## Results

- Host tests: 25 passed.
- Smoke dry run: passed and now includes `settings get` and `identity status`.
- Hardware persistence smoke: passed on `COM7`; evidence is archived at `artifacts/smoke/d1l-smoke-persistence-COM7.json`.
- Clean Podman ESP-IDF build: passed; `build/meshcore_deskos_d1l.bin` size `0x89a40`, 46% free in the app partition.
- Hardware smoke after fixing the tracked LCD direct-mode config: passed on `COM7`; evidence is archived at `artifacts/smoke/d1l-smoke-public-rf-COM7-after-lcd-config.json`.
- Controlled Public RF test: passed on `COM7`; D1L sent exact Public text `test`, local Meshcorebot counters moved by `rx_channel_total +4`, `relay_success_total +4`, `discord_send_success_total +4`, and the D1L decoded Public replies including `Krabs Node: Test OK CH0.` Evidence is archived at `artifacts/smoke/d1l-public-test-rf-COM7.json`.

## Not Yet Validated

- MeshCore C++ `LocalIdentity` generation/storage.
- MeshCore advert TX/RX.
- Public chat through the touch UI; current validation is through the serial console.
- Full MeshCore companion C++ binding for identities, DMs, contacts, and persistent stores.
