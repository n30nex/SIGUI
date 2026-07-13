# SIGUI Audit Evidence Index

**Audit date:** 2026-07-12  
**Repository:** `n30nex/SIGUI`  
**Purpose:** make the master roadmap traceable to the exact repository state inspected.

## Snapshot

| Item | Value |
|---|---|
| Merged `main` | `5b5dfaa0592347497df1a2f77f572c6d49933c6a` |
| Newest integrated candidate | `07322ed4c700866106ecca6c31ff70ea3a3d4ede` |
| Active PR stack | #62 → #64 → #80 |
| Candidate distance from audited `main` | 30 commits ahead, 0 behind |
| Pinned MeshCore | `e8d3c53ba1ea863937081cd0caad759b832f3028` |
| SDK | ESP-IDF 5.5.4 |
| Candidate host suite | 614 passing tests reported by PR/CI |
| Candidate CI | green host, envelope conformance/fuzz, RP2040, firmware, package, checksum |
| Conformance closure | false; `wire_envelope_only` |
| Release status | not ready to tag |

## Live post-audit reconciliation

- PR #81 merged WP-00 on live `main` at `157c9670eb43a0119280f1e8119d9584b06dcfbf`.
- Exact-main Actions run `29208908642` passed change filtering, host checks including the completion-ledger validator, firmware packaging, and checksum verification. RP2040 stayed correctly out of scope.
- Downloaded host receipt `completion_ledger_validation_157c9670eb43a0119280f1e8119d9584b06dcfbf.json` reports `passed=true`, `error_count=0`, and `repository_commit=157c9670eb43a0119280f1e8119d9584b06dcfbf`; its SHA-256 is `13b977eafb883d421ccdbfca4eba8dc8c3838b5198745dcf63c08cee86e741c8`.
- This closes WP-00 only. WP-01 remains `in_progress`, `proof_banked=false`, and unmerged. Its exact `07322ed` pair reached a clean `READY_SD` preflight with zero retained counters, but asynchronous `storage diag raw` exceeded its fixed settle window and overlapped the one-second retained worker before the packet-append failure and correctly cancelled reboot. A 750 ms ordinary-timeout increase is not accepted as the fix; the next proof isolates diagnostics in a maintenance boot, resets/reflashes the exact artifacts, and requires another clean zero-counter preflight before canaries.

## Branch and PR evidence

### PR #62

- Base: `main`
- Purpose: bounded built-in current-view Map and UI hierarchy
- Large cross-cutting change; must land first.

### PR #64

- Base: PR #62 branch
- Purpose: ESP-IDF 5.5.4, dependency lock, BSP compatibility, Wi-Fi/Map/platform integration
- Must be retargeted to `main` after #62 lands and rechecked.

### PR #80

- Base: PR #64 branch
- Purpose: first MeshCore envelope conformance slice, retained durability, release evidence, current false-`no_card` repair
- Predecessor hardware failures:
  - route-persistence task stack overflow;
  - post-ack WDT;
  - inserted card temporarily reported `no_card`.
- Current head includes a narrow read-only sector-zero liveness check but still requires exact-pair proof.

### Stale feature branch

`feature/meshcore-deskos-d1l` has diverged from `main`. It is not a valid bulk merge source. Compare it after the active stack lands; cherry-pick only proven unique desired work, then archive it.

## Source anchors and confirmed conclusions

### Build boundary

**File:** `main/CMakeLists.txt`

Only selected upstream MeshCore Ed25519 C files are compiled into the current product. The upstream Mesh, `BaseChatMesh`, dispatcher, contacts/channels, and session behavior are not the production runtime. Therefore the local protocol implementation needs semantic conformance, not merely source presence.

### Mesh service

**File:** `main/mesh/meshcore_service.c` — about 1,826 lines

Confirmed:

- one hard-coded Public secret/hash path;
- small service queue centered on RX start/raw send;
- outbound DM attempt effectively fixed;
- inbound DM computes ACK data but does not transmit ACK/PATH;
- outbound history/state is advanced before TxDone/ACK truth;
- trace is represented as a normal DM token;
- callbacks perform parsing/store/status work directly;
- mutable service state is accessed through multiple paths.

### Upstream expected behavior

**Files at pinned MeshCore:** `src/helpers/BaseChatMesh.h/.cpp`

Confirmed upstream concepts:

- expected ACK derived from timestamp/message/sender key;
- inbound valid DM sends direct ACK or flood ACK+PATH;
- attempts are encoded, including extended attempt values;
- direct vs flood timeout calculation;
- path return behavior;
- contact lifecycle and channels;
- login/request/response/keepalive session concepts.

### Wire conformance

**File:** `main/mesh/meshcore_wire.c`

Current implementation validates and encodes structural header, transport codes, path length/bytes, and payload. It does not by itself prove crypto, advert, DM, ACK, retry, route, trace, duplicate, replay, or retained-session semantics.

### Boot/application ownership

**File:** `main/app_main.c`

Positive fail-closed initialization exists for NVS, retained NVS, RP2040/SD, stores, radio, connectivity, UI, and console. There is no single immutable capability registry, so partial subsystem failures can be difficult for UI/automation to represent consistently.

### Retained storage

**Files:**

- `main/storage/retained_blob_store.c` — about 1,502 lines
- `main/storage/storage_status.c` — about 1,276 lines
- route/message/DM/packet stores and worker modules

Confirmed strengths:

- dedicated retained NVS partition;
- marker/anchor/sentinel ownership;
- compact NVS fallback;
- SD generations;
- atomic replace;
- no-format policy;
- detailed telemetry.

Confirmed risk:

- behavior is duplicated across stores;
- reboot/quiescence spans multiple tasks/locks;
- exact current repair is not physically closed;
- broader coalescing, power-loss, schema, reset, and time work remains open.

### RP2040 bridge

**Files:**

- `main/hal/rp2040_bridge.c` — about 1,389 lines
- `firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino` — about 3,093 lines

The RP2040 owns physical SD. Current bridge firmware includes card-detect sampling, low-level SPI probes, FAT32 mount, directory creation, file protocol, atomic rename, diagnostics, and the new bounded sector-zero liveness verification. This subsystem must be frozen during exact-pair qualification.

### Map

**Files:**

- `main/map/map_view_service.c`
- `main/storage/map_tile_store.c`

Confirmed:

- current-view-only plan;
- max visible 3×3 tiles;
- sequential fetch;
- explicit User-Agent;
- TLS certificate bundle and SNTP;
- 429/503 handling;
- cancellation;
- content/size/PNG validation;
- atomic SD cache;
- attribution metadata;
- PSRAM double-buffered render.

Open:

- exact hardware live fetch/render/cache/cancel proof;
- signed peer location markers;
- provider/cache lifecycle;
- Wi-Fi reconnect;
- Map requires ready SD cache for current implementation.

### Wi-Fi/BLE

**File:** `main/comms/connectivity_manager.c`

Wi-Fi station setup, saved profile, scan/status, and fail-closed startup exist. A complete bounded reconnect/backoff state machine is not evident. BLE is disabled in `sdkconfig.defaults` and remains a placeholder capability.

### UI

**File:** `main/ui/ui_phase1.c` — about 8,544 lines

Confirmed:

- many mature screens and simulator contracts;
- large global widget/modal/state surface;
- navigation, data access, actions, refresh, and styling mixed in one implementation;
- incremental controller extraction is necessary before adding the full remaining feature set.

### USB console

**File:** `main/comms/usb_console.c` — about 5,124 lines

The console is a major strength for deterministic automation but mixes parsing, dispatch, JSON, diagnostics, mutating controls, and test hooks. It should become a domain command registry and feed a bounded redacted event ring used by both USB and the requested UI Terminal.

### Release gate

**File:** `scripts/release_gate_audit_d1l.py` — about 3,430 lines

Confirmed:

- extensive exact-commit/evidence policy;
- current conformance closure deliberately false;
- current default helper peer resolves to COM11;
- this conflicts with the operator’s hard no-COM11 rule and must be removed.

### Partition table

**File:** `partitions_d1l.csv`

Current layout has a single large factory application and no `otadata`, `ota_0`, or `ota_1`. Release-grade OTA requires a partition/size/migration decision before implementation.

### Settings/time

**Files:** `main/app/settings_model.h/.c`

Confirmed:

- schema v7;
- identity/private key, Wi-Fi profile, radio/settings in NVS-backed model;
- migrations exist for older schemas;
- no complete channel/admin/timezone model;
- unknown future schema handling needs non-destructive quarantine;
- Mesh timestamps are monotonic, not truthful wall-clock time.

## External policy anchors

### OpenStreetMap Standard tiles

Official policy requires visible attribution, a distinct User-Agent, cache use, and no bulk/offline prefetch. It recommends avoiding a hard-coded provider URL. The current bounded Map foundation aligns with the important request limits but needs final provider/cache lifecycle documentation and proof.

### GitHub Actions

Official GitHub guidance states that a full-length action commit SHA is the immutable pinning mechanism. Current moving major action tags should be pinned before public release.

### ESP-IDF

ESP-IDF v5.5.4 is an official bug-fix release in the selected 5.5 line. The repository’s version and lock must still be proved on exact combined hardware and kept immutable in release metadata.

## Audit limitations

- No new physical flash or RF test was performed by this audit.
- No claim from a predecessor SHA is treated as final release proof.
- GitHub source, history, issues, PRs, and workflow results were inspected through the connected GitHub/API tools because a direct local clone was unavailable in the audit environment.
- The roadmap therefore includes exact physical closure wherever source/CI cannot prove hardware behavior.
