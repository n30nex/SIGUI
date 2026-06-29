# Release Checklist

## Phase 1

- [x] Host tests pass.
- [x] Firmware builds with ESP-IDF v5.1.x.
- [ ] Flash backup captured with SHA256. Skipped for current bring-up per operator instruction.
- [x] D1L flashes using explicit port only.
- [x] Boot banner includes firmware name/version/schema.
- [x] `i2c` detects D1L expander/touch.
- [ ] `display test` shows stable bars. Command passed on hardware; manual visual confirmation is still pending.
- [x] `touch test` reports coordinates.
- [x] `backlight <0-100>` accepts dim/restore commands.
- [x] `radiohw` reports SX1262 status or exact wiring failure.
- [x] `radio get` reports US/CAN 910.525/BW62.5/SF7/CR5/20 dBm/TCXO NONE.
- [x] NVS settings survive reboot on hardware.
- [x] MeshCore local identity is generated and retained.
- [x] Controlled MeshCore advert TX/RX validated with a local bot.
- [x] Controlled MeshCore Public `test` TX/RX validated with a local bot.
- [x] Smoke JSON and monitor logs archived.

## Major Version Release

- [x] Private GitHub repo exists under the user's account.
- [x] CI artifacts produced.
- [x] Firmware binaries and SHA256 checksums attached.
- [x] Known limitations updated.
- [x] Hardware validation notes include exact port, board, and date.
