# MeshCore DeskOS D1L Core 1.0 — 24-Hour Status

This is the live, fail-closed execution ledger for the Core 1.0 release sprint.
The authoritative product boundary, roadmap, and work graph are:

- `SIGUI_CORE_1_0_PRODUCT_CONTRACT_2026-07-18.md`
- `SIGUI_24H_AUDIT_AND_ROADMAP_2026-07-18.md`
- `SIGUI_24H_EXECUTION_BACKLOG_2026-07-18.yaml`

## Sprint identity

| Field | Value |
|---|---|
| Sprint start | `2026-07-18T13:38:38-04:00` |
| Hard elapsed deadline | `2026-07-19T13:38:38-04:00` |
| Repository | `n30nex/SIGUI` |
| Integration branch | `release/24h-core` |
| Integration worktree | `F:\SIGUI-worktrees\release-24h-core` |
| Starting `origin/main` | `846f728dd3faded85451c6d39ba6a07cb8ca7f44` |
| Starting main Actions run | `29652673963` (`d1l-ci`, success) |
| Target tag | `v1.0.0` |
| Release profile | `core_1_0` |
| Core readiness | `false` |
| Full-feature readiness | `false` |
| Candidate SHA | not frozen |
| Candidate Actions run | not started |
| SD history mode | `conditional` |

## Immutable safety rules

- Firmware builds run only in GitHub Actions.
- D1L app/console/flash work uses `COM12`.
- `COM16` is allowed only when the final SD/RP2040 qualification requires it.
- `COM8`, `COM11`, and `COM29` are forbidden.
- Never format SD.
- Normal candidate flashing is non-erasing.
- Never automate transmission on the default Public RF channel.
- Predecessor, simulated, dry-run, source-only, or manually edited evidence cannot close an exact-candidate gate.
- Unsupported Core features must be unreachable and side-effect-free.
- Tagging is forbidden until the exact Core audit reports
  `core_release_ready=true`, all Core P0 gates pass, and no Core
  crash/data-loss/security P1 remains.

## Product decisions

- Core 1.0 supports only the capability matrix in the product contract.
- BLE is unavailable. PR #199 is explicitly excluded from this release.
- Map, user Wi-Fi control, multi-channel management, administration,
  Observer/MQTT, signed SD/OTA update, GPS/location, mutable terminal,
  advanced QR/emoji, and user-facing TRACE/PATH tools are unavailable.
- SD begins as conditional. It becomes `supported_optional` only after the
  exact paired-candidate matrix passes; otherwise it becomes `disabled`, the
  RP2040 payload is omitted, and retained NVS remains authoritative.
- PR #197 will be integrated as reviewed `.c`/`.h`/native-test substance.
  Generated manifests will be regenerated against this branch rather than
  accepting stale hashes from its conflicting branch.

## Current GitHub snapshot

Snapshot refreshed at `2026-07-18T13:46:38-04:00`.

| Item | State |
|---|---|
| `origin/main` | `846f728dd3faded85451c6d39ba6a07cb8ca7f44` |
| Main `d1l-ci` | run `29652673963`, success |
| PR #197 | open, non-draft, conflicting; Actions `29650366501` success |
| PR #199 | open draft, conflicting; Actions `29650450872` firmware failure; excluded |
| Open release-blocker P0 issues | 21; tracker not yet Core/deferred classified |
| Existing `release/24h-core` before sprint | none |
| Existing `v1.0.0` tag/release | none |
| Active/queued Actions runs at snapshot | none |

## File ownership

| Role | Work packages | Exclusive ownership |
|---|---|---|
| Lead/integrator | R0, R1, R9–R16 | integration branch; `main/app/release_profile.*`; freeze; Actions; hardware; release decision |
| Agent A — Core UI | R2 | `main/ui/**`, including sole ownership of `main/ui/ui_phase1.c` |
| Agent B — storage/persistence | R4, R5 | `main/storage/retained_blob_store.*`; coordinates profile fields with lead |
| Agent C — core Mesh/RF | R6 | `main/mesh/**`; sole implementation owner of `main/mesh/meshcore_service.c` |
| Agent D — command admission | R3 | `main/comms/usb_command_parser.*`, `main/comms/usb_console.*`; coordinates app-model edits with lead |
| Agent E — QA/package/audit | R7 | new Core runners/audit/tests and `scripts/package_release_d1l.py`; no firmware runtime files |
| Agent F — independent review | R8 | review notes, operator command sheet, receipt validation; no implementation by default |

No sub-agent may edit this ledger, completion ledgers, or release status.
The lead reviews and cherry-picks each bounded commit in dependency order.

## Work-package status

| ID | Status | Evidence / next action |
|---|---|---|
| R0 | in progress | Clean integration worktree created at exact starting SHA; contract pack and this ledger are being committed |
| R1 | pending | Lead central immutable release profile |
| R2 | pending | Agent A Core UI surface |
| R3 | pending | Agent D command/background-task admission; depends on R1 |
| R4 | pending | Agent B bounded PR #197 substance |
| R5 | pending | Agent B conditional SD/NVS fallback; depends on R1 |
| R6 | pending | Agent C focused DM/contact-path review |
| R7 | pending | Agent E Core smoke/package/audit; depends on R1/R2/R3/R5 |
| R8 | pending | Independent integrated diff/evidence review |
| R9 | pending | Focused checks plus one full host suite and candidate freeze |
| R10 | pending | One final `d1l-ci`, download all artifacts, verify all checksums |
| R11 | pending | Exact non-erasing COM12 flash, UI/boot/persistence gates |
| R12 | pending | Controlled peer RF/DM; `public_rf_tx=false` |
| R13 | pending | Exact SD pass or disable/NVS fallback |
| R14 | pending | 60-minute active plus 30-minute idle exact-candidate soak |
| R15 | inactive | Activate only for a failed Core gate |
| R16 | pending | Final Core audit and tag/release or exact no-go |

## Exact-candidate evidence ledger

All rows remain fail-closed until an exact receipt is recorded.

| Gate | Status | Exact evidence |
|---|---|---|
| Source/profile frozen | missing | — |
| Full host suite | missing | — |
| Final Actions workflow | missing | — |
| Artifact downloads/checksums | missing | — |
| Profile/package/provenance/SBOM binding | missing | — |
| Non-erasing COM12 flash | missing | — |
| Core smoke/display/touch | missing | — |
| 20-round Core UI probe | missing | — |
| Compose/scroll surfaces | missing | — |
| Five software reboots | missing | — |
| Three cold boots | missing | — |
| Retained state | missing | — |
| Controlled RF/DM/ACK/PATH/direct route | missing | — |
| SD decision | pending conditional | — |
| 60-minute active soak | missing | — |
| 30-minute idle soak | missing | — |
| Install/recovery package review | missing | — |
| Final Core audit | missing | — |

## Release decision

Current decision: **NO-GO / execution in progress**.

`core_release_ready=false` and `full_feature_release_ready=false`. No tag or
release is authorized by the evidence currently recorded.
