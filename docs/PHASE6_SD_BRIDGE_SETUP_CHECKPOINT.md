# Phase 6 SD Bridge Setup Checkpoint

This checkpoint advances optional SD-card data storage without making boot or retained data depend on the card.

## Implemented

- Added an ESP32-side RP2040 SD status probe using the line protocol `DESKOS_SD_STATUS`.
- Added the guarded ESP32 command path for `DESKOS_SD_FORMAT FORMAT-DESKOS-SD`; it is sent only after the bridge has reported a present card, `format_supported=true`, setup is required, and the user typed the exact confirmation phrase.
- Extended `storage status` with RP2040 protocol, card, filesystem, DeskOS root, capacity/free, setup, and format action fields.
- Added non-destructive `storage setup`.
- Kept `storage setup confirm FORMAT-DESKOS-SD` non-destructive on the current D1L bridge, which still does not answer the SD protocol or advertise format support.
- Added a Settings-tab Storage Setup sheet and simulator coverage.
- Added `tools/rp2040_sd_protocol.py` as the host reference simulator for the RP2040 line protocol.
- Kept all retained stores on onboard NVS; no SD-backed store migration is claimed in this slice.

## Validation Rules

- Do not run firmware builds on the Windows host. Firmware artifacts must come from GitHub Actions.
- Local verification for this slice is limited to host tests, simulator generation, dry-run smoke, diff checks, and GitHub Actions status.
- Do not test RF on Public channel for this slice. Current D1L hardware validation uses COM12 serial only; do not use reserved bot/OpenClaw serial ports during this SD bridge slice.

## Remaining SD Work

- Add or import a buildable D1L RP2040 firmware target. The current repo only has Seeed example sketches/helpers and no production RP2040 SD bridge target.
- Implement the RP2040 firmware side of `DESKOS_SD_STATUS` and `DESKOS_SD_FORMAT FORMAT-DESKOS-SD`; see `docs/SD_BRIDGE_PROTOCOL_D1L.md`.
- Move larger retained stores to SD when configured: Public/DM history, packet/route history, exports, and future map tiles.
- Keep settings, identity, and minimum boot-critical state on onboard storage.
