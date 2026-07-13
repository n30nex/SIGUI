# SIGUI Audit Evidence Index

**Audit date:** 2026-07-12  
**Repository:** `n30nex/SIGUI`  
**Purpose:** make the master roadmap traceable to the exact repository state inspected.

## Snapshot

| Item | Value |
|---|---|
| Live merged `main` | `3e712916a05931fd10998f51d7f616e506daeeb4` |
| WP-01 exact source candidate | `092293f2311a24c9899bc9bf343ab014c4ba0411` |
| Active PR stack | #62 → #64 → #80 |
| Candidate distance from live `main` | 61 commits ahead, 3 behind before WP-02 reconciliation |
| Proof-ledger PR | #83; pre-update remote head observed at `185b07100e55445bdf0d61a238de2d6e6df2cea0`; final pushed head belongs to the PR/merge receipt |
| Pinned MeshCore | `e8d3c53ba1ea863937081cd0caad759b832f3028` |
| SDK | ESP-IDF 5.5.4 |
| Candidate host suite | 773 passing tests in exact Actions host job |
| Candidate CI | push `29272708844` and PR `29272709642` green; 8 manifests / 78 checksum entries verified |
| Conformance closure | false; `wire_envelope_only` |
| Release status | not ready to tag; 15 P0 failures and 16 failures overall including P1 remain |

## Live post-audit reconciliation

- Live `main` is `3e712916a05931fd10998f51d7f616e506daeeb4`; WP-00 is merged.
- WP-01 is `hardware_green` with `proof_banked=true` on exact source `092293f2311a24c9899bc9bf343ab014c4ba0411`, but `implementation_merged=false` until WP-02 lands PR #80.
- Exact push/PR Actions runs `29272708844` / `29272709642` are green. The Actions host job reports 773 passed, and all 8 manifests / 78 checksum entries verify.
- The accepted pair passed inserted-card stability, 10/10 physical removal/reinsert cycles, 5/5 retained reboots, and a 7,207.089-second six-segment active-storage soak with retained-worker stack floor 7,976 bytes. It used no Public RF and no SD formatting.
- WP-02 is `in_progress`. PR #83 banks the proof and completion state. Its exact pushed head and Actions runs are recorded in the PR/merge receipt and then folded into the next exact-main ledger refresh, rather than attempting to embed a commit's own hash in its contents.

### WP-01 canonical exact-source receipts

| Evidence | SHA-256 |
|---|---|
| Exact-pair provenance | `2decf8ad60b73e71bbb09b489adba8fd827856a8daf00c376c5a9ba5354e451e` |
| Inserted-card stability | `a038ee7ca371c4ee404493c721a343ef54e7bbd55b08273c8dc833d9d0203aef` |
| Removal/reinsert, 10/10 | `3a3882038fec2497529d281f3c2b9b7468c1e62dcca7962ca3b9492125f0fad1` |
| Retained reboot matrix, 5/5 | `db6cab3020bfa8ef575bd6a59c61d1277a8e72e1e73eb987248817199797a986` |
| Active-storage soak | `caf19395d0e1a175f6fa13c2550bc8693297661756bf339ba4acec63da2699b9` |
| WP-01 aggregate | `994f4e5ac7b9e0e8bdb57aad7715f52a99294a1841847860e2ce2f70bd6e2277` |

This proof closes only WP-01's narrow source gate. The exact integrated/frozen candidate still needs the broader no-card, unusable/non-FAT32, representative-card/size, Seeed, electrical, power-loss, cold/warm boot, 12-hour, UI, Map, and RF matrices.

## Branch and PR evidence

### PR #62

- Base: `main`
- Purpose: bounded built-in current-view Map and UI hierarchy
- Large cross-cutting change; must land first.
- Local-only reconciliation rehearsal `7648611c412e7f4658f5d14b43ba530744d96160` passed 423 full / 80 focused host tests. It is not remote exact Actions or hardware proof and must absorb the final PR #83/main result before push.

### PR #64

- Base: PR #62 branch
- Purpose: ESP-IDF 5.5.4, dependency lock, BSP compatibility, Wi-Fi/Map/platform integration
- Must be retargeted to `main` after #62 lands and rechecked.
- Local-only reconciliation rehearsal `c5886de1e2988b2097034183d5e39bb3aec88344` passed 575 full / 128 focused host tests. It is not remote exact Actions or hardware proof.

### PR #80

- Base: PR #64 branch
- Purpose: first MeshCore envelope conformance slice, retained durability, release evidence, current false-`no_card` repair
- Predecessor hardware failures:
  - route-persistence task stack overflow;
  - post-ack WDT;
  - inserted card temporarily reported `no_card`.
- Exact source head `092293f2311a24c9899bc9bf343ab014c4ba0411` has now passed the WP-01 exact-pair source proof listed above. It remains unmerged and does not qualify the later integrated SHA.
- Local-only full-stack reconciliation rehearsal `341a3abf4db4c52acf5859e396f25e7adb4cbab1` passed 787 full / 302 focused host tests. It is not remote exact Actions or hardware proof.

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
- the narrow `092293f` WP-01 exact-pair repair is physically proof-banked but not merged or requalified on the integrated line;
- broader coalescing, power-loss, schema, reset, and time work remains open.

### RP2040 bridge

**Files:**

- `main/hal/rp2040_bridge.c` — about 1,389 lines
- `firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino` — about 3,093 lines

The RP2040 owns physical SD. Current bridge firmware includes card-detect sampling, low-level SPI probes, FAT32 mount, directory creation, file protocol, atomic rename, diagnostics, and the new bounded sector-zero liveness verification. The exact bridge paired with `092293f` passed WP-01; future integrated and frozen release candidates must use checksum-bound bridge provenance and receive their own applicable qualification.

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

- The original 2026-07-12 audit performed no new physical flash or RF test. The post-audit WP-01 reconciliation above records later exact-source physical evidence and does not rewrite the original audit boundary.
- No claim from a predecessor SHA is treated as final release proof.
- GitHub source, history, issues, PRs, and workflow results were inspected through the connected GitHub/API tools because a direct local clone was unavailable in the audit environment.
- The roadmap therefore includes exact physical closure wherever source/CI cannot prove hardware behavior.
