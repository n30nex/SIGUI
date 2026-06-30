# Phase 6 SD Bridge Setup Checkpoint

This checkpoint advances optional SD-card data storage without making boot or retained data depend on the card.

## Implemented

- Added an ESP32-side RP2040 SD status probe using the line protocol `DESKOS_SD_STATUS`.
- Extended `storage status` with RP2040 protocol, card, filesystem, DeskOS root, capacity/free, setup, and format action fields.
- Added non-destructive `storage setup`.
- Kept `storage setup confirm FORMAT-DESKOS-SD` non-destructive until the RP2040 bridge implements the matching format command.
- Added a Settings-tab Storage Setup sheet and simulator coverage.
- Kept all retained stores on onboard NVS; no SD-backed store migration is claimed in this slice.

## Validation Rules

- Do not run firmware builds on the Windows host. Firmware artifacts must come from GitHub Actions.
- Local verification for this slice is limited to host tests, simulator generation, dry-run smoke, diff checks, and GitHub Actions status.
- Do not test RF on Public channel for this slice. If hardware proof is needed, use the COM11 Meshcorebot DM path only.

## Remaining SD Work

- Implement the RP2040 firmware side of `DESKOS_SD_STATUS`.
- Add the RP2040 user-confirmed format/setup command.
- Move larger retained stores to SD when configured: Public/DM history, packet/route history, exports, and future map tiles.
- Keep settings, identity, and minimum boot-critical state on onboard storage.
