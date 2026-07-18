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
| SD history mode | `disabled` |

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
- SD is disabled for this candidate because the required paired `COM16`
  SD/RP2040 target is absent and therefore cannot be qualified against the
  exact candidate. The RP2040 payload must be omitted and retained NVS is
  authoritative.
- PR #197 was integrated as reviewed `.c`/`.h`/native-test substance.
  Generated manifests were regenerated against this branch rather than
  accepting stale hashes from its conflicting branch.

## Current GitHub snapshot

Snapshot refreshed at `2026-07-18T15:15:36-04:00`.

| Item | State |
|---|---|
| `origin/main` | `846f728dd3faded85451c6d39ba6a07cb8ca7f44` |
| Main `d1l-ci` | run `29652673963`, success |
| PR #197 | open, non-draft, conflicting; Actions `29650366501` success |
| PR #199 | open draft, conflicting; Actions `29650450872` firmware failure; excluded |
| Open release-blocker P0 issues | 21; all classified for Core 1.0 |
| Existing `release/24h-core` before sprint | none |
| Existing `v1.0.0` tag/release | none |
| Active/queued Actions runs at snapshot | none |

## P0 tracker classification

Each open P0 has exactly one Core-sprint classification label. Classification
does not close an issue or substitute for exact-candidate evidence.

| Classification | Issues | Core handling |
|---|---|---|
| `core-blocker` | #71 | Must close through exact package, Actions, hardware, audit, and release evidence |
| `evidence-only` | #7, #8, #63, #66, #67, #68, #69, #70, #74, #75, #76 | Software is present for the Core subset; exact candidate evidence remains fail-closed |
| `full-feature-deferred` | #4, #6, #11, #13, #14, #16, #73, #77, #78 | Unavailable Core surface; retained for Full Feature work |
| `stale/close after reconciliation` | none | Nothing was closed as stale during classification |

Mixed-scope issues are narrowed by the authoritative Core contract: #67
requires only basic contact-to-DM evidence while multi-channel breadth is
deferred; #68 requires internal DM PATH/route behavior while user-facing
TRACE is deferred; #74 is fixed Public plus DM only; and #76 omits unavailable
Map, Wi-Fi, BLE, and SD feature claims.

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
| R0 | complete | Contract/roadmap/backlog and ledger committed as `c09c9b4e6ee0a7eb9ea4bc405369ffdc94544265` |
| R1 | in progress | Central immutable profile, compile-time SD mode, app snapshot, version, health, settings, mesh, companion, and storage truth are wired; package wiring remains in R7 |
| R2 | complete | Initial boundary `bec47e2` plus truth/profile-matrix follow-up `8b36d99`; 227 integrated UI/profile/map tests passed |
| R3 | complete | Profile-aware command admission integrated as `b356e45` plus disabled-SD fail-closed follow-up `cbbfbed`; 52 integrated command/profile/source-pin tests passed |
| R4 | complete | Reviewed PR #197 substance integrated as `2824d63c6c779560ce0ad1ca787e230634b5c3ff`; 47 storage tests and 7 integrated source-pin checks passed |
| R5 | complete | Profile-bound SD admission integrated as `0c72561`; Core conditional/disabled routes retained data to NVS and cannot activate SD |
| R6 | complete | 109 focused Mesh/DM/contact/route tests passed; one Actions-only libFuzzer case skipped; no `main/mesh/**` P0 or code change |
| R7 | in progress | Agent E isolated at `F:\SIGUI-worktrees\24h-core-qa`; Core smoke/UI probe/audit/package and exact RF admission |
| R8 | in progress | Independent integrated diff/evidence review found and lead repaired missing exact build identity in `health`; review continues |
| R9 | pending | Focused checks plus one full host suite and candidate freeze |
| R10 | pending | One final `d1l-ci`, download all artifacts, verify all checksums |
| R11 | pending | Exact non-erasing COM12 flash, UI/boot/persistence gates |
| R12 | pending | Controlled peer RF/DM; `public_rf_tx=false` |
| R13 | in progress | SD disabled/NVS fallback selected because COM16 is absent; exact package/device truth remains |
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
| SD decision | selected disabled | `COM16` absent; compile-time default changed to `disabled`; exact package/device confirmation pending |
| 60-minute active soak | missing | — |
| 30-minute idle soak | missing | — |
| Install/recovery package review | missing | — |
| Final Core audit | missing | — |

## Release decision

Current decision: **NO-GO / execution in progress**.

`core_release_ready=false` and `full_feature_release_ready=false`. No tag or
release is authorized by the evidence currently recorded.

## Execution updates

### `2026-07-18T14:10:06-04:00`

- R0 froze the exact authority pack on the integration branch.
- R1 added the deterministic `core_1_0` CMake default, immutable feature
  authority, compile-time `conditional` SD mode, app-snapshot profile binding,
  and a four-case native profile/SD matrix.
- Focused R1 validation: 67 tests passed across release-profile, UI shell,
  storage hierarchy, diagnostics, release metadata, CI workflow, and Actions
  security contracts.
- Three isolated worktrees are active for R2, R4, and R6. No firmware build,
  serial port, RF transmission, or SD operation has occurred.

### `2026-07-18T14:20:43-04:00`

- R4 integrated the reviewed runtime/header substance from PR #197, added
  fail-safe regression coverage for NVS comparison-read failures, and
  regenerated its transitive manifest pins from the integrated tree.
- Integrator validation for R4 passed 47 retained/factory-reset/storage tests
  plus seven exact source-pin checks. Initial pin failures correctly exposed
  R1's `main/CMakeLists.txt` and app-model changes; the generated hashes were
  refreshed from current canonical-LF sources and then passed.
- R6 completed as a no-code result: 109 focused MeshCore, DM, ACK/retry,
  duplicate/replay, contact authorization, route/PATH, and runner-contract
  tests passed; one libFuzzer execution remains intentionally Actions-only.
- R6 found an exact-evidence gap outside its ownership:
  `rf_full_acceptance_d1l.py` accepts a commit argument without verifying the
  device build and permits a non-`COM12` D1L port. R12 remains fail-closed;
  the QA/package slice must add exact device-version and `COM12` admission.
- R3 command admission and R5 conditional-SD/NVS fallback are now active in
  fresh worktrees based on the integrated profile. No firmware build, serial
  port, RF transmission, or SD operation has occurred.

### `2026-07-18T14:41:48-04:00`

- R5 integrated a release-profile admission check at the retained-store
  boundary. In Core `conditional` or `disabled` mode, even a fully capable
  bridge cannot claim or activate SD; retained reads, writes, and erases route
  to NVS. `supported_optional` still requires every runtime prerequisite.
- R2 integrated the exact Core dock (`Home`, `Messages`, `Nodes`, `Packets`,
  `Settings`), Public-only channel projection, unavailable-screen/deep-link
  rejection, NVS-only storage wording, and controller/timer admission gates
  for excluded features.
- Integrated R2 validation passed all 212 `test_ui_*` tests and 19 focused
  release-profile, retained-store, and map-marker source-pin tests. The two
  UI source hashes used by the map-marker oracle were regenerated from the
  integrated files.
- Read-only hardware preflight now sees `USB-SERIAL CH340 (COM12)` present and
  OK. `COM16` is absent. No serial port has been opened, no firmware has been
  flashed, no RF transmission has occurred, and no SD operation has occurred.
- Independent R2 review found no crash, null-controller, or excluded-timer
  blocker, but held completion for four contract-truth cleanups: remove a Core
  room-conversation claim, avoid claiming a notification system, omit Map
  storage rows when Map is unavailable, and avoid inherited SD warning styling
  in NVS-only mode. The UI owner is implementing a bounded follow-up with
  stronger Core-compiled controller coverage.

### `2026-07-18T14:53:45-04:00`

- R3 integrated an ordered profile-aware USB command allow/deny table before
  command handlers, the exact `ESP_ERR_NOT_SUPPORTED`/profile/feature response,
  truthful read-only unavailable-feature status, and release profile plus SD
  mode fields in version, health, settings, mesh, companion, and storage
  output.
- A lead review follow-up added disabled-SD admission for media and RP2040
  mutations while retaining conditional-mode qualification and bounded
  read-only status/diagnostic probes. Core help no longer offers
  `storage force-nvs off`.
- The console's MeshCore-oracle and USB-parser canonical-LF SHA-256 pins were
  regenerated from the integrated source. Integrated validation passed 32
  command/profile/connectivity/channel/storage tests plus 20 USB-parser/oracle
  tests; one documented Actions-only oracle execution was skipped.
- R7 Core smoke, Core UI corruption probe, separate Core audit, package
  capability truth, and exact COM12/firmware admission are active in an
  isolated worktree. No build, serial-port open, hardware mutation, RF
  transmission, or SD operation occurred.

### `2026-07-18T15:03:31-04:00`

- GitHub state was refreshed again before candidate preparation:
  `origin/main` remains
  `846f728dd3faded85451c6d39ba6a07cb8ca7f44`, PR #197 and excluded draft
  PR #199 remain the only open pull requests, and main `d1l-ci` run
  `29652673963` remains successful.
- Read-only PnP preflight still reports only the allowed D1L endpoint,
  `USB-SERIAL CH340 (COM12)`, present and OK. The paired `COM16` SD/RP2040
  target is absent.
- The candidate SD decision is therefore fail-closed: compile-time SD history
  defaults to `disabled`, retained NVS is authoritative, and R7 packaging must
  omit the RP2040 payload. Exact package and flashed-device truth remain
  required before R13 closes.
- No serial port has been opened, no firmware has been flashed, no RF
  transmission has occurred, and no SD operation has occurred.

### `2026-07-18T15:06:46-04:00`

- The independent R8 review found a candidate-freeze blocker: `version`
  exposed the exact 40-hex build commit, but `health` exposed only release
  profile and SD mode. R7 intentionally requires both preflight commands to
  bind to the exact candidate before any hardware action.
- The shared profile/status field emitter now includes
  `D1L_BUILD_GIT_COMMIT`, binding `health` and other profile-aware status
  results to the exact Actions source. The USB parser and MeshCore oracle
  canonical-LF source pins were refreshed to
  `87362a8d7a3591a2fc66ca31d8923f3e52ee48b2e7e185ec78aad49d81c23ff6`.
- Focused lead validation passed 51 release-profile, command-admission,
  diagnostics, source-pin, retained-profile, metadata, workflow, and Actions
  security tests; one documented Actions-only execution was skipped.
- The operator power-cycled the present device. Read-only PnP observation
  confirmed that COM12 returned present and OK. This is a pre-candidate
  hardware check and cannot count toward the exact-candidate cold-boot gate.
- No serial port was opened, no firmware was flashed, no RF transmission
  occurred, and no SD operation occurred.

### `2026-07-18T15:11:31-04:00`

- R2 follow-up `8b36d99` integrated accurate Core Home and unread-counter
  wording, Map-location filtering when Map is unavailable, neutral NVS-only
  storage presentation with real NVS faults preserved, centralized Public
  channel projection, and Core/development native matrices.
- The map-marker oracle pins for `ui_node_detail.c` and `ui_phase1.c` were
  regenerated from the integrated canonical-LF sources. One legacy source
  assertion was updated to require the new profile projector plus projected
  channel lookup instead of the removed inline expression.
- Integrated R2 validation passed all 227 UI, release-profile,
  retained-profile, and map-marker tests.

### `2026-07-18T15:15:36-04:00`

- `origin/main`, open PRs, and Actions were refreshed again. Main remains
  `846f728dd3faded85451c6d39ba6a07cb8ca7f44`; PR #197 and excluded draft
  PR #199 remain the only open PRs; no workflow was triggered by the
  `release/24h-core` backup push.
- All 21 open P0 issues were reviewed against their current bodies and given
  exactly one required classification label: one `core-blocker`, eleven
  `evidence-only`, nine `full-feature-deferred`, and zero stale closures.
  No issue was closed, and evidence-only items remain open until the exact
  candidate receipts pass.

### `2026-07-18T15:25:58-04:00`

- Independent documentation review confirmed that the previous public README
  and user guide advertised Full Feature Map, Wi-Fi, BLE, SD, channel,
  administration, TRACE, and forbidden-port workflows that are unreachable in
  Core 1.0.
- The public README and documentation index now describe only the immutable
  Core surface, SD-disabled/NVS operation, Actions-only builds, COM12, exact
  package verification, and unavailable capabilities. The prior full-feature
  user guide is preserved under an explicit historical filename and replaced
  by a Core-only user guide. Flash/recovery guidance now uses COM12 and
  distinguishes non-erasing normal flash from typed-confirm destructive
  recovery.
- Focused documentation, storage, location-boundary, DM-identity, no-format,
  and hardware-script validation passed 79 tests.
- R7 is independently removing the Full Feature and RP2040/SD documents from
  Core packages and generating exact Core install/recovery, support-matrix,
  and release README files. Final package review remains open until that
  commit is integrated.

### `2026-07-18T15:35:50-04:00`

- Independent command/documentation review found two incorrect Core console
  spellings and unsafe repository-relative install/recovery examples. The
  Core guide now uses the implemented `board` and `identity status` commands.
  Install/recovery guidance permits only the checksum-verified, package-root
  non-erasing `flash_project.ps1` on COM12 for a normal install; the separate
  full-flash helper is destructive-recovery-only and typed-confirmed.
- Focused package, documentation, and release-profile validation passed 32
  tests with one documented Actions-only skip.
- R8 also found that the package flash helper did not produce the exact flash
  receipt required by the Core audit, and that the first R7 audit draft did
  not verify raw reboot receipts or uninterrupted boot continuity across the
  active-to-idle soak seam. R7 remains open while adding bounded fail-closed
  evidence paths and tests; no candidate has been frozen.
- Read-only PnP metadata identifies the separate present COM15 endpoint as an
  `NRF52 DK` TinyUSB serial device. It is only a potential controlled RF peer:
  the port has not been opened and cannot be admitted until the exact Core RF
  runner validates its identity and policy. COM12 also remains unopened.
- No firmware was built locally, no serial port was opened, no firmware was
  flashed, no RF transmission occurred, and no SD operation occurred.

### `2026-07-18T16:04:58-04:00`

- The controlled peer on COM15 was identified through its already-running
  OpenClaw listener status rather than by taking over the port. The listener
  reports `serial.port=COM15`, `mesh_connected=true`, public-key prefix
  `024999dedfd2`, and deterministic direct-message handling: a DM containing
  `test` receives `Test OK DM.`. A bounded direct open attempt was denied
  before the port opened because the listener owns it; no RF action occurred.
- The release workflow now writes
  `d1l-host-artifacts/build-inputs/d1l-candidate-scope.json` before the exact
  host build-input checksum manifest. The receipt binds the source commit,
  run ID and attempt, canonical repository/workflow, manual-dispatch event,
  ESP32-only scope, `release_profile=core_1_0`, and disabled SD history. A
  manual false dispatch cannot be changed by last-commit path inference.
- The host workflow also runs the separate Core audit in explicitly
  non-closing dry-run mode and passes explicit Core/disabled arguments to the
  package generator. Focused workflow validation passed all eight contract
  tests. Commit `8901b42` is pushed; branch pushes do not trigger `d1l-ci`.
- R8 still holds candidate freeze for fail-closed QA gaps under active repair:
  executable COM15/OpenClaw RF adaptation, raw/manual/cold-cycle and operator
  receipt producers, package-path containment and license-notice checks, and
  the narrow pre-tag sequencing rule for release-tracking issue #71. No full
  host suite or final Actions candidate has run.
- COM12 remains present after the operator's pre-candidate power cycle. That
  cycle is not release evidence. No firmware was built locally, COM12 was not
  opened or flashed, no RF transmission occurred, and no SD operation
  occurred.

### `2026-07-18T16:30:47-04:00`

- Post-power-cycle read-only Windows checks confirm
  `USB-SERIAL CH340 (COM12)` is present with `CM_PROB_NONE` and remains mapped
  to the live serial namespace. `Win32_SerialPort` did not enumerate the CH340,
  so release admission uses the authoritative Plug-and-Play state. COM12 was
  not opened and the pre-candidate cycle remains ineligible for release proof.
- Commit `c84b190` adds executable Core-only manual UI and package
  install/recovery review producers. Both fail closed unless bound to the
  exact candidate SHA, Actions run and attempt, clean source, exact COM12/Core
  profile, and hashed automated/package evidence. They require distinct
  operator and reviewer attestations; optional PNG evidence is validated when
  supplied but is not an added Core-only prerequisite.
- Focused validation passed all nine manual UI and install/recovery producer
  tests plus Python compilation. The branch remains unfrozen while R7
  artifact/RF/flash work and R8 reboot/cold-cycle and live-defect producers
  complete. No full host suite or candidate workflow has run.
- No firmware was built locally, no serial port was opened, no firmware was
  flashed, no RF transmission occurred, and no SD operation occurred.
