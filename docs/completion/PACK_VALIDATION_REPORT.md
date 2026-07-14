# SIGUI Audit Pack Validation Report

**Validated:** 2026-07-13 22:16 EDT

**Live `main`:** `5a4e7f2777b306ad4d7b62311fd79296d7d45747`

**WP-01 exact source:** `092293f2311a24c9899bc9bf343ab014c4ba0411`

**Proof-ledger PR:** #83 merged head `a2da533310c7b2e6898439684922b9cd86896b59` as main commit `c3f9106ea9b88c491889cd8dea9ad883a0d72180`. Exact-main Actions `29285852443` passed 388 host tests; two downloaded checksum manifests and all 35 entries verify.

## Refreshed execution state

- WP-01 is `merged`, `proof_banked=true`, and `implementation_merged=true`; accepted physical evidence remains bound to predecessor source `092293f2311a24c9899bc9bf343ab014c4ba0411`.
- Exact push/PR Actions `29272708844` / `29272709642` are green; the host job reports 773 passed and 8 manifests / 78 checksum entries verify.
- Canonical WP-01 aggregate SHA-256 is `994f4e5ac7b9e0e8bdb57aad7715f52a99294a1841847860e2ce2f70bd6e2277`.
- WP-02 is `in_progress`.
- PRs #62, #64, #80, #84, #85, #86, #87, #88, and #89 are merged. PR #88 head `8f90ad74da443c79a79b458aac313955860f3bf5` merged as `b130eb1026e8d14e4e6878bd819b5ee7f0da2165`; PR #89 head `de7eee7ecf8d29c110830f1ed813b71b4d770ebd` then merged as exact main `5a4e7f2777b306ad4d7b62311fd79296d7d45747`.
- Full integration-baseline Actions `29290978741` on `4ee07caf` passed 795 host plus 24 checksum-contract tests and strict-verified 8 manifests / 78 entries. The earlier 7/8 negative receipt remains preserved; `BLK-WP02-RELEASE-MANIFEST-COVERAGE-20260713` is closed.
- WP-02 software integration is complete. Exact PR #84 merged-main Actions `29294553135` passed 823 host plus 24 checksum-contract tests, and both emitted downloaded manifests / all 36 entries strict-verified; RP2040 correctly skipped for this host/docs-only slice. Tracked portable integration baseline `docs/completion/evidence/wp02/integration_baseline_4ee07caf09906abdcebe8faccd95790dceb5fe88.json` (SHA-256 `39d8632d6de5bc819a96e92e970b9d280130a3014336be5d045a1f3fe07b654c`) fails closed only for five physical roles. They are deferred to the frozen final candidate, so WP-02 remains `in_progress` while WP-03/WP-04 continue and release readiness remains false.
- WP-03 is `in_progress` / `proof_banked=false`. PRs #86/#87 merged release ownership and immutable inputs; PR #88 merged deterministic SPDX SBOM; PR #89 merged unsigned SLSA v1 provenance. Exact main Actions `29299321770` passed 845 host plus 25 checksum-contract tests; all five artifacts downloaded and strict-passed 3 manifests / 41 entries across 210 files with canonical tree `d192413aadb9ac6a1fb9f858c3071fa99178b20d0d51a40e2d0c382e9ca50ff1`. SBOM is `565e9144f038e07061d035fc51daf8ca80341d8dc248371a2e4369a8f7e2b065`; provenance is `b3763f05820d5ed361bb043f3eee7d9acc40200abfa6b6a0951127338b683d73`.
- PR #90 has a preserved negative comparison receipt, not closure: SHA-256 `a03bdb8112bc309a64ea185d2d29e7a09a8e41a282dd8fc22e2b466c9218a31e` records `invalid_sbom` for exact source `8b7b2c0368216bfe2f945d93323384ff6a10d7db` runs `29299613481` / `29299614693` because WindowsPath case ordering differed from POSIX ordering. Head `f731e465bb5dc31cb1a8a88a82dafda1e3c38577` contains the canonical relative-POSIX sort fix; replacement comparison and merge receipts remain pending.
- Exact-main Actions `29299321770` emitted a fail-closed release audit with 31 P0 failures and 33 failures overall (SHA-256 `3883981efd4add53249b149fbbcb0129f5d63d212de9df71f1a9852b82fac105`). The larger count reflects the expanded exact-input and evidence gates, not a readiness regression; `ready_for_public_release` remains false.

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

This pack validates document and dependency integrity and records the narrow WP-01 exact-source proof. It does not close the broader final SD/card/Seeed/electrical/power-loss/boot/12-hour, D1L UI, Wi-Fi, Map, RF, or exact-tag gates. The completion-pack `SHA256SUMS.txt` must be regenerated after all document updates; the PR/merge receipt records the resulting exact pushed head.
