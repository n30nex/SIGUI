# MeshCore DeskOS D1L Roadmap

This is the active production roadmap. The release definition and blocker analysis in [KRAB'S THOUGHTS](KRABSTHOUGHTS.MD) are incorporated here; the audit remains the detailed rationale, while this file is the ordered execution plan.

Release status: the expanded exact-main audit reports `ready_for_public_release=false`, with **31 remaining P0 failures and 33 failures overall including P1**. `scripts/release_gate_audit_d1l.py` must report `ready_for_public_release=true` for the exact release commit before tagging; older evidence or individually passing branches cannot close that gate.

Execution checkpoint: live `main` is `5a4e7f2777b306ad4d7b62311fd79296d7d45747` after PR #88 merged deterministic SPDX SBOM and PR #89 merged unsigned SLSA v1 provenance. Exact merged-main Actions `29299321770` passed 845 host tests plus 25 checksum-contract tests; all five artifacts downloaded and strict-passed 3 manifests / 41 entries across 210 files, with canonical tree `d192413aadb9ac6a1fb9f858c3071fa99178b20d0d51a40e2d0c382e9ca50ff1`. WP-03 remains `in_progress` / `proof_banked=false`: preserved PR #90 comparison receipt `a03bdb8112bc309a64ea185d2d29e7a09a8e41a282dd8fc22e2b466c9218a31e` is a negative `invalid_sbom` result caused by platform-dependent package ordering; head `f731e465bb5dc31cb1a8a88a82dafda1e3c38577` contains the canonical POSIX-order fix, but replacement comparison and merge receipts are still required. WP-01 remains banked on `092293f`; WP-02 physical qualification remains open for five frozen-candidate roles. [COMPLETION_LEDGER.yaml](COMPLETION_LEDGER.yaml) remains the machine-readable execution source, with [COMPLETION_STATUS.md](COMPLETION_STATUS.md) as its generated view.

## Frozen 1.0 Product Contract

DeskOS D1L 1.0 is a standalone, touch-first, non-forwarding MeshCore client that a normal user can operate without a serial console.

- Core release workflows are persistent identity and adverts, Public messaging, direct messaging with truthful delivery state, contacts, heard-node discovery, channels, route discovery/fallback, real trace/path diagnostics, authenticated repeater/room administration, radio settings, retained state, and recovery.
- The 1.0 target is multi-channel. A single hard-coded Public channel is not full 1.0; narrowing that scope requires an explicit product decision, matching UI removal, and release-note language rather than a silent waiver.
- Map ships in 1.0. It uses built-in OpenStreetMap Standard with a deterministic local dark render style, an explicit user-set center, a regional zoom-10 default, user-controlled zooms 8 through 14, one-finger pan, direct `-`/`+`/`Center` controls, and at most one visible current-view 3x3 plan at one zoom per visible generation. Passive signed peer-advert coordinates appear as bounded bright node markers with readable names below them; they are labelled as advert locations, never as live GPS. Completed exact-view Home-to-Map revisits use the retained rendered frame without network or SD reread; SD tile cache supplies reboot/later-session reuse. Visible attribution remains mandatory, with no provider/key/source editor, background download, multi-zoom prefetch, off-screen batch, area download, or device-side SD formatting. PRs #62/#64 supply this bounded current-view Map foundation; full live provider/cache/fetch/render/cancel/revisit qualification remains WP-19 and is not closed by landing the stack.
- DeskOS 1.0 uses a simple user-facing hierarchy: Messages opens two large Public and Direct-message destinations before chat-style conversation pages; Network is renamed Nodes and shows truthful heard/type totals plus a scan-friendly node list; More is renamed Tools/Settings; the bottom dock is compact and icon-led; and Home device state is a compact evidence-backed icon row rather than a dense status block. These requirements are tracked by #74, #75, and #76.
- Rooms and Repeaters remain read-only observed/inferred inspectors until authenticated administration is implemented, but compatible repeater and room-server login, status, command, and settings flows remain mandatory for Stable Core under WP-18/#77. Unsupported or unauthenticated actions stay absent rather than appearing as dead controls.
- D1L has no onboard GPS and DeskOS does not expose or claim local GPS support. BLE companion and OTA are not 1.0 release claims; each remains absent/disabled until explicitly implemented and release-tested.
- Every enabled action must complete end to end or return an honest, actionable error. A working-looking dead end is a release defect.
- Persisted invalid settings, missing radio/RP2040, failed Wi-Fi, no/bad SD media, and upgrade state must never cause a permanent boot loop.

## Current P0 Release Blockers

| Blocker | Current State | Next Proof |
|---|---|---|
| UI modular ownership and current hierarchy proof | Navigation, shared chrome/layout, Home state/geometry/rendering, the top-level More hierarchy/actions, keyboard policy, the screen-root boundary, and single-active modal ownership are split across focused modules. PR #60 / source `0b138be` passed push Actions `29068006554` and PR Actions `29068007961`, then proved Mesh Roles, Rooms, and Repeaters on COM12 with exact CRC matches `63DE54FB`, `FD538D71`, and `5C41EE08`, passing simulator diffs, and a clean three-round probe without Public RF or formatting. The current host slice applies the same full-height progressive disclosure to read-only Storage, Card status, and Data locations pages with a fixed no-format footer. The remaining #16/#6 work is to own the Messages renderer and Network/routes, Map, Packets, contact, and settings page domains; add UI-task command ownership; finish sheet-specific keyboard submit/cancel ownership; and deepen single-root/callback-lifetime invariants. | Build this Storage slice in Actions, capture `storage`, `storage_card`, and `storage_data` on COM12, and physically verify Back, fixed-footer, and Data locations scrolling. Do not rerun PR #60 Mesh Roles pixels for this unrelated slice. The final frozen release commit still needs commit-matched gate evidence. |
| Full RF/DM acceptance | Public and outbound DM foundations exist; full inbound DM, ACK/PATH, direct-route proof remains open. | Produce `rf_full_acceptance_*.json` with health and no-Public-command proof. |
| Remaining FAT32 matrix and SD/card/power release gates | WP-01's narrow exact-pair gate is banked on `092293f`: clean inserted-card stability, 10/10 real removals/reinsertions, 5/5 retained reboots, and a 7,207.089-second six-segment active-storage soak passed with retained-worker stack floor 7,976 bytes, `public_rf_tx=false`, and `formats_sd=false`. This closes only the WP-01 source gate. | Continue host-only WP-03/WP-04 now. After the final candidate is frozen, capture the five missing WP-02 physical roles and complete the no-card, unusable/non-FAT32, representative-card/size, electrical/power-loss, Seeed, cold/warm boot, and final 12-hour storage-aware matrices on that one exact pair. Do not reuse `092293f`, `4ee07caf`, or any other predecessor as exact proof for the tagged SHA. |
| Physical screenshots/review | Host simulator screenshots are committed under `docs/screenshots`; PR #56 / Actions `29060900359` provides current hierarchy framebuffer proof on COM12 with CRC `E72745BA`. | Add physical device photos and the manual UI review artifact. |
| Long soak | Short evidence exists. | Run 12-hour idle/listening soak on the release artifact. |

## Non-Negotiable Release Rules

- Firmware builds are GitHub-Actions-only. Hardware proof uses the exact checksummed Actions artifact for the commit under test.
- COM12 is the D1L ESP32/UI console and production RP2040 bridge control path.
  COM16 is used only for bounded RP2040 USB smoke/UF2 maintenance when that
  issue explicitly requires it; the production bridge exposes no USB CDC port.
  Do not open COM8, COM11, or COM29 as the D1L serial target; COM11 may be
  checked separately only as the independent Discord-bot radio endpoint for
  controlled DM evidence.
- Never format SD media. Users prepare FAT32 SD cards on a computer; firmware and validation retain NVS fallback for absent or unusable media. There is no device-side SD formatting path.
- MicroSD support is handled by the RP2040 side. The ESP32 direct SD path remains disabled in the D1L BSP.
- Automated RF acceptance uses only the dedicated `#test` channel or a controlled direct-message peer such as the COM11 bot; it never transmits on the default Public channel.
- Keep release evidence fail-closed and commit-matched. Host contracts, simulator pixels, a different SDK build, or an older hardware artifact cannot qualify the release candidate by implication.
- One issue-sized slice at a time. Each P0 must name its code scope, host checks, Actions job, physical proof, RF/destructive constraints, artifact schema, and single closure condition.

## Recently Closed P0 Evidence

- WP-01 exact-pair storage/reboot source proof: `092293f2311a24c9899bc9bf343ab014c4ba0411` / Actions `29272708844` and `29272709642` passed 773 host tests and 8 manifests / 78 checksum entries. Canonical SHA-256 receipts are provenance `2decf8ad60b73e71bbb09b489adba8fd827856a8daf00c376c5a9ba5354e451e`, inserted stability `a038ee7ca371c4ee404493c721a343ef54e7bbd55b08273c8dc833d9d0203aef`, removal/reinsert `3a3882038fec2497529d281f3c2b9b7468c1e62dcca7962ca3b9492125f0fad1`, reboot matrix `db6cab3020bfa8ef575bd6a59c61d1277a8e72e1e73eb987248817199797a986`, active soak `caf19395d0e1a175f6fa13c2550bc8693297661756bf339ba4acec63da2699b9`, and aggregate `994f4e5ac7b9e0e8bdb57aad7715f52a99294a1841847860e2ce2f70bd6e2277`. PR #80 is merged on `4ee07caf`; this predecessor proof remains valid for WP-01 but does not close successor physical roles or broader final SD/power/soak gates.
- Split-page/stale-column redraw proof: COM12 `ui_corruption_probe_d1l.py` from `59610ab` / Actions `28723265336` completed 20 targeted rounds with zero failures, 20 serial data-refresh events, `final_pending=false`, `public_rf_tx=false`, and `formats_sd=false`.
- Previous Home pixel proof: COM12 `ui_capture_d1l.py` from PR #33 (`c6a88e2` / PR Actions `28725692751`, merged as `e086312`) reconstructed the then-current 480x480 RGB565 Home PNG, matched firmware CRC `ED8A8E31`, and passed simulator/reference diff.
- Prior icon Home proof: PR #40 host/simulator evidence, Actions run `28731948363`, and COM12 `ui_pixel_capture_b2ca00a_actions_28731948363_COM12.json` showed the previous small-title icon launcher with colored Time/Wi-Fi/BLE/SD status icons and no Home bottom dock. The capture reconstructed a 480x480 PNG with CRC `0DB2C27A`, simulator diff `ok=true`, `public_rf_tx=false`, and `formats_sd=false`; current Home geometry changes need a matching current-commit proof before release-gate use.
- Tightened Home proof: PR #51 / Actions `29055441069` captured `ui_pixel_capture_485744e_actions_29055441069_COM12.json` after fixing the live Setup-state label. The 480x480 RGB565 capture matched firmware/host CRC `BB15A654`, passed the simulator diff, and reported `public_rf_tx=false` plus `formats_sd=false`.
- Current hierarchy proof: PR #56 / source `51258ba` / Actions `29060900359` captured `ui_pixel_capture_51258ba_actions_29060900359_COM12.json`. The 480x480 RGB565 frame matched firmware/host CRC `E72745BA`, passed the simulator diff, and reported `public_rf_tx=false` plus `formats_sd=false`; the companion three-round all-tab probe had zero failures, empty crashlog, and no stuck render state.
- Contact hierarchy proof: merged PR #59 / source `d24552e` / Actions `29064260772` captured Contact Detail (`4DE99F9D`), Contact Options (`E1728433`), and confirmation-only Forget (`5A6D0604`) on COM12. Firmware and host CRCs matched, all simulator diffs passed, and the companion three-round all-tab probe had zero failures with `public_rf_tx=false` and `formats_sd=false`.
- Mesh Roles hierarchy proof: PR #60 / source `0b138be` passed Actions `29068006554` and `29068007961`; COM12 captured Mesh Roles (`63DE54FB`), Rooms (`FD538D71`), and Repeaters (`5C41EE08`) with exact firmware/host CRC matches and passing simulator diffs. The companion three-round all-tab/data-refresh probe had zero failures, empty crashlog, `public_rf_tx=false`, and `formats_sd=false`.
- Compose/input keyboard proof: PR #35 / issue #2 captured all 12 release-blocking keyboard callers on COM12 from `fce5d82` / Actions `28727064923` using `ui_compose_keyboard_capture_d1l.py --targets all`. The artifact reports `ok=true`, `capture_count=12`, `public_rf_tx=false`, and `formats_sd=false` for Public/DM compose, Public search, Packet search, contact edit, onboarding, map location/provider, and Wi-Fi SSID/password.

## Feature Direction

- Keep MeshCore-first behavior. Public messages, DMs, routes, packet diagnostics, and retained history are the core release value.
- Keep Wi-Fi, BLE, GPS, OTA, and live map tile browsing honest as experimental or pending until hardware-proven.
- Keep SD FAT32-only. Users prepare FAT32 SD cards on a computer; there is no device-side SD formatting path. Non-FAT32 media must report guidance and retain NVS fallback.
- Keep hardware validation autonomous where possible: COM12 for ESP32 app/console, COM16 for RP2040 USB/CDC/UF2. Do not use COM8, COM11, or COM29 for D1L validation.

## Ordered Production Plan

The order below supersedes the former screenshot-first queue. Platform integration comes first, then protocol correctness, runtime durability, and one frozen end-to-end qualification.

### Stage 1 - Freeze the product contract

Status: **complete in this roadmap; enforce in issues, UI, and release docs.**

- Ship multi-channel MeshCore client behavior and the bounded current-view Map.
- Keep Rooms/Repeaters observational until #77's authenticated protocol is proven; the D1L remains a non-forwarding client while managing compatible remote services.
- Keep local GPS absent; hide or clearly mark incomplete BLE/OTA functions, and keep server-administration controls absent until #77 is implemented and proven.
- Publish a matching supported/experimental/absent feature matrix before the release candidate freezes.

### Stage 2 - Establish one supported integration image

Status: **bounded platform base established, but physical Stage 2 closure is
still open.** Stage 3 work is proceeding on top of `727bd23` without treating
that base as final qualification.

1. PRs #62, #64, #80, #84, and the WP-03 slices through #88/#89 are merged on exact `main` `5a4e7f2777b306ad4d7b62311fd79296d7d45747`.
2. Exact merged-main Actions `29299321770` built the applicable ESP32/package inputs and strict-passed 3 downloaded manifests / 41 entries; the full RP2040-inclusive integration baseline remains `4ee07caf` / `29290978741` at 8 manifests / 78 entries. Continue WP-03/WP-04 while the five WP-02 physical roles remain explicitly deferred to the frozen final candidate; green builds do not substitute for physical proof.
3. Before the replacement flash, complete the one-time retained-layout preparation only through `prepare_retained_nvs_upgrade_d1l.py`: exact code-pinned incident evidence, device identity, table artifact, current raw-region hash, quiescent scoped erase, all-`0xFF` readback, and a durable staged receipt; never erase default NVS/meta, touch SD, transmit RF, or retain a raw backup. Then flash the exact Actions artifact to COM12 without erasing retained settings or touching SD contents. Retain a P0 receipt proving the selected full commit/run/port, complete checksum manifest, exact contained offset/file set with required bootloader/partition/application roles and no extras, successful non-erasing esptool result, and an exact full `build_commit` read back by COM12 smoke.
4. Prove on the same image: ESP-IDF identity, stable boot, Wi-Fi disabled path, enable/connect/reconnect, saved-profile reboot, internal/DMA reserve, radio readiness, autonomous bounded RP2040/SD recovery without a reset storm, no intermittent false no-card state from #78, and no new PANIC/WDT/brownout loop.
5. Physically enter Map at the saved test location and prove useful local-dark regional detail at default zoom 10; one-finger pan; `-`, `+`, and `Center` across the bounded 8-through-14 range; at most nine requests at one zoom per committed visible generation; no request while a drag is still active; leave-Map cancellation; completed exact-view Home-to-Map retained-frame reuse with no network or SD reread; SD persistence; reboot cache reuse without duplicate network fetches; and passive signed-advert node markers with names below them that update without starting tile or RF work.
6. Rebase or replace the stale SD PR #36 only if current Stage 2 code still needs it; do not merge a dirty historical branch for its evidence.

Stage 2 closes only when one combined Actions artifact passes the COM12
boot/Wi-Fi/radio/SD integration checks plus physical Map controls/cache/marker
and touch/photo/power-cycle acceptance under #63/#73, and the exact ESP32/RP2040
pair passes #78 removal/reinsertion without a false no-card state. Separate
green SDK, Map/Wi-Fi, marker-source, or SD-source branches and the bounded
`727bd23` checks are necessary evidence but do not close it.

### Stage 3 - Establish MeshCore conformance

Status: **in progress under issue #65.** The first bounded slice is integrated
on top of the exact `727bd23` platform candidate and extracts and checks only
the wire envelope against the pinned upstream packet code under
ASan/UBSan/libFuzzer. Its next combined Actions artifact is
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

### Stage 4 - Complete reliable messaging, Nodes, contacts, channels, routing, and administration

1. Implement inbound DM ACK with correct direct/flood timing and ACK+PATH behavior.
2. Implement the outbound DM state machine: queued, waiting for radio, transmitting, sent over RF, acknowledged, retrying, timed out/failed, and cancelled if supported.
3. Track bounded expected-ACK state; update retained history on TxDone, TxTimeout, ACK, retry, reboot recovery, and final failure.
4. Fix heard-node Message by explicitly promoting or validating the contact before opening a send-capable composer.
5. Complete contact promote/import/export/rename/favorite/mute/route/forget/key-update behavior without fingerprint-prefix ambiguity.
6. Implement persistent multi-channel configuration, selection, keyed RX/TX, history/unread separation, diagnostics identity, and official-client exchange on at least two independent channels.
7. Implement real path discovery, reciprocal paths, route age/invalidation, deterministic selection, direct failure fallback, and a real MeshCore trace request/response that never pollutes DM history.
8. Deduplicate Public, DM, ACK, advert, PATH, and trace traffic with bounded tables and no ACK/retry storms.
9. Implement #74: a Home-themed Messages root with two large Public and Direct-message destinations, then familiar left/right chat bubbles, truthful delivery metadata, sticky compose entry, and simple empty/offline/failure states.
10. Implement #75: rename user-visible Network to Nodes, show truthful heard-node and canonical-role totals in a compact header, and render a scan-friendly bounded node/contact list without dead role actions.
11. Implement #76: rename More to Tools/Settings, add compact icon-led bottom navigation with smaller labels and 44x44 targets, rename the Home Network destination to Nodes, and replace the large Home Device status block with evidence-backed status icons.
12. Implement #77 against a pinned compatible MeshCore administration protocol: authenticated repeater and room-server login/session handling, status, allowlisted command/settings flows, confirmation for mutations, secret redaction, expiry/replay protection, and 480x480 UI acceptance.

Stage 4 closes only with official-client/two-radio proof for success and failure paths, including lost ACK, absent recipient, radio busy, TX timeout, duplicate delivery, stale route, reboot during a pending DM, the final Messages/Nodes/chrome physical UI, and authenticated compatible repeater/room administration.

### Stage 5 - Harden ownership, persistence, settings, and truthfulness

1. Make one MeshCore service task own protocol/session mutation; radio callbacks enqueue compact immutable events with visible overflow counters.
2. Keep all LVGL mutation on the UI task through a bounded command/event path; finish modal, keyboard, root, live-refresh, and callback lifetime ownership where safety boundaries remain incomplete.
3. Coalesce hot NVS/SD persistence, avoid unchanged writes, measure commits per hour, and prove atomic old-or-new recovery across power loss.
4. Version every persisted schema and test supported upgrade, same-version reboot, corrupted-field recovery, factory reset, and downgrade rejection.
5. Keep protocol time monotonic and truthful; add retained network time if Wi-Fi ships, otherwise display relative/unknown time. Test ordering across reboot and clock correction.
6. Label Rooms/Repeaters as observed/inferred before authentication, expose management only through #77's proven compatible session, and keep the local client/non-forwarding role explicit.
7. Stress simultaneous RX, UI refresh, Wi-Fi, storage flush, rapid navigation, modal open/close, and large retained stores with no stale callback, split frame, watchdog, queue corruption, or monotonic heap loss.

Stage 5 closes only when host state-machine tests and current-commit hardware stress prove bounded ownership, write rate, recovery, and truthful UI state.

### Stage 6 - Complete hardware acceptance

1. Full RF/DM/channel/PATH/trace acceptance with an official client or controlled second radio. Every automated transmission is confined to `#test` or a targeted DM; COM11 may act only as the independent Discord-bot radio peer and must never be opened as the D1L serial port. Default Public is not an automation target.
2. SD FAT32 matrix: no card, runtime removal, non-FAT32/unformatted, representative 8/16/32GB FAT32 cards, RP2040 unavailable/timeout/malformed response, fallback/recovery, electrical evidence, and atomic-write/power-loss behavior.
3. Guided fresh-operator install for ESP32 plus RP2040 artifacts, flashing order, FAT32 preparation, validation, recovery, and checksums.
4. Physical review of every enabled page/action: pixels, touch targets, scrolling, keyboard, modal return, brightness, RX-active redraw, and actionable errors.
5. Minimum 12-hour idle/listening soak plus active-traffic soak covering controlled `#test`/DM ACK/retry, adverts, UI, Wi-Fi/Map, and storage flush. Every soak artifact must include the exact command manifest; any separate Public interoperability check is explicit and non-automated.

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
| Supported integration image | P0.1, P0.16 | #63, #13, #12, #14, #73, #78 | One ESP-IDF 5.5.4 Actions artifact passes COM12 boot/Wi-Fi/radio/SD/Map/reboot plus physical Map marker/touch/photo/power-cycle and SD removal/reinsertion proof. |
| MeshCore conformance foundation | P0.2, P0.11 | #65 | Bidirectional upstream vectors, malformed input coverage, RX fuzzing, and declared supported packet surface pass in CI. The wire-envelope-only slice is necessary but cannot close this row. |
| Reliable DM session | P0.3, P0.4, P0.5, P0.10 | #66 | Official peer proves ACK, retry, truthful states, heard-node send, duplicate suppression, timeout, and reboot recovery. |
| Contact and channel parity | P0.6, P0.7 | #67 | Official-client contact import/export plus two independently keyed retained channels pass without history leakage. |
| Routing and diagnostics | P0.8, P0.9 | #68 | Real trace/PATH, reciprocal learning, route expiry, direct send, and safe flood fallback pass against a compatible peer. |
| Runtime durability | P0.12, P0.13, P0.14, P0.15, P0.19 | #6, #16, #69 | Single-owner protocol/UI paths, bounded queues/writes, schemas/recovery, truthful time, and hardware stress are green. |
| Operator messaging and Nodes UX | P0.6, P0.7, P0.13, P0.20 | #74, #75, #76 | Public/DM chat, Nodes totals/list, compact dock, Tools/Settings, and Home status icons pass simulator, exact pixels, manual touch, and truthful-state checks. |
| Remote role administration | P0.2, P0.8, P0.13, P0.18 | #77 | Pinned-protocol repeater and room-server authentication, status, allowlisted mutations, expiry, redaction, and controlled-peer proof pass. |
| SD and role truthfulness | P0.17, P0.18 | #4, #11, #70, #78 | Full media/bridge/power/fallback matrix passes with no intermittent false no-card state; role pages expose only evidence-backed labels/actions. |
| Frozen release qualification | P0.20 | #8, #71 | Current physical review, full RF, SD/electrical, idle and active soaks, package audit, and exact-tag gate are green. |

## Immediate Work Queue

WP-01's exact-pair proof is banked. The integration and release-security slices through PR #89 are merged on live `main` `5a4e7f2777b306ad4d7b62311fd79296d7d45747`; exact merged-main Actions/checksums pass. The tracked, repository-relative WP-02 baseline at `docs/completion/evidence/wp02/integration_baseline_4ee07caf09906abdcebe8faccd95790dceb5fe88.json` has SHA-256 `39d8632d6de5bc819a96e92e970b9d280130a3014336be5d045a1f3fe07b654c` and fails closed only on five missing physical roles. Keep that receipt open without stalling WP-03/WP-04, and collect those roles once on the frozen final candidate. The earlier PR #80 `341a3abf4db4c52acf5859e396f25e7adb4cbab1` result is rehearsal history only.

1. Preserve `727bd23` as a bounded predecessor hardware base and PR #64 exact Actions as build/package evidence. Neither qualifies PR #80 or closes exact COM12 board/display/touch/Wi-Fi/RF/RP2040-SD/Map/health/reboot/power-cycle work; physical #63/#73/#78 gates remain open.
2. Finish the current Stage 3 integration batch: pinned #65 wire-envelope conformance; the #69 route write-rate and retained-NVS capacity fixes exposed by exact COM12 runs; isolated non-durable UI preview slots for Public, DM, route, and packet canaries; and deterministic SD reboot/remount acceptance. Retained mirrors now move from the crowded 24 KiB default NVS into a dedicated 124 KiB top-of-flash NVS partition protected by v2 metadata markers, a committed anchor inside that partition, and a default-NVS completion sentinel. Blank first-use order is marker 1, NVS initialization, anchor commit, marker 2, legacy migration, then sentinel. Because ESP-IDF NVS initialization is not read-only and may mutate corrupt pages, ambiguous nonblank bytes with neither marker nor sentinel ownership fail closed as `external_init_required=true`; firmware neither initializes nor erases that region. A separately audited installer/hardware procedure must verify the supported predecessor layout and scope any required erase to `d1l_retained`, after which firmware sees blank first use. Marker- or sentinel-owned recovery never explicitly erases the retained partition; sentinel-only marker repair requires the existing anchor, and only marker 1 valid plus marker 2 erased plus no sentinel may resume blank state. Published pre-anchor v1 marker ownership upgrades in place without data erase, committing anchor, migration, and sentinel before both markers are rewritten as v2. Loss of both markers and the sentinel, including anchor-only NVS, remains preserved and external-init-required. Divergent downgrade/re-upgrade copies remain preserved, and release evidence must report `markers_complete=true`, `anchor_ready=true`, `sentinel_ready=true`, and `external_init_required=false`. Review the combined source first, then run one focused validation pass, one full host pass, one Actions cycle, and exact-artifact COM12 reboot proof.
3. Validate and bank the current #69 retained-store source boundary before claiming SD fallback complete. Public and DM now use schema-v4 persisted clear lineages, prefer valid device-local NVS over a foreign removable lineage, roll back a merge if the SD generation changes, and keep independently dirty/retryable SD and NVS targets. Packet compact/journal persistence now reconciles before mutation, uses idempotent sequence-derived journal offsets, validates the real zero-byte-EOF bridge reply, and keeps independent SD/NVS/journal dirty state. The periodic worker and controlled reboot cover Public, DM, packet, and route stores. The first exact `3aefb9f` COM12 artifact was rejected after late-SD reconciliation reproduced a `route_persist` stack-overflow PANIC loop. The 12 KiB worker/runner repair at `a2695fb` then passed exact Actions run `29183542983`, checksum-verified non-erasing COM12 flash, exact-commit smoke, and an SD file canary, before retained acceptance exposed a post-ack `WDT`. Shared-deadline/quiesce successor `91be531` passed exact Actions run `29185308761` and one complete controlled `SW` reboot with both flushes `ESP_OK` and healthy worker margin. Its second cleanup reboot was safely cancelled twice when the route store replayed an RP2040 cached false-`no_card` write error, while the card later recovered to `READY_SD` and no new WDT/PANIC appeared. Exact PR #80 source `092293f2311a24c9899bc9bf343ab014c4ba0411` adds the bounded read-only `CMD17` sector-zero liveness transaction and banked the narrow WP-01 exact-pair proof: inserted-card stability, 10/10 physical removal/reinsert cycles, 5/5 retained reboots, and a 7,207.089-second active-storage soak without Public RF or formatting. Keep the refreshed PR #80 integration head fail-closed until its full host suite, exact-head Actions, and downloaded checksums pass; retain the `092293f` receipt as source evidence rather than claiming it qualifies the merged head or the later replacement-card, filesystem, electrical, power-loss, and final-soak matrices. Do not treat this source slice as all of #69 or #78.
4. Extend #65 from structural envelopes to deterministic semantic and production-cryptography vectors for signed adverts, Public, DM, ACK/ACK+PATH, PATH/trace, invalid MAC/signature/length, self-message, duplicate/replay, and retained-state transitions. Keep `closure_ready=false` until the entire declared surface is proven against the pinned implementation.
5. Implement #66, then #67 and #68: reliable DM state/ACK/retry, contact and multi-channel parity, followed by real PATH/trace and direct-route fallback. Use official-peer/two-radio evidence only after the host state machines are complete.
6. Implement the truthful operator layer on those contracts: #74 Messages, #75 Nodes, #76 compact navigation/Home status, #70 role truthfulness, and #77 authenticated compatible repeater/room administration.
7. Finish #78 with physical removal/reinsertion proof on the exact ESP32/RP2040 pair, and finish #73 plus #63 physical Map control/cache/marker/touch/photo/power-cycle proof. These require manual card/touch actions and are not replaced by serial automation.
8. Close #69 ownership/schema/time recovery, the remaining #6/#16 runtime boundaries, the full SD/card/Seeed/electrical/power-loss, cold/warm boot, RF, and UI matrices, and the 12-hour idle/listening plus mixed active soaks. The WP-02 integration baseline is tracked and fail-closed; freeze and audit the exact #71 release candidate, then collect the missing physical roles and final matrices on that same candidate before tagging 1.0.

## Evidence Already Banked, Not Final Qualification

- Map/Wi-Fi branch `de79c9f` passes its Actions jobs and exact-COM12 boot-loop recovery, Wi-Fi enable/reconnect/reboot, bounded scan, internal/DMA telemetry, SD readiness, and ten-minute stability checks. Physical Map entry/download/cache proof is still open.
- PR #64 head `15f2a9ed99541fa059445ff3d1b06a40b4c42bee` merged as `12d5470eca45ef6e86b6e15cf1822716e563a78e`; PR #80 head `ab3e7d82b6f3c4b38fd80d833e155aa941dee045` merged as `4ee07caf09906abdcebe8faccd95790dceb5fe88`; PR #84 head `e5d2f8a21a0cb32713a7c0b3796f1660abda788d` then merged as `17a948cf1ad23a5d2a89419039897943028f9bce`. Exact PR #84 main run `29294553135` passed 823 host plus 24 checksum-contract tests and strict-verified both emitted manifests / 36 entries; the complete integration run `29290978741` remains the 8-manifest / 78-entry RP2040-inclusive baseline. This closes software integration and artifact bookkeeping, not physical combined-candidate qualification.
- The predecessor integration source merge plus #78 source hardening at `727bd23` preserves the safety contracts and passes 461 host tests. Actions run `29166341373` built ESP-IDF v5.5.4 with a clean release manifest; the exact ESP32 application and RP2040 UF2 checksums were verified before flashing. COM12/COM16 then passed safe bridge preflight, FAT32/READY_SD, file and retained-history canaries, reboot survival, delayed post-remount map-cache verification, and a 120-second persistent-handle soak with zero command failures, zero crash-like resets, and zero Public TX. This remains non-exact predecessor hardware evidence; physical card removal/reinsertion and physical Map/touch proof on the later integrated candidate remain open.
- Retained repair commit `a2695fb` and Actions run `29183542983` banked a checksum-verified COM12 flash, exact-commit smoke, and SD file-canary proof. Successor `91be531` / Actions `29185308761` proved one fully quiesced controlled `SW` reboot with healthy retained-worker margin, then safely cancelled a later reboot when a route SD write exposed the RP2040 cached false-`no_card` gate. Exact PR #80 source `092293f2311a24c9899bc9bf343ab014c4ba0411` subsequently banked the narrow WP-01 inserted-card, 10/10 removal/reinsert, 5/5 retained reboot, and 7,207.089-second active-storage proof. That source proof does not merge PR #80, close all of #69/#78, or replace the broader card/filesystem/electrical/power-loss/final-soak matrices.
- Existing COM12 UI hierarchy/pixel artifacts, SD canaries, and short soaks remain useful regression evidence, but they do not replace current-commit screenshots, the final SD/RF matrices, WP-19 Map lifecycle proof, or the frozen-candidate soak.

## Validation Notes

- The active UI boundary includes `ui_navigation.c`, `ui_chrome.c`, `ui_home.c`, `ui_settings.c`, `ui_keyboard.c`, `ui_screen.c`, and `ui_modal.c`; remaining refactors must close an ownership or runtime-safety boundary.
- `tools/ui_simulator.py` provides deterministic 480x480 schema/pixel checks and must retain large-mesh coverage.
- Hardware Map automation remains network-suppressed. Only a physical Map entry authorizes the bounded current-view fetch; probe counters must remain unchanged.
- Each physical pan or zoom may create only one visible 3x3 generation at its selected zoom. Drag motion itself must remain network-silent, and returning from Home to an already completed exact view must reuse the retained frame without replaying SD reads.
- `ui_capture_d1l.py` reconstructs the firmware-maintained RGB565 shadow for exact pixel evidence, but final release also requires manual physical photos/touch review.
- Serial route diagnostics remain `routes`, `routes detail <seq>`, `routes trace <fingerprint>`, `routes probe <fingerprint>`, and `routes clear`.
- Serial diagnostics and test hooks are evidence surfaces, not substitutes for user-complete touchscreen workflows.
- Automated RF uses only `#test` or targeted DMs through an independent peer such as the COM11 Discord Python bot. It never uses default Public, and COM11 is never opened as D1L serial.
- Soak artifacts must retain their command manifest; a duration and health summary without the commands executed is not release evidence.

## Active Docs

Use [docs/README.md](README.md) as the documentation index. Keep [README.md](../README.md), [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md), [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md), [TEST_PLAN_D1L.md](TEST_PLAN_D1L.md), and the release-gate schema aligned whenever scope or evidence changes.
