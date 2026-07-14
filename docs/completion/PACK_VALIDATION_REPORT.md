# SIGUI Audit Pack Validation Report

**Validated:** 2026-07-14 (post-PR #97 reconciliation)

**Live `main`:** `ee520984d2209ae7419c02bb46d57c1549eeb56c`

**WP-01 exact source:** `092293f2311a24c9899bc9bf343ab014c4ba0411`

**Proof-ledger PR:** #83 merged head `a2da533310c7b2e6898439684922b9cd86896b59` as main commit `c3f9106ea9b88c491889cd8dea9ad883a0d72180`. Exact-main Actions `29285852443` passed 388 host tests; two downloaded checksum manifests and all 35 entries verify.

## Refreshed execution state

- WP-01 is `merged`, `proof_banked=true`, and `implementation_merged=true`; accepted physical evidence remains bound to predecessor source `092293f2311a24c9899bc9bf343ab014c4ba0411`.
- Exact push/PR Actions `29272708844` / `29272709642` are green; the host job reports 773 passed and 8 manifests / 78 checksum entries verify.
- Canonical WP-01 aggregate SHA-256 is `994f4e5ac7b9e0e8bdb57aad7715f52a99294a1841847860e2ce2f70bd6e2277`.
- WP-02 is `in_progress`.
- PRs #62, #64, #80, and #84-#97 are merged. PR #97 head `27c7a32e3ad51313f96d7e678dadef4a24101e75` merged as live main `ee520984d2209ae7419c02bb46d57c1549eeb56c` and closes the exact ESP-IDF version receipt defect. Draft PR #98 exact head `d44e9c95f8e2b5a03366ab905782e6170057d606` is the active schema-v5 ACK durability slice; route selection is isolated directly behind it.
- Full integration-baseline Actions `29290978741` on `4ee07caf` passed 795 host plus 24 checksum-contract tests and strict-verified 8 manifests / 78 entries. The earlier 7/8 negative receipt remains preserved; `BLK-WP02-RELEASE-MANIFEST-COVERAGE-20260713` is closed.
- WP-02 software integration is complete. Exact PR #84 merged-main Actions `29294553135` passed 823 host plus 24 checksum-contract tests, and both emitted downloaded manifests / all 36 entries strict-verified; RP2040 correctly skipped for this host/docs-only slice. Tracked portable integration baseline `docs/completion/evidence/wp02/integration_baseline_4ee07caf09906abdcebe8faccd95790dceb5fe88.json` (SHA-256 `39d8632d6de5bc819a96e92e970b9d280130a3014336be5d045a1f3fe07b654c`) fails closed only for five physical roles. They are deferred to the frozen final candidate, so WP-02 remains `in_progress` while WP-04 continues and release readiness remains false.
- WP-03 is `merged` / `proof_banked=true`. PRs #86/#87 merged release ownership and immutable inputs; PR #88 merged deterministic SPDX SBOM; PR #89 merged unsigned SLSA v1 provenance; PR #90 merged canonical POSIX-order reproducibility. Exact main Actions `29300795502` passed 891 host plus 28 checksum-contract tests; all five artifacts downloaded and strict-passed 3 manifests / 44 entries across 214 files with canonical tree `f9761f28bf4b5fd526ec2fd1146d196da9d7299895eb488a38d4b02cb16b8738`. SBOM is `78cf8c09c3a24cad8fc0631cf7b15a3ccfa67dbdc5729f7ea6e5888a5f097037`; provenance is `7f1e682daf28c3bd53486b9a009727a1895733b388e24cab99d5ac6b07703a4c`.
- Exact-source full-release runs `29300805114` and `29300806682` each strict-verified 9 manifests / 89 entries / 257 files. Comparator receipt SHA-256 `babb5d8c42133ab2e0d42fc38633fbba9976c17cfd42de1eb05b5559253ac11f` passed with no failures. The tracked aggregate `docs/completion/evidence/wp03/release_reproducibility_e79fb56160914f4483515f4f70998aa2f8961496.json` has SHA-256 `ff97327ae7a6c7e90f2db8905ffe344dbe73d0fb75065bbf6f66294b5c72e264`. The older `a03bdb8...` `invalid_sbom` receipt is preserved fixed negative history.
- WP-04 remains `in_progress` / `proof_banked=false`. PR #92's oracle foundation, PR #94's defined-arithmetic overlay, PR #93's signed runtime, and PR #96's production/oracle/runtime/package integration are merged. The bounded receipts keep issue #65 closure false for ACK, route, TRACE, admin, retained-state, RF, and physical proof; `BLK-WP04-ED25519-SHIFT-UB-20260714` is closed.
- Exact-main Actions `29306243447` repeated 906 host plus 28 checksum-contract passes. All 5 archives matched their GitHub API digests, all 3 manifests / 44 entries passed, and 215 files were independently hashed with inventory SHA-256 `519c3a0af21c2c50120c64e35c5fc9e3c5bdb96fb9c65f00c5b3864907bdaa4b`. Portable aggregate `docs/completion/evidence/wp04/oracle_foundation_977cbd2590ddd0b73fe24274ba45f9d1e4051a37.json` has SHA-256 `a4ccb0dde40b87fb3646149579a10c78e3778fcf0cf5885a46c02c1ac7f9b2ff` and records the exact receipt set.
- PR #94 merges a 215-expression defined-arithmetic Ed25519 overlay foundation. Exact merged-main Actions `29307225130` passed 909 host plus 28 checksum-contract tests; all five downloaded archive digests and all 44 manifest entries passed across 215 files / 72,316,214 bytes with canonical inventory `8c341bf026be2309791421f8dd5f9e5fbfdfb049f6858dd96b4d07f722d97eb7`. Independent Clang 18 ASan/UBSan differential proof passed with no exception flags.
- PR #93 merges real signed-advert Identity/Mesh/Dispatcher/BaseChatMesh execution. Push/PR Actions `29306794376` / `29306795470` each passed 914 host plus 28 checksum-contract tests and verified all 10 archives / 88 entries. Exact merged-main Actions `29307595930` passed 917 host plus 28 checksum-contract tests; all five downloaded archive digests and all 44 manifest entries passed across 216 files / 72,353,618 bytes with ordinal canonical inventory `9117424199086903f96138436d686a031cafcfc6636c857f0e25b5e782b68df9`, while signed-runtime, package, SBOM, and provenance bindings all verify. Portable aggregate `docs/completion/evidence/wp04/signed_advert_ed25519_foundations_b49a7b3a18379fdb6e4fe95c46784e8e2ea79d2e.json` has SHA-256 `0203d464868d46fde17cde13b391ac12af4f5089bae32db6cf2898776f192cef`. This closes only signed-advert runtime semantics, not ACK, route, TRACE, retained-state, admin, RF, or physical closure.
- PR #96 integrates the overlay into production/oracle/runtime/package evidence. Push/PR Actions `29310422653` / `29310424258` each pass 929 host plus 32 checksum tests and verify 10 ZIPs / 92 nested entries. Exact merged-main Actions `29311228360` passes 929/32 and the Actions-only ESP32 build; five API ZIPs and 46 entries verify across 219 files / 72,428,696 bytes. `full_ubsan_clean=true`, zero exceptions, signed-runtime raw/canonical binding, SBOM, provenance, and notices all pass. Portable aggregate `docs/completion/evidence/wp04/production_oracle_ed25519_integration_83a811247aa79a379ee810da7489c90c62112fee.json` has SHA-256 `d75c1f948784faecb07b49ed423732542e91bc3947f68e76f831dad0413521f2`.
- PR #97 exact push/PR Actions `29311854987` / `29311857208` and merged-main Actions `29313013731` pass 935 host plus 32 checksum tests. Five API ZIPs and 46 entries verify across 219 files / 72,428,299 bytes. The 15-byte `idf-version.txt` is exactly `ESP-IDF v5.5.4` plus LF; nonzero capture mutations produce no usable receipt and remain audit-invalid. Portable aggregate `docs/completion/evidence/wp24/idf_version_receipt_ee520984d2209ae7419c02bb46d57c1549eeb56c.json` has SHA-256 `f21abb17403f432c17bf19cc052cd185b05a39c705a3a984e73f5fcb6a547fa5`.
- The latest Actions dry audit remains fail-closed with 33 P0 failures and 35 failures overall; `ready_for_public_release` is false. A downloaded exact-input recheck passes six scoped gates and leaves 28 P0 / 30 overall failures. The exact IDF receipt blocker is closed, but WP-24 and Full Release remain open for the other software, physical, RF, soak, packaging, and publication gates.

## Structural checks

- YAML parsed successfully.
- Work packages: **26** (`WP-00` through `WP-25`).
- Work-package IDs are unique.
- Every dependency resolves to a declared work package.
- Dependency graph is acyclic.
- WP-02 depends on WP-01's banked-proof gate. WP-02's merged implementation gate now unlocks WP-03/WP-04 without misrepresenting its missing physical qualification as closed.
- Stable Core profile includes its direct dependencies and the completion ledger.
- Full Feature profile contains all 26 work packages.
- WP-24 requires WP-00 and all Stable Core implementation domains; its Full Feature profile adds WP-20 through WP-23.
- Markdown deliverables present: **4** before this validation report.
- Forbidden-port policy has no default peer port and rejects COM8, COM11, and COM29.

## Audit and manifest boundary

This pack validates document and dependency integrity and records the narrow WP-01 exact-source proof plus the merged WP-03 reproducibility proof. It does not close the broader final SD/card/Seeed/electrical/power-loss/boot/12-hour, D1L UI, Wi-Fi, Map, RF, or exact-tag gates. The completion-pack `SHA256SUMS.txt` must be regenerated after all document updates; the PR/merge receipt records the resulting exact pushed head.
