# MeshCore DeskOS D1L Roadmap

This is the active production roadmap. The release definition and blocker analysis in [KRAB'S THOUGHTS](KRABSTHOUGHTS.MD) are incorporated here; the audit remains the detailed rationale, while this file is the ordered execution plan.

Release status: the current audit remains `ready_for_public_release=false`. `scripts/release_gate_audit_d1l.py` must report `ready_for_public_release=true` for the exact release commit before tagging. No release tag should be cut until that exact-commit audit is green, regardless of older evidence or individually passing branches.

## Frozen 1.0 Product Contract

DeskOS D1L 1.0 is a standalone, touch-first, non-forwarding MeshCore client that a normal user can operate without a serial console.

- Core release workflows are persistent identity and adverts, Public messaging, direct messaging with truthful delivery state, contacts, channels, route discovery/fallback, real trace/path diagnostics, radio settings, retained state, and recovery.
- The 1.0 target is multi-channel. A single hard-coded Public channel is not full 1.0; narrowing that scope requires an explicit product decision, matching UI removal, and release-note language rather than a silent waiver.
- Map ships in 1.0. It uses built-in OpenStreetMap Standard, a user-set location, a regional zoom-10 default, user-controlled zooms 8 through 14, one-finger pan, direct `-`/`+`/`Center` controls, and at most one visible current-view 3x3 plan at one zoom per visible generation. Completed exact-view Home-to-Map revisits use the retained rendered frame without network or SD reread; SD tile cache supplies reboot/later-session reuse. Visible attribution remains mandatory, with no provider/key/source editor, background download, multi-zoom prefetch, off-screen batch, area download, or device-side SD formatting.
- Rooms and Repeaters remain read-only observed/inferred inspectors for 1.0. Management/login controls stay absent until their protocol flows are implemented and accepted.
- BLE companion, GPS, OTA, and server administration are not release claims. Each must remain visibly experimental with safe failure behavior or be absent/disabled in the release UI until complete and release-tested.
- Every enabled action must complete end to end or return an honest, actionable error. A working-looking dead end is a release defect.
- Persisted invalid settings, missing radio/RP2040, failed Wi-Fi, no/bad SD media, and upgrade state must never cause a permanent boot loop.

## Non-Negotiable Release Rules

- Firmware builds are GitHub-Actions-only. Hardware proof uses the exact checksummed Actions artifact for the commit under test.
- COM12 is the D1L ESP32/UI console. COM16 is the RP2040/SD bridge only when that issue explicitly requires it. Do not use COM8, COM11, or COM29 for D1L validation.
- Never format SD media. Users prepare FAT32 SD cards on a computer; firmware and validation retain NVS fallback for absent or unusable media. There is no device-side SD formatting path.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Keep Public RF off unless the active RF acceptance issue explicitly authorizes it. Prefer a controlled direct-message peer.
- Keep release evidence fail-closed and commit-matched. Host contracts, simulator pixels, a different SDK build, or an older hardware artifact cannot qualify the release candidate by implication.
- One issue-sized slice at a time. Each P0 must name its code scope, host checks, Actions job, physical proof, RF/destructive constraints, artifact schema, and single closure condition.

## Ordered Production Plan

The order below supersedes the former screenshot-first queue. Platform integration comes first, then protocol correctness, runtime durability, and one frozen end-to-end qualification.

### Stage 1 - Freeze the product contract

Status: **complete in this roadmap; enforce in issues, UI, and release docs.**

- Ship multi-channel MeshCore client behavior and the bounded current-view Map.
- Keep Rooms/Repeaters observational and the D1L non-forwarding.
- Hide or clearly mark incomplete BLE/GPS/OTA/server functions.
- Publish a matching supported/experimental/absent feature matrix before the release candidate freezes.

### Stage 2 - Establish one supported integration image

Status: **in progress. This is the only current implementation priority.**

1. Integrate the ESP-IDF 5.5.4 migration from PR #64 with the Map/Wi-Fi/boot-loop work from PR #62/#13.
2. Build firmware, package, checksums, effective `sdkconfig`, dependency lock, and release-gate inputs in Actions from the combined commit.
3. Flash that exact artifact to COM12 without erasing retained settings or touching SD contents.
4. Prove on the same image: ESP-IDF identity, stable boot, Wi-Fi disabled path, enable/connect/reconnect, saved-profile reboot, internal/DMA reserve, radio readiness, RP2040/SD readiness, and no new PANIC/WDT/brownout loop.
5. Physically enter Map at the saved test location and prove useful regional detail at default zoom 10; one-finger pan; `-`, `+`, and `Center` across the bounded 8-through-14 range; at most nine requests at one zoom per committed visible generation; no request while a drag is still active; leave-Map cancellation; completed exact-view Home-to-Map retained-frame reuse with no network or SD reread; SD persistence; and reboot cache reuse without duplicate network fetches.
6. Rebase or replace the stale SD PR #36 only if current Stage 2 code still needs it; do not merge a dirty historical branch for its evidence.

Stage 2 closes only when the combined Actions artifact passes the quick COM12 integration proof. Separate green SDK and Map/Wi-Fi branches are necessary evidence but do not close it.

### Stage 3 - Establish MeshCore conformance

Status: **in progress under issue #65.** The first bounded slice extracts and
checks only the wire envelope against the pinned upstream packet code under
ASan/UBSan/libFuzzer. Its artifact is
`coverage_level="wire_envelope_only"`, must remain `closure_ready=false`, and
does not satisfy the semantic, cryptographic, retained-state, real-peer, or
full issue #65 closure requirements. See
[MeshCore Conformance Boundary](MESHCORE_CONFORMANCE.md).

1. Decide and document a narrow adapter around pinned upstream MeshCore packet/routing/chat machinery, or document why the local implementation remains and its conformance boundary.
2. Add bidirectional golden vectors for every supported packet type and channel mode against pinned MeshCore.
3. Cover advert, Public, DM encryption/decryption, ACK, multipart ACK+PATH, flood/direct headers, PATH, trace, invalid length/MAC/signature/path/payload, and self-message behavior.
4. Add bounded RX fuzzing and duplicate/replay/lifetime tests.
5. Require an intentional conformance review whenever the MeshCore submodule changes.

Stage 3 closes only when CI proves SIGUI-generated traffic is accepted upstream and upstream-generated traffic is accepted by SIGUI for the entire declared 1.0 surface.

### Stage 4 - Complete reliable messaging, contacts, channels, and routing

1. Implement inbound DM ACK with correct direct/flood timing and ACK+PATH behavior.
2. Implement the outbound DM state machine: queued, waiting for radio, transmitting, sent over RF, acknowledged, retrying, timed out/failed, and cancelled if supported.
3. Track bounded expected-ACK state; update retained history on TxDone, TxTimeout, ACK, retry, reboot recovery, and final failure.
4. Fix heard-node Message by explicitly promoting or validating the contact before opening a send-capable composer.
5. Complete contact promote/import/export/rename/favorite/mute/route/forget/key-update behavior without fingerprint-prefix ambiguity.
6. Implement persistent multi-channel configuration, selection, keyed RX/TX, history/unread separation, diagnostics identity, and official-client exchange on at least two independent channels.
7. Implement real path discovery, reciprocal paths, route age/invalidation, deterministic selection, direct failure fallback, and a real MeshCore trace request/response that never pollutes DM history.
8. Deduplicate Public, DM, ACK, advert, PATH, and trace traffic with bounded tables and no ACK/retry storms.

Stage 4 closes only with official-client/two-radio proof for success and failure paths, including lost ACK, absent recipient, radio busy, TX timeout, duplicate delivery, stale route, and reboot during a pending DM.

### Stage 5 - Harden ownership, persistence, settings, and truthfulness

1. Make one MeshCore service task own protocol/session mutation; radio callbacks enqueue compact immutable events with visible overflow counters.
2. Keep all LVGL mutation on the UI task through a bounded command/event path; finish modal, keyboard, root, live-refresh, and callback lifetime ownership where safety boundaries remain incomplete.
3. Coalesce hot NVS/SD persistence, avoid unchanged writes, measure commits per hour, and prove atomic old-or-new recovery across power loss.
4. Version every persisted schema and test supported upgrade, same-version reboot, corrupted-field recovery, factory reset, and downgrade rejection.
5. Keep protocol time monotonic and truthful; add retained network time if Wi-Fi ships, otherwise display relative/unknown time. Test ordering across reboot and clock correction.
6. Label Rooms/Repeaters as observed/inferred, remove unsupported management actions, and keep the client/non-forwarding role explicit.
7. Stress simultaneous RX, UI refresh, Wi-Fi, storage flush, rapid navigation, modal open/close, and large retained stores with no stale callback, split frame, watchdog, queue corruption, or monotonic heap loss.

Stage 5 closes only when host state-machine tests and current-commit hardware stress prove bounded ownership, write rate, recovery, and truthful UI state.

### Stage 6 - Complete hardware acceptance

1. Full RF/DM/channel/PATH/trace acceptance with an official client or controlled second radio.
2. SD FAT32 matrix: no card, runtime removal, non-FAT32/unformatted, representative 8/16/32GB FAT32 cards, RP2040 unavailable/timeout/malformed response, fallback/recovery, electrical evidence, and atomic-write/power-loss behavior.
3. Guided fresh-operator install for ESP32 plus RP2040 artifacts, flashing order, FAT32 preparation, validation, recovery, and checksums.
4. Physical review of every enabled page/action: pixels, touch targets, scrolling, keyboard, modal return, brightness, RX-active redraw, and actionable errors.
5. Minimum 12-hour idle/listening soak plus active-traffic soak covering Public RX, DM ACK/retry, adverts, UI, Wi-Fi/Map, and storage flush.

Stage 6 evidence must come from the same frozen release-candidate commit and artifact.

### Stage 7 - Package, audit, and tag

1. Build from a clean checkout with submodules and no undocumented local files.
2. Package binaries/UF2, checksums, licenses/attributions, supported upgrade instructions, factory reset, recovery, and changelog.
3. Refresh README, user guide, build decision, known limitations, test plan, release checklist, feature matrix, and physical screenshots from the frozen commit.
4. Run the final release gate and inspect every P0 result and referenced artifact.
5. Tag 1.0 only when `ready_for_public_release=true`, every enabled touchscreen workflow is complete, official MeshCore peers observe correct behavior, and failures recover without serial intervention.

## P0 Workstream Ledger

The detailed requirements are in the audit's P0.1-P0.20 ledger. The groups below are the independently closable roadmap units; existing GitHub issues must be updated or split so none of these requirements is hidden inside a broad evidence issue.

| Workstream | Audit coverage | Existing tracking | Closure proof |
|---|---|---|---|
| Supported integration image | P0.1, P0.16 | #63, #13, #12, #14 | One ESP-IDF 5.5.4 Actions artifact passes COM12 boot/Wi-Fi/radio/SD/Map/reboot proof. |
| MeshCore conformance foundation | P0.2, P0.11 | #65 | Bidirectional upstream vectors, malformed input coverage, RX fuzzing, and declared supported packet surface pass in CI. The wire-envelope-only slice is necessary but cannot close this row. |
| Reliable DM session | P0.3, P0.4, P0.5, P0.10 | #7 | Official peer proves ACK, retry, truthful states, heard-node send, duplicate suppression, timeout, and reboot recovery. |
| Contact and channel parity | P0.6, P0.7 | #20 is currently P2 and too narrow | Official-client contact import/export plus two independently keyed retained channels pass without history leakage. |
| Routing and diagnostics | P0.8, P0.9 | #7, #19 | Real trace/PATH, reciprocal learning, route expiry, direct send, and safe flood fallback pass against a compatible peer. |
| Runtime durability | P0.12, P0.13, P0.14, P0.15, P0.19 | #6, #16, #18, #22 | Single-owner protocol/UI paths, bounded queues/writes, schemas/recovery, truthful time, and hardware stress are green. |
| SD and role truthfulness | P0.17, P0.18 | #4, #11 | Full media/bridge/power/fallback matrix passes; role pages expose only evidence-backed labels/actions. |
| Frozen release qualification | P0.20 | #8 plus release gate | Current physical review, full RF, SD/electrical, idle and active soaks, package audit, and exact-tag gate are green. |

## Immediate Work Queue

1. Publish the combined source merge after its 414-test host pass.
2. Validate the combined commit in Actions and inspect the exact dependency lock, effective `sdkconfig`, firmware/package checksums, and release-gate inputs.
3. Flash the exact combined artifact to COM12 and repeat SDK identity, boot, Wi-Fi, radio-readiness, SD, health, and reboot proof.
4. Complete the still-open physical Map control proof: default zoom 10, one-finger pan, `-`/`+`/`Center` over zooms 8 through 14, bounded request/render/cancel, instant completed exact-view Home-to-Map retained-frame reuse, SD-cache reread only when the retained frame is unavailable, and reboot reuse without duplicate network fetches.
5. Reconcile GitHub labels and split the audit-only protocol/durability workstreams into issue-sized P0 trackers before implementing Stage 3.
6. Execute Stages 3-7 in order; do not return to cosmetic-only UI refactoring while a protocol or safety boundary remains open.

## Evidence Already Banked, Not Final Qualification

- Map/Wi-Fi branch `de79c9f` passes its Actions jobs and exact-COM12 boot-loop recovery, Wi-Fi enable/reconnect/reboot, bounded scan, internal/DMA telemetry, SD readiness, and ten-minute stability checks. Physical Map entry/download/cache proof is still open.
- Standalone ESP-IDF 5.5.4 branch `39a043c` passes host, firmware, package, checksum, release, dependency-lock, and effective-config checks in Actions, but has no hardware qualification. Its evidence does not qualify this integration result; the combined commit needs fresh Actions and COM12 proof.
- The integration source merge preserves both safety contracts and passes 414 host tests plus Map simulator/dry-run checks. It remains unqualified until its Actions jobs, checksums, effective configuration, and exact-COM12 checks pass.
- Existing COM12 UI hierarchy/pixel artifacts, SD canaries, and short soaks remain useful regression evidence, but they do not replace current-commit screenshots, the final SD/RF matrices, or the frozen-candidate soak.

## Validation Notes

- The active UI boundary includes `ui_navigation.c`, `ui_chrome.c`, `ui_home.c`, `ui_settings.c`, `ui_keyboard.c`, `ui_screen.c`, and `ui_modal.c`; remaining refactors must close an ownership or runtime-safety boundary.
- `tools/ui_simulator.py` provides deterministic 480x480 schema/pixel checks and must retain large-mesh coverage.
- Hardware Map automation remains network-suppressed. Only a physical Map entry authorizes the bounded current-view fetch; probe counters must remain unchanged.
- Each physical pan or zoom may create only one visible 3x3 generation at its selected zoom. Drag motion itself must remain network-silent, and returning from Home to an already completed exact view must reuse the retained frame without replaying SD reads.
- `ui_capture_d1l.py` reconstructs the firmware-maintained RGB565 shadow for exact pixel evidence, but final release also requires manual physical photos/touch review.
- Serial route diagnostics remain `routes`, `routes detail <seq>`, `routes trace <fingerprint>`, `routes probe <fingerprint>`, and `routes clear`.
- Serial diagnostics and test hooks are evidence surfaces, not substitutes for user-complete touchscreen workflows.

## Active Docs

Use [docs/README.md](README.md) as the documentation index. Keep [README.md](../README.md), [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md), [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md), [TEST_PLAN_D1L.md](TEST_PLAN_D1L.md), and the release-gate schema aligned whenever scope or evidence changes.
