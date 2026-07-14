# SIGUI Audit Pack Validation Report

**Validated:** 2026-07-13 22:33 EDT

**Live `main`:** `e79fb56160914f4483515f4f70998aa2f8961496`

**WP-01 exact source:** `092293f2311a24c9899bc9bf343ab014c4ba0411`

**Proof-ledger PR:** #83 merged head `a2da533310c7b2e6898439684922b9cd86896b59` as main commit `c3f9106ea9b88c491889cd8dea9ad883a0d72180`. Exact-main Actions `29285852443` passed 388 host tests; two downloaded checksum manifests and all 35 entries verify.

## Refreshed execution state

- WP-01 is `merged`, `proof_banked=true`, and `implementation_merged=true`; accepted physical evidence remains bound to predecessor source `092293f2311a24c9899bc9bf343ab014c4ba0411`.
- Exact push/PR Actions `29272708844` / `29272709642` are green; the host job reports 773 passed and 8 manifests / 78 checksum entries verify.
- Canonical WP-01 aggregate SHA-256 is `994f4e5ac7b9e0e8bdb57aad7715f52a99294a1841847860e2ce2f70bd6e2277`.
- WP-02 is `in_progress`.
- PRs #62, #64, #80, #84, #85, #86, #87, #88, #89, and #90 are merged. PR #90 head `f731e465bb5dc31cb1a8a88a82dafda1e3c38577` merged as exact main `e79fb56160914f4483515f4f70998aa2f8961496`.
- Full integration-baseline Actions `29290978741` on `4ee07caf` passed 795 host plus 24 checksum-contract tests and strict-verified 8 manifests / 78 entries. The earlier 7/8 negative receipt remains preserved; `BLK-WP02-RELEASE-MANIFEST-COVERAGE-20260713` is closed.
- WP-02 software integration is complete. Exact PR #84 merged-main Actions `29294553135` passed 823 host plus 24 checksum-contract tests, and both emitted downloaded manifests / all 36 entries strict-verified; RP2040 correctly skipped for this host/docs-only slice. Tracked portable integration baseline `docs/completion/evidence/wp02/integration_baseline_4ee07caf09906abdcebe8faccd95790dceb5fe88.json` (SHA-256 `39d8632d6de5bc819a96e92e970b9d280130a3014336be5d045a1f3fe07b654c`) fails closed only for five physical roles. They are deferred to the frozen final candidate, so WP-02 remains `in_progress` while WP-04 continues and release readiness remains false.
- WP-03 is `merged` / `proof_banked=true`. PRs #86/#87 merged release ownership and immutable inputs; PR #88 merged deterministic SPDX SBOM; PR #89 merged unsigned SLSA v1 provenance; PR #90 merged canonical POSIX-order reproducibility. Exact main Actions `29300795502` passed 891 host plus 28 checksum-contract tests; all five artifacts downloaded and strict-passed 3 manifests / 44 entries across 214 files with canonical tree `f9761f28bf4b5fd526ec2fd1146d196da9d7299895eb488a38d4b02cb16b8738`. SBOM is `78cf8c09c3a24cad8fc0631cf7b15a3ccfa67dbdc5729f7ea6e5888a5f097037`; provenance is `7f1e682daf28c3bd53486b9a009727a1895733b388e24cab99d5ac6b07703a4c`.
- Exact-source full-release runs `29300805114` and `29300806682` each strict-verified 9 manifests / 89 entries / 257 files. Comparator receipt SHA-256 `babb5d8c42133ab2e0d42fc38633fbba9976c17cfd42de1eb05b5559253ac11f` passed with no failures. The tracked aggregate `docs/completion/evidence/wp03/release_reproducibility_e79fb56160914f4483515f4f70998aa2f8961496.json` has SHA-256 `ff97327ae7a6c7e90f2db8905ffe344dbe73d0fb75065bbf6f66294b5c72e264`. The older `a03bdb8...` `invalid_sbom` receipt is preserved fixed negative history.
- Exact-main Actions `29300795502` emitted a fail-closed release audit with 32 P0 failures and 34 failures overall (SHA-256 `9e32893d4478d9f13d00512631058a6a0910ab6338e35c354416654da22dc96d`). The larger count reflects the expanded exact-input and evidence gates, not a readiness regression; `ready_for_public_release` remains false.

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
