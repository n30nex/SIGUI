# Release Checklist

## Phase 1

- [ ] Host tests pass.
- [ ] Firmware builds with ESP-IDF v5.1.x.
- [ ] Flash backup captured with SHA256.
- [ ] D1L flashes using explicit port only.
- [ ] Boot banner includes firmware name/version/schema.
- [ ] `i2c` detects D1L expander/touch.
- [ ] `display test` shows stable bars.
- [ ] `touch test` reports coordinates.
- [ ] `backlight <0-100>` dims.
- [ ] `radiohw` reports SX1262 status or exact wiring failure.
- [ ] `radio get` reports US/CAN 910.525/BW62.5/SF7/CR5/20 dBm/TCXO NONE.
- [ ] NVS settings survive reboot on hardware.
- [ ] MeshCore local identity is generated and retained.
- [ ] Smoke JSON and monitor logs archived.

## Major Version Release

- [ ] Private GitHub repo exists under the user's account.
- [ ] CI artifacts produced.
- [ ] Firmware binaries and SHA256 checksums attached.
- [ ] Known limitations updated.
- [ ] Hardware validation notes include exact port, board, and date.
