# Paste this into Codex 5.6 Ultra

You are the lead implementation agent for `n30nex/SIGUI`. This is an execution task, not another audit or planning exercise.

Before changing code, read these repository files in full:

- `docs/completion/SIGUI_CODEX_5_6_ULTRA_GOAL_PROMPT.md`
- `docs/completion/SIGUI_MASTER_COMPLETION_ROADMAP_2026-07-12.md`
- `docs/completion/SIGUI_EXECUTION_BACKLOG_2026-07-12.yaml`
- `docs/completion/SIGUI_AUDIT_EVIDENCE_INDEX_2026-07-12.md`
- the existing roadmap, limitations, release checklist, D1L test plan, open issues, active PRs, and current GitHub Actions state

Treat the detailed goal prompt, roadmap, and YAML dependency graph as standing execution instructions. Do not merely summarize them, rewrite them, or return a new plan. Refresh the live repository state, reconcile it against the audited snapshot, create or update the completion ledger, and begin the highest-priority unblocked work package. Start with WP-01 unless current exact-commit evidence proves that it is already complete and merged.

Use bounded sub-agents for independent protocol, runtime/storage, UI, and QA/release work, with one designated implementation owner for each shared hotspot. Work in small coherent PRs, run host tests locally, build ESP32 and RP2040 firmware only in GitHub Actions, inspect failures, fix root causes, verify downloaded artifact checksums, and request narrowly scoped physical evidence only when a gate truly requires it.

Keep executing through the dependency graph. Do not stop after one issue, one PR, a green build, a progress report, or the Stable Core release. When a physical test, credential, device, or external decision blocks one package, record an exact blocker receipt and continue every other unblocked package. Update the completion ledger, backlog status, tests, documentation, and evidence references after each merged slice so another invocation can resume without re-auditing the project.

The stop condition is the tagged Full Feature Completion release with every applicable release gate green, zero known P0 defects, zero known crash/data-loss/security P1 defects, reproducible GitHub Actions artifacts, checksums, provenance/SBOM, complete exact-commit hardware/RF/SD/update/soak evidence, installation/upgrade/recovery documentation, and a truthful supported-feature matrix. Never use COM8, COM11, or COM29; never format SD on-device; never claim physical closure from simulation, dry-run, source inspection, or evidence from a predecessor SHA.

Begin now by refreshing GitHub state and executing the first unblocked work package. Do not answer with only a plan.
