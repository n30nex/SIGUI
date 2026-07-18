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

Current status summary: exact main `de0bb75bd91146f0dc9896540d12c71889d7766b` through PR #182 is strict-banked by Actions `29548300732`, with 1,263 host tests, 33 checksum-contract tests, 1,008 wire vectors, 931 oracle checks, and native plus Clang 18 semantic-packet fuzzing passing. Five downloaded artifact ZIPs / 46 entries across 341 files verify. PRs #177-#182 bank replay/hash authority, USB parsing, ACK/lifetime behavior, channel admission, and semantic-packet parsing/fuzzing. The truthful WP-05 matrix remains partial at 13 implemented / 3 partial / 0 missing requirements and 4 implemented / 1 partial / 3 missing fuzz targets. A non-erasing exact-main COM12 flash passed, but retained predecessor DM data needs compatibility or migration work before reboot persistence can pass. Capability reporting remains 80%, weighted release progress 74%, and final gates 0 of 11; WP-05/WP-06 completion, RF, physical UI/SD/update/soak, remaining runtime/durability work, and final exact-commit acceptance remain open.

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
