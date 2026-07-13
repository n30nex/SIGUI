# KRAB'S THOUGHTS: D1L Firmware 1.0 Release Audit

Audit snapshot: 2026-07-10

Repository: `n30nex/SIGUI`

Audited `main`: `5b5dfaa0592347497df1a2f77f572c6d49933c6a`

Pinned MeshCore: `e8d3c53ba1ea863937081cd0caad759b832f3028` (v1.16.0 line)
Host result at the audited commit: **374 tests passed**

## Executive verdict

SIGUI is a credible and unusually well-instrumented firmware project, but it is **not yet ready to be called a complete D1L firmware 1.0**.

The strongest parts are the fail-closed evidence system, deterministic UI captures, retained stores, SD bridge work, settings model, packet diagnostics, and the amount of real-hardware proof already accumulated. The biggest remaining risk is not ordinary UI polish. It is that the device currently presents several MeshCore workflows that are only partially implemented underneath.

The release should remain blocked until:

1. MeshCore DM, ACK, retry, PATH, direct-route, trace, contact and channel behavior is complete and wire-compatible with the pinned upstream implementation.
2. Every enabled UI action has an end-to-end implementation and an honest success/failure state.
3. ESP-IDF 5.5 migration, Wi-Fi/map integration, SD behavior and storage persistence are validated together on the release candidate.
4. The final firmware passes protocol conformance, two-radio acceptance, physical UI review, storage fault testing and a commit-matched soak.

The current roadmap is effective for a narrow first public binary. A real 1.0 needs a wider functional definition.

## What “D1L firmware 1.0” should mean

For this audit, 1.0 means a standalone, touch-first MeshCore client that can be trusted by a normal user without a serial console.

### Required 1.0 behavior

- Boot reliably from a clean flash and from an upgraded prior installation.
- Recover from invalid settings, unavailable radio, unavailable RP2040, bad SD media and failed Wi-Fi without boot-looping.
- Advertise a persistent identity and discover compatible MeshCore nodes.
- Send and receive Public messages using the configured channel.
- Send and receive direct messages with ACK, retry, timeout and honest delivery state.
- Discover, retain, edit, import, export and remove contacts.
- Discover or learn routes, select direct routes when valid and fall back safely when they are not.
- Perform a real MeshCore trace/path operation, not a text-message approximation.
- Support the channel workflows exposed by the UI. For full app-like functionality, this means more than one hard-coded Public channel.
- Display network, message, contact and diagnostic state accurately after reboot.
- Apply radio settings safely, with clear live-apply or reboot-required semantics.
- Preserve retained state without excessive flash writes or corruption.
- Keep all LVGL mutation on the UI task and survive large/live mesh updates without stale callbacks or split frames.
- Ship a reproducible release package with checksums, upgrade instructions, recovery instructions and commit-matched validation evidence.

### Optional features must be honest

BLE companion transport, OTA, GPS, online map tiles and server administration do not all have to be in 1.0. However, each feature must be one of:

1. complete and release-tested;
2. visibly marked experimental with safe failure behavior; or
3. absent from the release UI.

A visible action that opens a working-looking page but cannot complete its workflow is a release defect.

## Current foundation that should be preserved

- The release gate fails closed and requires evidence tied to the release commit.
- Public text TX/RX and signed advert foundations exist.
- Identity, settings, messages, DMs, nodes, contacts, routes and packet diagnostics have bounded stores.
- The UI simulator, pixel-capture tooling and no-Public/no-format evidence flags are excellent regression tools.
- Core FAT32 file, retained-history, reboot/remount, export and map-canary paths have substantial physical proof.
- Contact export and QR presentation exist.
- Radio settings, Wi-Fi setup, map policy, storage status and diagnostics are represented in the UI.
- CI and host tests are currently green.

These are strong foundations. They do not replace end-to-end protocol and failure-mode proof.

## P0 blocker ledger

### P0.1 — Move the release build to a supported ESP-IDF

Current `main` remains on the old ESP-IDF line. [PR #64](https://github.com/n30nex/SIGUI/pull/64) migrates the project to ESP-IDF 5.5.4 and is clean with green checks, but it is still a draft.

Release requirements:

- Merge the supported-toolchain migration.
- Build all release artifacts from a clean checkout under the documented toolchain.
- Rebase the map/Wi-Fi work after the toolchain change.
- Repeat internal-heap, Wi-Fi reconnect, RF timing and soak measurements. Earlier measurements cannot automatically qualify a different compiler, SDK, allocator and network stack.
- Label [issue #63](https://github.com/n30nex/SIGUI/issues/63) with `p0` and `release-blocker`; its title says P0 but its label set currently does not, so label-driven reports can omit it.

Acceptance:

- Supported ESP-IDF release build passes CI and boots on physical D1L hardware.
- No new warnings are waived without written justification.
- Wi-Fi disabled, Wi-Fi enabled, radio RX/TX and SD bridge paths pass on the same build.

### P0.2 — Stop treating a local protocol subset as upstream MeshCore

[`main/CMakeLists.txt`](../main/CMakeLists.txt) compiles MeshCore's Ed25519 files, but not the upstream `Mesh`, `Dispatcher`, `BaseChatMesh` or companion command implementation. Most protocol behavior is independently reimplemented in [`main/mesh/meshcore_service.c`](../main/mesh/meshcore_service.c).

That is not automatically wrong, but it creates a conformance obligation that is not currently met. A local codec can pass source-contract tests while differing from real MeshCore packet construction, routing, retry or duplicate behavior.

Preferred direction:

- Keep the D1L-specific SX1262 and ESP-IDF hardware layer.
- Add a narrow C++ adapter around the pinned upstream packet/routing/chat machinery where practical.
- If upstream integration is impractical, separate the local implementation into explicit codec, transport, routing/session and application layers, then prove each against upstream-generated vectors.
- Do not copy more protocol behavior into the monolithic service without conformance tests.

Acceptance:

- The supported MeshCore feature surface is documented.
- Every supported packet type has bidirectional golden-vector tests against pinned upstream MeshCore.
- Malformed packets cannot overrun buffers, corrupt retained state or wedge RX.

### P0.3 — Complete inbound DM ACK behavior

[`parse_rx_dm_packet()`](../main/mesh/meshcore_service.c) decrypts and stores an inbound DM and computes its ACK hash, but it does not construct and transmit the corresponding MeshCore ACK.

This is a direct functional blocker: another client may show the D1L's received message as undelivered even though it appeared on-screen.

Required behavior:

- ACK valid inbound DMs according to upstream timing and routing rules.
- Support direct and flood ACK paths as required.
- Handle ACK+PATH multipart responses where applicable.
- Do not ACK invalid, unauthenticated, duplicate or replayed payloads incorrectly.
- Record ACK transmission and failure separately from message receipt.

Acceptance:

- Official companion app sends a DM to D1L and receives the correct ACK.
- D1L sends a DM to official companion app and transitions through queued, transmitting, sent and acknowledged states.
- Duplicate inbound delivery does not create duplicate user-visible messages or ACK storms.

### P0.4 — Implement a real outbound DM delivery state machine

The current outbound path records `attempt = 0`, has no user-message retry scheduler and stores the DM as sent immediately after the raw-send command succeeds. That command means transmission was accepted by the service, not necessarily that `TxDone` occurred or an ACK arrived.

Required states:

- queued;
- waiting for radio;
- transmitting;
- sent over RF;
- acknowledged;
- retrying with attempt count;
- timed out/failed;
- cancelled if cancellation is supported.

Required behavior:

- Track expected ACK hashes with expiry.
- Retry according to upstream-compatible rules and airtime limits.
- Update retained history when `TxDone`, `TxTimeout` and ACK events occur.
- Avoid retrying forever after a route becomes invalid.
- Surface failure and retry state in the DM thread.

Acceptance:

- Successful ACK, lost ACK, no recipient, radio busy, TX timeout and reboot-during-pending tests all produce truthful final state.

### P0.5 — Fix the heard-node DM dead end

The Nodes UI can create a temporary `d1l_contact_entry_t` and open the DM composer. The send path then calls `d1l_meshcore_service_send_dm()`, which re-queries the retained contact store by fingerprint and returns `ESP_ERR_NOT_FOUND` if the temporary contact was never promoted.

Required fix:

- Promote the heard node into the retained contact store before opening or sending; or
- pass a validated public key/contact object into the service and make the persistence decision explicit.

Acceptance:

- Tapping Message on any DM-capable heard node sends successfully without a hidden prerequisite.
- If promotion cannot be completed, the composer must not open as though sending is available.

### P0.6 — Finish contact lifecycle parity

Contact storage and several contact actions exist, but 1.0 needs one coherent lifecycle:

- promote from advert/heard node;
- add/import from a MeshCore QR or URI;
- retain full public key and type;
- rename, favorite and mute;
- inspect route and last-heard data;
- export/share;
- forget with confirmation;
- handle key/name updates safely;
- prevent ambiguous fingerprint-prefix collisions.

Acceptance:

- Contact import/export round-trips with an official MeshCore client.
- A contact created through every supported entry path can receive a DM.
- Reboot preserves all intended fields and no stale contact pointer survives deletion.

### P0.7 — Add real channel support or explicitly narrow 1.0

The current service uses one hard-coded Public secret/hash. The companion experience supports channel configuration and multiple channel slots.

For a full app-like 1.0, implement:

- channel list and active channel selection;
- channel name and secret/key management;
- Public and private channel RX/TX;
- import/export or QR setup where supported;
- unread/history separation by channel;
- channel identity in packet diagnostics;
- safe persistence and migration of channel settings.

If multi-channel support is deferred, the release must clearly state “single Public channel client,” remove channel-management affordances and avoid implying broader compatibility.

Acceptance:

- At least two independently keyed channels can be configured, selected, retained and used without cross-channel history leakage.
- Official MeshCore app and D1L exchange messages on each configured channel.

### P0.8 — Replace the fake trace with MeshCore trace/path discovery

`d1l_meshcore_service_request_trace_probe()` currently sends a normal DM containing a token such as `trace_12345678`. This is not a MeshCore trace packet and must not be presented as protocol trace.

Required behavior:

- Build the correct trace/path request packet.
- Authenticate it as upstream expects.
- Parse response hops, hashes and SNR values.
- Correlate response to request and enforce timeout/cooldown.
- Display partial, timed-out and successful results accurately.
- Keep ordinary user DM history free of diagnostic probes.

Acceptance:

- D1L trace interoperates with a compatible official client/repeater path.
- A route change is reflected in a subsequent trace.

### P0.9 — Complete route learning and direct-route fallback

Current code can retain PATH information and select a stored outbound path, but a complete implementation needs:

- path discovery request/response;
- reciprocal path handling after inbound DM;
- ACK+PATH multipart behavior;
- route age and invalidation;
- direct-send failure fallback to flood where safe;
- route scoring or deterministic selection when more than one observation exists;
- clear separation between observed route metadata and a proven usable outbound route.

Acceptance:

- First contact succeeds by flood, learns a path and subsequently uses direct routing.
- Invalid stored path does not permanently break messaging.
- Route detail UI reports source, age, direction and proof state truthfully.

### P0.10 — Add duplicate, replay and packet-lifetime controls

The local receive path parses packets into stores but does not presently provide the complete upstream seen-packet, duplicate-suppression and replay behavior.

Required behavior:

- Deduplicate Public, DM, ACK, advert, PATH and trace traffic using protocol-appropriate identifiers.
- Reject stale/replayed adverts according to timestamp/signature rules.
- Ignore self adverts and self messages where appropriate.
- Bound all seen/expected-ACK tables.
- Define whether the D1L forwards packets. If it is intentionally a non-forwarding client, make that explicit and do not expose repeater semantics.

Acceptance:

- Replaying a captured packet does not create a second visible message or corrupt route/contact state.
- A busy mesh cannot grow an unbounded table or generate retransmission storms.

### P0.11 — Build a real protocol-conformance suite

The 374 passing host tests are valuable, but much of the suite checks source contracts, bounded structures and UI artifacts. That is insufficient for a wire protocol implementation.

Add tests for:

- upstream-generated advert decode and SIGUI-generated advert decode upstream;
- Public message vectors for every supported channel configuration;
- DM encryption/decryption in both directions;
- ACK and multipart ACK+PATH;
- direct and flood route headers;
- path encoding for every supported hash size/hop count;
- trace request/response;
- invalid signature, MAC, length, path and payload cases;
- duplicate/replay behavior;
- fuzzing of raw RX packet parsing;
- retained-state transitions for TxDone, TxTimeout, ACK and retry.

Acceptance:

- CI builds and runs the conformance harness against the exact pinned MeshCore submodule.
- Updating the submodule requires an intentional conformance review.

### P0.12 — Fix service concurrency and ownership

The service has a status mutex, but many radio callbacks and command paths access `s_status`, pending TX state and stores outside one obvious ownership boundary.

Required direction:

- Radio callbacks should enqueue compact immutable events.
- One MeshCore service task should own protocol/session state and retained-store transitions.
- Status snapshots should be copied under the same lock or through message passing.
- UI code should consume model snapshots, never mutable radio-owned structures.
- Define backpressure when queues are full; do not silently lose an ACK or TxDone event.

Acceptance:

- ThreadSanitizer is not available on ESP32, so add host state-machine tests plus hardware stress with simultaneous RX, UI refresh, Wi-Fi and storage flush.
- Queue saturation produces counters and recoverable errors, not corrupted state.

### P0.13 — Reduce flash wear and synchronous persistence

Node advert updates and several retained stores can commit to NVS on frequent network events. On a busy mesh, this risks unnecessary flash wear and latency in radio/UI-sensitive paths.

Required behavior:

- Keep hot observations in RAM.
- Mark stores dirty and coalesce persistence on a bounded timer or material change.
- Avoid rewriting unchanged advert/contact data.
- Keep the newest critical fallback state in NVS and use SD for deeper history when available.
- Flush safely before controlled reboot without blocking radio callbacks.
- Document expected write frequency under a busy-mesh test profile.

Acceptance:

- A synthetic high-advert/high-message run demonstrates bounded commits per hour.
- Power loss during a coalesced flush leaves either the old or new valid snapshot, never a corrupt store.

### P0.14 — Finish settings schema, migration and reset safety

[Issue #22](https://github.com/n30nex/SIGUI/issues/22) is currently P1, but a public 1.0 needs stable settings semantics.

Required behavior:

- Version every persisted settings/store schema.
- Migrate from every public pre-1.0 beta intended to be upgradeable.
- Reject or reset invalid fields individually where possible.
- Provide a documented factory-reset and recovery path.
- Never let bad Wi-Fi, radio, channel or map settings create a permanent boot loop.
- Clearly indicate which radio changes apply live and which require restart.

Acceptance:

- Clean flash, same-version reboot, upgrade from selected old images, corrupted settings and downgrade-rejection scenarios are tested.

### P0.15 — Complete runtime-safe UI ownership

[`main/ui/ui_phase1.c`](../main/ui/ui_phase1.c) remains approximately 8,500 lines. The existing [#6](https://github.com/n30nex/SIGUI/issues/6) and [#16](https://github.com/n30nex/SIGUI/issues/16) blockers should focus on runtime safety rather than achieving a cosmetically perfect module tree.

Required before release:

- all LVGL mutation occurs on the UI task;
- one modal manager owns active modal/sheet lifetime;
- callbacks never retain pointers to transient stack, query or cleaned LVGL objects;
- screen roots have explicit creation/destruction ownership;
- keyboard submit/cancel belongs to the active sheet;
- model updates enter through a bounded UI command/event queue;
- live refresh is coalesced and cannot rebuild a screen during its callback;
- the Messages, Network/routes, Map, Packets, Contacts and Settings domains have testable ownership boundaries.

Can move to post-1.0:

- further splitting that changes no ownership or safety boundary;
- aesthetic deduplication with no behavior impact.

Acceptance:

- Large-mesh, rapid-tab, modal-open/close and live-RX stress tests pass repeatedly on hardware.
- No stale callbacks, split frames, watchdog resets or monotonic heap loss.

### P0.16 — Decide and finish the Wi-Fi/map release surface

[PR #62](https://github.com/n30nex/SIGUI/pull/62) contains the built-in bounded current-view map work and the important internal-memory correction for Wi-Fi, but remains draft. Current `main` reports tile rendering unsupported and Wi-Fi experimental.

For 1.0 choose one:

**Ship maps:**

- merge/rebase the work after ESP-IDF 5.5;
- prove physical map entry, bounded tile request, cancellation and cache revisit;
- render cached tiles correctly;
- show attribution;
- handle no Wi-Fi/no SD/tile-service failure without blocking UI or radio;
- prove Wi-Fi off means the network stack and web-related services are actually inactive.

**Defer maps:**

- remove or visibly disable the Map download/render workflow;
- retain only truthful manual location/policy information if useful;
- do not call the feature supported in 1.0.

Acceptance if shipped:

- Wi-Fi off, connect, failed credentials, reconnect after reboot, AP disappearance, download cancellation, cache hit and low-memory paths pass on the final SDK build.

### P0.17 — Finish SD/RP2040 release behavior

Core SD functionality is strong, but [issues #4](https://github.com/n30nex/SIGUI/issues/4) and [#11](https://github.com/n30nex/SIGUI/issues/11) remain open. [PR #36](https://github.com/n30nex/SIGUI/pull/36) is currently dirty and should be rebased or replaced.

Required proof:

- actual no-card boot and runtime removal behavior;
- actual unformatted/non-FAT32 media behavior;
- no device-side formatting and clear computer-preparation guidance;
- FAT32 cards at representative 8GB, 16GB and 32GB sizes;
- RP2040 unavailable, bridge timeout and malformed bridge response;
- retained-store fallback and later recovery;
- power/electrical evidence;
- atomic write/power-loss behavior;
- guided install that a normal Windows/Linux/macOS user can follow.

Acceptance:

- No card or bad card can never prevent the core MeshCore client from booting.
- Firmware never formats or destructively modifies unsupported media.
- The release package contains the required RP2040 UF2, checksums and explicit flashing order if the bridge update is mandatory.

### P0.18 — Make network-role pages truthful

Rooms and Repeaters are currently useful read-only inspector views inferred from observed traffic. They are not equivalent to all companion-app login/status/control workflows.

For 1.0:

- label inferred candidates as inferred/observed;
- distinguish a heard repeater from a logged-in/manageable server;
- implement request/login/status/control flows before exposing management actions;
- show unsupported operations as absent, not mysteriously locked;
- document that the D1L is a client unless forwarding/repeater behavior is explicitly implemented and tested.

Acceptance:

- Every role label and action corresponds to protocol evidence the firmware actually possesses.

### P0.19 — Provide truthful time and ordering semantics

Current MeshCore TX timestamps are monotonic and persisted/fallback-protected, but a real wall-clock source is not yet wired.

Required behavior:

- Keep protocol timestamps monotonic and duplicate-safe.
- If Wi-Fi ships, obtain and retain sane network time.
- If no wall clock is available, display relative/unknown time rather than fabricated clock time.
- Define ordering across reboot and clock correction.

Acceptance:

- Messages remain correctly ordered across reboot, NTP acquisition and backward/forward wall-clock corrections.

### P0.20 — Complete final physical and release evidence

The existing evidence framework should remain the final authority, but it must run against one frozen release commit.

Required final proof:

- exact Actions-built firmware flashed to physical D1L;
- current screenshots and manual review of all enabled pages/actions;
- touch accuracy, scrolling, keyboard, modal return paths, brightness and visual corruption review;
- full two-radio/app RF acceptance;
- SD matrix and recovery tests;
- minimum existing 12-hour idle/listening soak; 24 hours is preferable for 1.0;
- an active-traffic soak including DM ACK/retry, Public RX, adverts, UI use, Wi-Fi and storage flush;
- heap, PSRAM, LVGL, stack, queue, crashlog, reset reason and persistence checks;
- release-gate output tied to the exact tag commit.

Acceptance:

- [Issue #8](https://github.com/n30nex/SIGUI/issues/8) is closed with commit-matched artifacts.
- No release blocker is waived solely because an older commit passed a related test.

## Functional parity matrix

| Area | Current assessment | 1.0 disposition |
|---|---|---|
| Public messaging | Functional foundation | Complete channel-aware interoperability proof |
| Direct messaging | Partial | P0: ACK, retry, state, contacts and route fallback |
| Contacts | Partial but substantial | P0: complete lifecycle and import/export proof |
| Channels | Single hard-coded Public channel | P0 for full app-like 1.0, otherwise explicitly narrow product scope |
| PATH/direct routes | Parsing/storage exists | P0: discovery, reciprocal path, expiry and fallback |
| Trace | Text DM probe | P0: implement real protocol trace or remove the claim |
| Nodes | Useful heard-node view | P0: fix Message action and evidence semantics |
| Rooms/repeaters | Inferred read-only views | P0: truthful labels; management only if implemented |
| Packet diagnostics | Strong foundation | P0: deeper decode/conformance for supported packet types |
| Radio settings | Implemented foundation | P0: final-SDK hardware apply/reboot proof |
| Wi-Fi | Experimental; fix in draft PR | Ship and prove, or hide/defer |
| Map tiles | Partial/draft | Ship complete current-view flow, or hide/defer |
| SD retained storage | Strong core, incomplete matrix | P0: physical failure/card/power matrix |
| BLE companion | Unsupported | Defer and hide unless completed |
| OTA | Not complete | May defer; provide excellent wired upgrade/recovery docs |
| GPS | Not complete | May defer; do not imply live location support |
| Time | Monotonic protocol time, no wall clock | P0: truthful display/order; add NTP if Wi-Fi ships |
| UI architecture | Significant progress, large remaining monolith | P0 only for ownership/race/lifetime safety |
| Release evidence | Excellent framework | P0: final frozen-commit execution |

## Recommended roadmap restructuring

The current RF issue is too broad and the UI issues mix safety with code tidiness. Split the remaining 1.0 work into independently closable deliverables:

1. **MeshCore conformance foundation** — upstream adapter decision, golden vectors and RX fuzzing.
2. **Reliable DM session** — inbound ACK, expected ACK table, retries, delivery states and duplicate handling.
3. **Contact and channel parity** — heard-node promotion, import/export and multi-channel storage/UI.
4. **Routing diagnostics** — path discovery, reciprocal path, direct fallback and real trace.
5. **Runtime durability** — service-task ownership, UI queue, write coalescing and schema migration.
6. **Supported build integration** — ESP-IDF 5.5, then rebased Wi-Fi/map work.
7. **Hardware acceptance** — RF matrix, SD matrix, UI review and soak.
8. **Release packaging** — clean flash, upgrade, recovery, checksums, changelog and final gate.

Each issue should name:

- exact code scope;
- exact host tests;
- exact physical proof;
- destructive/RF constraints;
- artifact schema;
- the one condition that closes it.

## Recommended implementation order

### Stage 1 — Freeze the product contract

- Decide whether 1.0 is single-channel or multi-channel.
- Decide whether Map ships enabled.
- Decide whether Rooms/Repeaters remain read-only inspectors.
- Publish the supported/not-supported feature matrix.

### Stage 2 — Establish the final platform

- Merge PR #64.
- Rebase PR #62 if Map/Wi-Fi ships.
- Rebase or replace PR #36.
- Build and flash one integration image before collecting more feature evidence.

### Stage 3 — Repair MeshCore semantics

- Add upstream conformance harness.
- Implement ACK/session state and heard-node contact promotion.
- Implement channels, route discovery/fallback and actual trace according to the frozen product contract.
- Add duplicate/replay controls.

### Stage 4 — Harden ownership and persistence

- Move protocol mutation into one service task.
- Finish bounded UI-task command ownership.
- Coalesce NVS/SD writes.
- Add schema migrations and recovery tests.

### Stage 5 — Run end-to-end acceptance

- Official app to D1L and D1L to official app.
- Two D1Ls if two units are available.
- Public, DM, ACK, retry, channels, path, direct fallback and trace.
- Wi-Fi/map and SD fault matrix on the same release candidate.

### Stage 6 — Freeze and qualify

- Freeze the commit.
- Build only from Actions.
- Run physical screenshots/manual review.
- Run idle/listening and active-traffic soaks.
- Run the final release gate.
- Tag only if every P0 is green on that exact commit.

## Final release checklist

### Build and package

- [ ] ESP-IDF 5.5.x supported build merged.
- [ ] Clean clone plus submodules builds without undocumented local files.
- [ ] CI runs host, firmware and required RP2040 jobs.
- [ ] Release package includes binaries/UF2, checksums, licenses and flashing instructions.
- [ ] Recovery and factory-reset procedures are documented and physically tested.

### MeshCore interoperability

- [ ] Signed advert TX/RX interoperates with pinned upstream.
- [ ] Public RX/TX passes on every supported channel type.
- [ ] DM RX triggers correct ACK.
- [ ] DM TX exposes correct TxDone, ACK, retry and failure state.
- [ ] Heard-node Message action works without hidden contact setup.
- [ ] Contact import/export round-trips with official app.
- [ ] PATH discovery and direct-route fallback pass.
- [ ] Trace is a real MeshCore trace and returns parsed results.
- [ ] Duplicate/replay tests pass.
- [ ] Malformed packet/fuzz suite passes.
- [ ] Forwarding/non-forwarding role is explicit and tested.

### UI and settings

- [ ] Every visible action completes or gives an actionable error.
- [ ] No unsupported feature appears complete.
- [ ] All LVGL mutation is UI-task-owned.
- [ ] Modal/keyboard/root callback lifetime tests pass.
- [ ] Settings schema migration and invalid-setting recovery pass.
- [ ] Radio live-apply/reboot semantics are accurate.
- [ ] Large/live mesh UI stress passes without heap loss or corruption.

### Storage and connectivity

- [ ] No-card and bad-card boots retain core functionality.
- [ ] 8/16/32GB FAT32 matrix passes.
- [ ] RP2040 unavailable/timeout/recovery passes.
- [ ] Power-loss/atomic-write behavior passes.
- [ ] NVS write rate is bounded under busy-mesh traffic.
- [ ] Wi-Fi disabled path leaves network features inactive.
- [ ] Wi-Fi connect/fail/reconnect/low-memory paths pass if Wi-Fi ships.
- [ ] Tile request/cancel/cache/render paths pass if Map ships.

### Physical release qualification

- [ ] Official release artifact is flashed and commit identity verified.
- [ ] Physical screenshots and manual touch review are current.
- [ ] Full RF acceptance artifact is green.
- [ ] SD/electrical artifact set is green.
- [ ] Minimum 12-hour listening soak passes; 24-hour preferred.
- [ ] Active traffic/storage/connectivity soak passes.
- [ ] Final release-gate report says `ready_for_public_release=true` for the tag commit.

## Go/no-go rule

The firmware is ready to tag as 1.0 only when a normal user can perform every enabled workflow from the touchscreen, the official MeshCore ecosystem observes correct protocol behavior, failure modes recover without serial intervention, and every claim is backed by the exact release artifact.

The shortest path is not more screenshot polish. It is:

**supported SDK → protocol conformance → reliable DM/ACK/PATH/trace → truthful channel/contact UI → ownership/persistence hardening → one frozen hardware qualification run.**
