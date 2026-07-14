# SIGUI Audit Evidence Index

**Audit date:** 2026-07-12; live reconciliation refreshed 2026-07-14
**Repository:** `n30nex/SIGUI`  
**Purpose:** make the master roadmap traceable to the exact repository state inspected.

## Snapshot

| Item | Value |
|---|---|
| Live merged `main` | `ee520984d2209ae7419c02bb46d57c1549eeb56c` |
| WP-01 exact source candidate | `092293f2311a24c9899bc9bf343ab014c4ba0411` |
| Active PR stack | draft PR #98 exact head `d44e9c95f8e2b5a03366ab905782e6170057d606` implements schema-v5 durable inbound-DM ACK correlation; route selection is isolated directly behind it at local exact head `a7a2bdca8f1e01977e2afe860883deb65bf982b9` |
| Candidate integration state | PRs #62, #64, #80, and #84-#97 are merged; WP-03 proof is banked, WP-04 production/oracle Ed25519 integration and the exact IDF receipt fix are merged while ACK/route/TRACE/admin remain, and frozen-candidate physical qualification remains open |
| Proof-ledger PR | #83 head `a2da533310c7b2e6898439684922b9cd86896b59`, merged as `c3f9106ea9b88c491889cd8dea9ad883a0d72180` |
| Pinned MeshCore | `e8d3c53ba1ea863937081cd0caad759b832f3028` |
| SDK | ESP-IDF 5.5.4 |
| Candidate host suite | 935 passing tests and 32 checksum-contract tests in exact-main Actions `29313013731` |
| Candidate CI | Exact PR #97 and merged-main runs are green; production/oracle/runtime Ed25519, exact IDF receipt, conformance/fuzz, ESP32 firmware, package, SBOM, provenance, notices, and downloaded checksums pass |
| Conformance closure | false; defined-arithmetic signed-advert production/oracle integration is merged, while ACK, route, TRACE, admin, retained-state, and real-peer proof remain |
| Release status | not ready to tag; Actions dry audit remains fail-closed with 33 P0 failures / 35 overall; the exact IDF-version receipt failure is closed, while all other applicable gates remain open |

## Live post-audit reconciliation

- At the PR #92 checkpoint, `977cbd2590ddd0b73fe24274ba45f9d1e4051a37` merged the fail-closed WP-04 oracle foundation. Exact PR head `a1aa3567567642f8479c64098414a5174359bab4` passed push/PR Actions `29305643722` / `29305644969`: 906 host tests and 28 checksum-contract tests per run, 931 oracle cases, 864 bidirectional packet vectors, 100,000 fuzz inputs, zero enabled-sanitizer/memory findings, exact 22-command receipt binding, ESP32 firmware, and release packaging. All 10 downloaded artifact archive digests matched GitHub, all 6 available checksum manifests / 88 entries passed, and 430 extracted files were SHA-256 indexed. RP2040 correctly skipped for this ESP32-only slice. WP-04 and issue #65 remain open; the exact three-source Ed25519 `shift-base` exception is declared by `BLK-WP04-ED25519-SHIFT-UB-20260714` and blocks release, not parallel implementation.
- Exact merged-main Actions `29306243447` also passed 906 host tests, 28 checksum-contract tests, conformance/fuzz, ESP32 firmware, and packaging. All 5 downloaded archives matched GitHub API digests, all 3 checksum manifests / 44 entries passed, and 215 extracted files were independently hashed with inventory SHA-256 `519c3a0af21c2c50120c64e35c5fc9e3c5bdb96fb9c65f00c5b3864907bdaa4b`. Portable aggregate `docs/completion/evidence/wp04/oracle_foundation_977cbd2590ddd0b73fe24274ba45f9d1e4051a37.json` (SHA-256 `a4ccb0dde40b87fb3646149579a10c78e3778fcf0cf5885a46c02c1ac7f9b2ff`) records the exact run, artifact IDs and digests, semantic counts, release audit, and open blocker without claiming WP-04 closure.
- PR #94 head `be36fe10b1ac34966b83f2a73d43d17df9f7d2c7` merged as `2b878566d846f4db68ddb40f853cc63148f4a024`. Exact merged-main Actions `29307225130` passed 909 host tests and 28 checksum-contract tests; all five downloaded archives matched GitHub and all 44 manifest entries passed across 215 files / 72,316,214 bytes. The 215-expression overlay passed independent exception-free Clang 18 ASan/UBSan differential and RFC 8032 checks. Production and the main oracle still use the legacy sources, so the shift-UB blocker remains open.
- PR #93 head `e0f44a20fe52c795189c3bc40f0c17238aa764e2` merged as live main `b49a7b3a18379fdb6e4fe95c46784e8e2ea79d2e`. Push/PR Actions `29306794376` / `29306795470` each passed 914 host tests and 28 checksum-contract tests. Exact merged-main Actions `29307595930` passed 917 host tests and 28 checksum-contract tests; all five archive API digests and all 44 manifest entries passed across 216 extracted files / 72,353,618 bytes with ordinal canonical inventory `9117424199086903f96138436d686a031cafcfc6636c857f0e25b5e782b68df9`. Signed-advert receipt `e2c9de18c96b9f33161b2e60292cce35ff595d0d4913103ea33bf960ea68fc41` proves 9 assertions / 27 commands, all 35 repository pins, all 17 external sources, balanced 5/5 allocation/release, and duplicate/bad-signature/self suppression. Conformance receipt `3af1e480bd15f2054908f1ea6cd0bf88a41faeeb3e320a964524d50b8d69cec9` proves 100,000 fuzz inputs, 864 vectors, zero findings/failures, and zero executed sanitizer/memory errors. Portable aggregate `docs/completion/evidence/wp04/signed_advert_ed25519_foundations_b49a7b3a18379fdb6e4fe95c46784e8e2ea79d2e.json` has SHA-256 `0203d464868d46fde17cde13b391ac12af4f5089bae32db6cf2898776f192cef`. WP-04 remains open.
- PR #96 head `8afe6cf3fae799b6685bd7abe8da032e31d91dd3` merged as `83a811247aa79a379ee810da7489c90c62112fee`. Push/PR Actions `29310422653` / `29310424258` each passed 929 host and 32 checksum-contract tests; all 10 API ZIPs and 92 nested entries verified. Exact merged-main Actions `29311228360` repeated 929/32 plus the Actions-only ESP32 build. All five API ZIPs and 46 nested entries verified across 219 files / 72,428,696 bytes with inventory `d5d25e7521181009080125a532b4773fd7ca8514b98349796ab397f1d480aeef`. The production build, main oracle, and signed runtime select the reviewed 215-expression overlay with `full_ubsan_clean=true`, zero sanitizer exceptions/errors, 864 vectors, 100,000 fuzz inputs, and raw/canonical signed-runtime binding. SBOM `762bf41ad8ea23daf8149d1eccd0a775717cafd991a0d18629410177facb2c9c`, provenance `2a4995a9af3a444807d1806f5f2b24b4bdf0acde4faa72d1d48b5d1dd0ef01aa`, and ORLP notice/Zlib records pass. Portable aggregate `docs/completion/evidence/wp04/production_oracle_ed25519_integration_83a811247aa79a379ee810da7489c90c62112fee.json` has SHA-256 `d75c1f948784faecb07b49ed423732542e91bc3947f68e76f831dad0413521f2`. `BLK-WP04-ED25519-SHIFT-UB-20260714` is closed; WP-04 remains open for ACK, route, TRACE, admin, retained-state, RF, and physical proof.
- PR #97 head `27c7a32e3ad51313f96d7e678dadef4a24101e75` merged as live main `ee520984d2209ae7419c02bb46d57c1549eeb56c`. Push/PR Actions `29311854987` / `29311857208` and exact merged-main Actions `29313013731` passed 935 host and 32 checksum-contract tests. All five merged-main API ZIPs and 46 entries verified across 219 files / 72,428,299 bytes with inventory `b3eac68f12ab9fd7192319a3bfdfac56c9104f8d0a5a5f8b7c416e8e838cc606`. `idf-version.txt` is exactly one 15-byte `ESP-IDF v5.5.4` LF-terminated line and real nonzero capture mutations leave no usable receipt. Portable aggregate `docs/completion/evidence/wp24/idf_version_receipt_ee520984d2209ae7419c02bb46d57c1549eeb56c.json` has SHA-256 `f21abb17403f432c17bf19cc052cd185b05a39c705a3a984e73f5fcb6a547fa5`. `BLK-RELEASE-IDF-VERSION-RECEIPT-20260714` is closed narrowly; WP-24 and Full Release remain open.
- Historical WP-03 checkpoint `e79fb56160914f4483515f4f70998aa2f8961496` passed exact merged-main Actions `29300795502` with 891 host tests plus 28 checksum-contract tests and strict verification of all five artifacts, 3 manifests / 44 entries, and 214 files; canonical tree is `f9761f28bf4b5fd526ec2fd1146d196da9d7299895eb488a38d4b02cb16b8738`. Root hashes are firmware `6b2c9bea1ae6221bacd00eaa24ec6c1ed167f11bafe0b57368f861b87c6808eb`, build inputs `521cfebb807cbf2ba214ee7309ebc277731995e404181246b584db2e6e120233`, and release `499d8cec2784a7d08f76e888e675ee17a135324c9de06e500fd878c1dedfec18`; SBOM and provenance are banked in the WP-03 aggregate. RP2040 correctly skipped on that merged-main run.
- PR #90 exact-source rebuild comparison is closure evidence. Full-release runs `29300805114` and `29300806682` each strict-verified 9 manifests / 89 entries / 257 files. Receipt `F:/SIGUI-evidence/actions/pr90-e79fb561-full-release-comparison-exact-29300805114-29300806682.json` has SHA-256 `babb5d8c42133ab2e0d42fc38633fbba9976c17cfd42de1eb05b5559253ac11f`, reports `reproducible=true`, and has no failures. Portable aggregate `docs/completion/evidence/wp03/release_reproducibility_e79fb56160914f4483515f4f70998aa2f8961496.json` has SHA-256 `ff97327ae7a6c7e90f2db8905ffe344dbe73d0fb75065bbf6f66294b5c72e264`; WP-03 is `merged` / `proof_banked=true`. The older `a03bdb8...` `invalid_sbom` receipt remains fixed negative history. A stale GitHub check status and an initial wrong-worktree `source_sha_mismatch` guard receipt are recorded as non-product anomalies, not failures.
- Exact merged-main run `29290978741` passed 795 host plus 24 checksum-contract tests and downloaded strict verification of 8 manifests / 78 entries. Root manifest is `22e554bef7988f4132bd0bccc5657bb617035d1a8a9beab7c4c7b717e5e79b64`; application is `44679d6f3ee9b4bd2deeb4582aa52f813064de994cf2a20bd3a2dda8c00b225a`; full flash is `ad877ec984e3b36a7cb990045c754da480791cfc985f8b10e940834b1116b2cd`. The earlier `29286754864` 7/8 failure remains preserved as a negative receipt, and the coverage blocker is closed.
- WP-01 is `merged` with `proof_banked=true`; its physical evidence remains explicitly bound to exact source `092293f2311a24c9899bc9bf343ab014c4ba0411`.
- Exact push/PR Actions runs `29272708844` / `29272709642` are green. The Actions host job reports 773 passed, and all 8 manifests / 78 checksum entries verify.
- The accepted pair passed inserted-card stability, 10/10 physical removal/reinsert cycles, 5/5 retained reboots, and a 7,207.089-second six-segment active-storage soak with retained-worker stack floor 7,976 bytes. It used no Public RF and no SD formatting.
- WP-02 software integration is complete but remains `in_progress` / `proof_banked=false`. The tracked repository-relative baseline `docs/completion/evidence/wp02/integration_baseline_4ee07caf09906abdcebe8faccd95790dceb5fe88.json` (SHA-256 `39d8632d6de5bc819a96e92e970b9d280130a3014336be5d045a1f3fe07b654c`) binds PR #62/#64/#80 to their trusted heads and fails closed only for missing board, UI, SD, reboot, and Map-open physical receipts. PR #84 merged the hardened baseline/tooling as `17a948cf`; PRs #86/#87 changed release-security/CI inputs, not the runtime qualification boundary. Those physical roles remain deferred to the frozen final candidate. `BLK-WP02-EXACT-HARDWARE-ROLES-20260713` blocks WP-02 completion but not dependent implementation execution, so WP-03 and WP-04 continue while release readiness remains false.

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
- Final head `7a6ff86493042cc5617ef88c4765312cea46150d` passed 423 full / 80 focused local tests, exact push/PR Actions `29286375559` / `29286378383`, and downloaded checksum verification before merging as `570a94ad6ead0941f7acb7d9c9812c63df869e33`.

### PR #64

- Final head: `15f2a9ed99541fa059445ff3d1b06a40b4c42bee`
- PR merge-test SHA: `82b086baf534dade4407fb62210a3cb5218e8986`
- Merged as: `12d5470eca45ef6e86b6e15cf1822716e563a78e`
- Purpose: ESP-IDF 5.5.4, dependency lock, BSP compatibility, Wi-Fi/Map/platform integration, and fail-closed package checksum coverage.
- Exact push/PR/merged-main Actions and downloaded checksum verification passed as recorded above. Physical combined-candidate qualification remains open.

### PR #80

- Final runtime-integration head `ab3e7d82b6f3c4b38fd80d833e155aa941dee045` merged as `4ee07caf09906abdcebe8faccd95790dceb5fe88`; baseline/tooling head `e5d2f8a21a0cb32713a7c0b3796f1660abda788d` then merged through PR #84 as exact main `17a948cf1ad23a5d2a89419039897943028f9bce`.
- Purpose: first MeshCore envelope conformance slice, retained durability, release evidence, current false-`no_card` repair
- Predecessor hardware failures:
  - route-persistence task stack overflow;
  - post-ack WDT;
  - inserted card temporarily reported `no_card`.
- Exact source head `092293f2311a24c9899bc9bf343ab014c4ba0411` passed the WP-01 exact-pair proof listed above. That predecessor proof remains valid for WP-01 but does not qualify the later integrated SHA.
- Local-only full-stack reconciliation rehearsal `341a3abf4db4c52acf5859e396f25e7adb4cbab1` passed 787 full / 302 focused host tests. It is not remote exact Actions or hardware proof.
- Exact merged-main Actions `29290978741` and downloaded 8-manifest / 78-entry verification supersede the local integration rehearsals for software proof. Physical proof remains separately exact-SHA-bound.

### PR #86

- Head `1c5e80be662ed64c1f97fc047d6dbfc995567d1c` merged as `9acb7d0cf498793dc0bed4854cc314a2eac2ea0c`.
- Release-critical workflows, dependency locks, build inputs, packaging, provenance/SBOM, update/security, and RP2040 surfaces now resolve through fail-closed CODEOWNERS contracts.
- Exact merged-main Actions `29296258019` passed 827 host plus 24 checksum-contract tests; downloaded artifacts strict-passed 2 manifests / 36 entries.

### PR #87

- Head `655ece5d5d33356937cad24f7e23fa58decf7ff5` merged as `14182d3f198b70ceb588d9d43312bf76d8745284`.
- Remote Actions, ESP-IDF OCI input, Windows Python, wheel-only host requirements, and cross-platform build-input bytes are pinned and fail closed.
- Exact push/PR Actions `29296977946` / `29296979420` each passed 834 host plus 24 checksum-contract tests and strict-verified 3 manifests / 39 entries. Exact merge ref `74db31b1a778fc8af3b304f321f36748e85c60cf` has verified parents `9acb7d0c` + `655ece5d`.
- Explicit Actions dispatch `29296995585` executed the pinned Arduino action and all three RP2040 builds; downloaded artifacts strict-passed 9 manifests / 81 entries. This proves the CI build path only and is not physical device evidence.
- Exact merged-main Actions `29297516173` passed, with application `bd3a071739f7773a92f3dd1869f8152c4091ff5457b50e79e66a792632cfcb64`, full flash `e3b82a8f65ee29b1914ff6ee69dd2d9bd677adbfbafabae1c0d74cf9ab328ad5`, and canonical downloaded tree `78fa7f3b4a043e9deaff6ae9a23d69833fa227845964cee612a54027147dcc88`.

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
- the narrow `092293f` WP-01 exact-pair repair is physically proof-banked and merged through PR #80, but not requalified on `4ee07caf`, `17a948cf`, or the eventual frozen candidate;
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
