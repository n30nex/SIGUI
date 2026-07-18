# MeshCore DeskOS D1L Documentation

The active public product boundary is MeshCore DeskOS D1L Core 1.0 with
immutable profile `core_1_0`. SD history is disabled for this candidate and
internal NVS is authoritative.

## Core release documents

- [Public project overview](../README.md)
- [Core user guide](USER_GUIDE_D1L.md)
- [Flash and recovery](FLASH_RECOVERY_D1L.md)
- [Core product contract](release/SIGUI_CORE_1_0_PRODUCT_CONTRACT_2026-07-18.md)
- [24-hour execution ledger](release/24H_STATUS.md)
- [Core audit and roadmap](release/SIGUI_24H_AUDIT_AND_ROADMAP_2026-07-18.md)
- [Core execution backlog](release/SIGUI_24H_EXECUTION_BACKLOG_2026-07-18.yaml)
- [Fast release workflow](FAST_RELEASE_WORKFLOW_D1L.md)
- [Build decision](D1L_BUILD_DECISION.md)
- [Attributions](ATTRIBUTIONS.md)
- [Source audit and attribution](SOURCE_AUDIT_AND_ATTRIBUTION.md)

The exact GitHub Actions package and Core audit determine release readiness.
Historical completion ledgers, screenshots, simulator output, predecessor
hardware evidence, and full-feature plans do not qualify a Core candidate.

## Historical Full Feature development

[The Full Feature development guide](FULL_FEATURE_DEVELOPMENT_GUIDE_D1L_2026-07-18.md)
is preserved as planning/history. It describes Map, Wi-Fi, BLE, SD,
multi-channel, administration, update, location, and advanced surfaces that
are intentionally unreachable in Core 1.0 and is excluded from the Core
release package.

Full Feature work continues in the broader roadmap and
`full_feature_release_ready` remains false.

## Safety summary

- Firmware builds run only in GitHub Actions.
- COM12 is the D1L app/console/flash endpoint.
- COM16 is reserved for separately authorized SD/RP2040 work.
- COM8, COM11, and COM29 are forbidden.
- DeskOS and its validation tools never format SD.
- Release automation never transmits on the default Public channel.
