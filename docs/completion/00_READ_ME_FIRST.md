# SIGUI Completion Audit Pack — Read Me First

This pack contains the new source-backed completion program for `n30nex/SIGUI`.

## Files

1. **`SIGUI_MASTER_COMPLETION_ROADMAP_2026-07-12.md`**  
   The full audit, findings, target architecture, 26 work packages, dependency order, hardware economy plan, acceptance gates, and release definition.

2. **`SIGUI_CODEX_5_6_ULTRA_GOAL_PROMPT.md`**  
   Paste this into Codex 5.6 Ultra as the standing goal. It tells the agent how to refresh state, use sub-agents safely, select work, test, gather exact evidence, merge, and continue until full completion.

3. **`SIGUI_EXECUTION_BACKLOG_2026-07-12.yaml`**  
   Machine-readable dependency graph, constraints, current priority, release gates, required artifacts, and completion targets. Commit a reviewed copy into the repository as the initial completion ledger.

4. **`SIGUI_AUDIT_EVIDENCE_INDEX_2026-07-12.md`**  
   Traceability notes tying the findings to current branches, PRs, code hotspots, upstream behavior, and external policy.

5. **`PACK_VALIDATION_REPORT.md`**  
   Integrity check for the YAML DAG, release profiles, forbidden-port policy, and refreshed GitHub snapshot.

6. **`SHA256SUMS.txt`**  
   SHA-256 manifest for the handoff files in this pack.

## Recommended handoff

- Put the roadmap and YAML backlog in the repository under `docs/completion/`.
- Give Codex the goal prompt.
- Make WP-01 the first task unless PR #80 has already been physically proven and merged.
- Do not create new broad work on top of the old PR #62/#64/#80 stack.
- Replace all COM11 helper defaults before any unattended RF automation.
- Keep this pack as an audit snapshot; update the repository ledger, not this historical copy.
