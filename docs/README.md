# Documentation Index

This folder contains only active release-facing documentation. Historical phase checkpoints and dated Codex handoffs were removed so the repo has one current source of truth.

## Start Here

- [Project README](../README.md): public overview, feature matrix, screenshots, hardware route.
- [Completion status](COMPLETION_STATUS.md): concise generated view of live work-package, blocker, and release-gate state.
- [Completion ledger](COMPLETION_LEDGER.yaml): machine-readable source used to generate and validate completion status.
- [Roadmap](ROADMAP.md): current release blockers and work queue.
- [KRAB'S THOUGHTS release audit](KRABSTHOUGHTS.MD): detailed 1.0 product definition, P0.1-P0.20 blocker rationale, and acceptance criteria incorporated into the roadmap.
- [Release checklist](RELEASE_CHECKLIST.md): evidence required before tagging.
- [Known limitations](KNOWN_LIMITATIONS.md): honest current gaps and non-goals.
- [Test plan](TEST_PLAN_D1L.md): host, hardware, SD, RF, UI, soak, and release-gate validation.
- [Fast release workflow](FAST_RELEASE_WORKFLOW_D1L.md): short PR loop, fast Actions path, targeted hardware proof.
- [Build decision](D1L_BUILD_DECISION.md): selected ESP-IDF target and the staged issue #63 qualification requirements.
- [Codex goal prompt](CODEX_GOAL_PROMPT_D1L.md): copy-paste prompt for the next iterative release cycle.

Current status summary: the top-level [README](../README.md) and [Roadmap](ROADMAP.md) are the release-readiness source of truth. Standalone Map/Wi-Fi `de79c9f` has green Actions and exact-COM12 boot-loop/Wi-Fi/memory/SD stability proof, with physical Map acceptance still open. Standalone ESP-IDF 5.5.4 `39a043c` has the exact Actions-generated dependency lock plus green host, firmware, package, checksum, effective-config, and release checks, with no hardware qualification. The combined candidate needs fresh Actions and exact-COM12 proof before either evidence set can qualify it. Public release also remains blocked by the KRAB-derived MeshCore conformance, reliable DM/ACK/retry/PATH/trace, contact/channel, runtime durability, remaining RF/SD/physical UI, soak, package, and final exact-commit gate requirements.

## User And Developer Guides

- [User guide](USER_GUIDE_D1L.md)
- [Developer guide](DEVELOPER_GUIDE_D1L.md)
- [Flash recovery](FLASH_RECOVERY_D1L.md)
- [Board bring-up](D1L_BOARD_BRINGUP.md)
- [480x480 UI spec](UI_SPEC_480x480_DARK.md)

## SD Card Docs

- [Guided SD install](D1L_SD_CARD_GUIDED_INSTALL.md)
- [RP2040 SD bridge flash and proof](RP2040_SD_BRIDGE_FLASH_D1L.md)
- [RP2040 SD bridge protocol](SD_BRIDGE_PROTOCOL_D1L.md)

Policy summary: users prepare FAT32 SD cards on a computer; DeskOS has no device-side SD formatting path. Current `1a29876` / Actions `28714355561` COM12/COM16 evidence proves the installed FAT32 card path, official Seeed smoke, raw diagnostics, correct/missing/existing-data boot scenarios, RP2040-unavailable fallback, retained-history survival, reboot/remount, map-tile canary, export canary, diagnostic export, and sampled data export. Actual no-card and unformatted/non-FAT32 behavior, FAT32 multi-card matrix, no-format guidance evidence tied to unusable media, and power/electrical proof still gate public release.

## Compatibility And Attribution

- [MeshCore 3-byte companion compatibility](COMPANION_3BYTE_COMPATIBILITY.md)
- [Attributions](ATTRIBUTIONS.md)
- [Source audit and attribution](SOURCE_AUDIT_AND_ATTRIBUTION.md)

## Screenshots

Current host simulator screenshots live in [screenshots](screenshots). They are not a replacement for physical device photos.
