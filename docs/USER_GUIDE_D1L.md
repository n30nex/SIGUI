# MeshCore DeskOS D1L Core 1.0 User Guide

MeshCore DeskOS D1L Core 1.0 is a touch-first, non-forwarding MeshCore desk
client for the Seeed SenseCAP Indicator D1L. The immutable release profile is
`core_1_0`.

Release readiness is determined only by the exact GitHub Actions package and
its Core release audit. A source checkout, simulator image, dry run, or older
device image is not release evidence.

## Supported surface

Core 1.0 supports:

- Home with device, radio, message, storage, and health truth;
- Public messages on the fixed default Public channel;
- direct-message conversations with verified contacts;
- basic contact and heard-node inspection;
- Nodes and bounded node detail;
- read-only Packets and route/signal diagnostics;
- Canada/USA radio settings;
- local identity and adverts;
- retained settings and Core message state in internal NVS;
- diagnostics, health, crashlog, display, touch, and backlight;
- fixed UTC-offset display and truthful approximate/unavailable time;
- checksum-verified USB installation and recovery.

SD history is disabled in this Core candidate because the paired SD/RP2040
path was not available for exact-candidate qualification. The package omits
the RP2040 payload and internal NVS remains authoritative.

## Unavailable surface

The following are intentionally unavailable and should not appear as usable
destinations or controls:

- Map and map tiles;
- user-controlled Wi-Fi;
- BLE;
- channel creation, import, export, selection, or removal;
- repeater or room-server administration;
- Observer/MQTT;
- signed SD update or OTA;
- GPS/location (the D1L has no onboard GPS);
- mutable terminal or log UI;
- advanced QR/emoji tools;
- user-facing active TRACE/PATH tools;
- a notification system beyond unread counters.

An unavailable mutation returns `ESP_ERR_NOT_SUPPORTED` with
`release_profile="core_1_0"` before any side effect.

## Navigation

The Core dock contains exactly:

1. Home
2. Messages
3. Nodes
4. Packets
5. Settings

Messages contains Public and Direct messages. Opening, reading, searching,
scrolling, or refreshing a message view does not transmit. Radio transmission
requires an explicit Send action.

## First use

1. Power on the D1L and wait for Home.
2. Confirm the device identifies itself as a non-forwarding desk client.
3. Open Settings and review About/Version, Radio, Identity, Storage, Time, and
   Diagnostics.
4. Confirm Storage reports internal/NVS operation and SD history disabled.
5. Use the Canada/USA radio preset unless your authorized region requires a
   different supported configuration.
6. Let a compatible peer advert be received before attempting a direct
   message. Use the complete verified contact identity; ambiguous fingerprint
   prefixes are rejected.

## Messaging

Public is the one fixed default channel in Core 1.0. A user may manually read,
compose, and send Public messages. Release automation never transmits on the
default Public channel.

A Public packet's sender display name is unverified. The
`sender_name_unverified` boundary never alias-matches that name into a direct
message target. Direct-message compose requires the complete public key of the
same retained, verified chat contact. Heard-only, incomplete, mismatched, or
non-chat identities remain read-only.

Direct messages require a verified compatible peer. The conversation reports
queued, transmitted, acknowledged, retrying, or failed state truthfully. A
failed draft is not silently retransmitted by navigation or refresh.

## Storage and persistence

Core retained state uses internal NVS. It includes bounded settings, identity,
Public and direct-message state, read markers, required contacts/routes, and
crashlog state.

- DeskOS never formats an SD card.
- This Core package does not contain an RP2040 SD bridge image.
- SD media is not required for Core 1.0.
- Normal project flashing is non-erasing and should preserve retained state.
- The full 8 MB recovery image is destructive and requires typed
  confirmation.

## Install from the exact release package

Use only the package attached to the GitHub release for the exact tagged
commit. Verify every checksum before flashing.

```powershell
python .\scripts\verify_checksums.py <extracted-package-directory>
$env:D1L_PORT = "COM12"
.\flash_project.ps1 -Port $env:D1L_PORT
```

The release package contains its own checksum manifest, source commit, Actions
run identifier, release profile, SD mode, provenance, SBOM, supported-feature
summary, and explicit-port flash helpers.

Do not use COM8, COM11, or COM29. COM16 is reserved for separately authorized
SD/RP2040 maintenance and is not used by this Core package.

Read `FLASH_RECOVERY_D1L.md` before using recovery. Do not use an erase or full
image for a normal upgrade.

## Core diagnostic commands

The bounded USB console includes:

- `version`
- `health`
- `crashlog`
- `board status`
- `settings get`
- `mesh status`
- `radio get`
- `identity`
- `messages public`
- `messages dm`
- `messages unread`
- `nodes`
- `contacts`
- `routes`
- `packets`
- `signal`
- `storage status`
- controlled `reboot`

The device may expose additional read-only recovery or diagnostic probes. A
read-only unavailable-feature status must say `available=false` and must not
claim runtime support.

## Safety

- Firmware binaries are built only by GitHub Actions.
- Flash only an exact package whose checksums and commit binding pass.
- Use COM12 for the D1L.
- Never use COM8, COM11, or COM29.
- Never format SD.
- Never automate default Public RF.
- Do not treat predecessor, simulated, dry-run, or source-only output as
  exact-candidate evidence.

## Support and release evidence

See:

- `README.md` for the public Core capability summary;
- `docs/release/24H_STATUS.md` for the live fail-closed execution ledger;
- `docs/FLASH_RECOVERY_D1L.md` for install/recovery safety;
- `docs/release/SIGUI_CORE_1_0_PRODUCT_CONTRACT_2026-07-18.md` for the
  authoritative product boundary.

Full Feature development remains tracked separately and
`full_feature_release_ready` remains false.
