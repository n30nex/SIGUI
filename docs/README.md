# Documentation Index

This folder contains only active release-facing documentation. Historical phase checkpoints and dated Codex handoffs were removed so the repo has one current source of truth.

## Start Here

- [Project README](../README.md): public overview, feature matrix, screenshots, hardware route.
- [Roadmap](ROADMAP.md): current release blockers and work queue.
- [Release checklist](RELEASE_CHECKLIST.md): evidence required before tagging.
- [Known limitations](KNOWN_LIMITATIONS.md): honest current gaps and non-goals.
- [Test plan](TEST_PLAN_D1L.md): host, hardware, SD, RF, UI, soak, and release-gate validation.
- [Fast release workflow](FAST_RELEASE_WORKFLOW_D1L.md): short PR loop, fast Actions path, targeted hardware proof.
- [Codex goal prompt](CODEX_GOAL_PROMPT_D1L.md): copy-paste prompt for the next iterative release cycle.

## User And Developer Guides

- [User guide](USER_GUIDE_D1L.md)
- [Developer guide](DEVELOPER_GUIDE_D1L.md)
- [Flash recovery](FLASH_RECOVERY_D1L.md)
- [Board bring-up](D1L_BOARD_BRINGUP.md)
- [Build decision](D1L_BUILD_DECISION.md)
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
