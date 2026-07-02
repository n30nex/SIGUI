# DeskOS D1L RP2040 SD Bridge

This Arduino sketch is the RP2040 side of the optional D1L SD-card data bridge.
It speaks the newline-delimited protocol documented in
`docs/SD_BRIDGE_PROTOCOL_D1L.md` over the internal ESP32/RP2040 UART.

## Pin Contract

- RP2040 `Serial1` RX: GPIO17, connected to ESP32 TX GPIO19.
- RP2040 `Serial1` TX: GPIO16, connected to ESP32 RX GPIO20.
- SD SPI: `SPI1`.
- SD CS: GPIO13.
- SD SCK: GPIO10.
- SD MOSI/TX: GPIO11.
- SD MISO/RX: GPIO12.
- SD/sensor rail power enable: GPIO18, driven high before SD init.
- SD CS is driven high during bus setup. Before each explicit SdFat mount or
  raw probe, the bridge force-cycles the selected rail level, waits for it to
  settle, and sends idle clocks so warm-reset cards can re-enter SPI init. The
  raw probe requires `CMD0` to answer idle (`0x01`) before it reports a card as
  present, so stale ready-state or floating-bus responses do not mask firmware
  mount failures.
- UART baud: 921600, matching Seeed's ESP32/RP2040 internal UART example.

The pin values are based on Seeed's SenseCAP Indicator RP2040 Arduino examples.
This bridge code is original project code and intentionally keeps the protocol
plain ASCII rather than Seeed's sensor `PacketSerial` framing.

## Build

Firmware builds are run in GitHub Actions. The workflow installs Arduino CLI,
adds the `earlephilhower/arduino-pico` board package URL, installs
`rp2040:rp2040`, and compiles the sketch with FQBN
`rp2040:rp2040:seeed_indicator_rp2040`. The RP2040 job passes
`--build-property compiler.cpp.extra_flags="-DUSE_SD_CRC=1 -DUSE_SPI_ARRAY_TRANSFER=0"`
so SdFat sends valid SPI command CRC bytes during card initialization while
using byte-wise SPI transfers on the RP2040 SPI1 SD bus. The bridge sketch has
compile-time guards for these settings because the current validation card
reached the byte-wise raw SPI init path while the default RP2040 SdFat array
transfer path failed in the filesystem mount path.

The bridge emits checksummed artifacts under `rp2040-sd-bridge-firmware`.
Do not use the Windows host for firmware compilation.

## Hardware Validation

Verify the GitHub Actions artifact checksum before any RP2040 flash attempt:

```powershell
python .\scripts\verify_checksums.py artifacts\github\<run-id>\rp2040-sd-bridge-firmware
python .\scripts\flash_rp2040_sd_bridge_uf2.py --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware --list-volumes
```

The current ESP32 release flashing scripts are not RP2040 UF2 flashing tools.
Put the D1L RP2040, not the ESP32-S3, into UF2/BOOTSEL mass-storage mode before
copying `deskos_sd_bridge.ino.uf2`. Use
`scripts/flash_rp2040_sd_bridge_uf2.py --volume <RP2040_UF2_DRIVE>:` first as a
dry run, then add `--copy` only after it verifies the artifact checksum and UF2
bootloader metadata. Use an empty or sacrificial SD card first: `DESKOS_SD_STATUS`
is non-formatting, but a mounted usable filesystem may get a `/deskos`
directory.

After flashing the RP2040 bridge, validate through the ESP32 console on COM12:

```powershell
python .\scripts\rp2040_sd_bridge_preflight_d1l.py --port COM12 --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware
python .\scripts\sd_file_canary_d1l.py --port COM12
python .\scripts\sd_retained_history_acceptance_d1l.py --port COM12
python .\scripts\soak_d1l.py --port COM12 --duration-sec 90 --sample-interval-sec 30 --sample-storage --sd-file-canary
python .\tools\rp2040_sd_protocol.py --request DESKOS_SD_PING
python .\tools\rp2040_sd_protocol.py --scenario ready --file-canary-transcript
```

The preflight sends `rp2040 ping` before `storage status`, so UART protocol
health can be proven without touching SD. It then uses explicit `storage mount`
for the SD-touch attempt and sends `storage diag` as optional non-destructive
evidence when the flashed bridge supports `DESKOS_SD_DIAG`. The canary sends
`storage status`, `storage filecanary`, `storage status`,
`packets`, and `health`. It does not send Public RF or any formatting command.
The SD-aware soak repeats `storage status` and
`storage filecanary` during a passive stability window. The retained-history
acceptance runner seeds synthetic Public/DM/route/packet rows without Public RF,
reboots, and verifies those rows are still readable. The protocol transcript
command is host-only and prints the deterministic `DESKOS_SD_FILE v=1` request
sequence that mirrors the ESP32 `storage filecanary` path.

See `docs/RP2040_SD_BRIDGE_FLASH_D1L.md` for the full flash/proof runbook.

## Runtime Notes

- `DESKOS_SD_STATUS` is safe to call during boot and UI polling. It does not
  probe, mount, format, or write SD. Before an explicit mount it reports
  `state=mount_required`; while an explicit mount worker is running it may
  report `state=mount_pending`; after `DESKOS_SD_MOUNT` or file operations,
  it returns the cached latest SD state. Ready cached cards
  advertise `file_ops=1`, `file_line_max=512`, `file_chunk_max=192`,
  `path_max=96`, and `atomic_rename=1`. Status replies include optional cached
  probe diagnostics:
  `probe_power`, `probe_mode`, `probe_present`, `probe_err`, `probe_data`,
  `mount_err`, and `mount_data`.
- `DESKOS_SD_MOUNT` is the deliberate SD-touch request used by `storage mount`.
  It starts the SD probe/mount worker on the second RP2040 core, may immediately
  report `state=mount_pending`, and tries the direct SdFat filesystem mount
  before using bounded raw SPI probes to find viable high/low rail and
  dedicated/shared SPI candidates. Every raw-present candidate gets a filesystem
  mount attempt before the bridge declares the card unmountable. Failed direct
  mount attempts report the captured SdFat `mount_err` and `mount_data` bytes.
  The filesystem mount force-cycles the rail before each SdFat init, uses the
  same SPI1 pin map at the bridge's conservative 1 MHz SD clock, and retries
  once after resetting the SdFat state. No electrical card
  reports `no_card`; an inserted card with an unusable filesystem reports
  `not_fat32_or_unmountable` and `needs_fat32=1`. Users must prepare FAT32
  cards on a computer.
- `DESKOS_SD_PING` reports protocol/file-operation limits and `sd_touch=0`
  without probing, mounting, formatting, or writing SD. ESP32 exposes this as
  `rp2040 ping` for bridge-app validation before any SD-specific request.
- `DESKOS_SD_DIAG` is a manual diagnostic request used by `storage diag`. It
  reports the pin contract, selected rail/SPI mode, and the high/dedicated,
  high/shared, low/dedicated, and low/shared raw probe result. It returns a
  pending-shaped diagnostic line instead of blocking the UART while another SD
  worker is running, uses only the bounded raw SPI probe, is non-formatting, and
  does not write to the card.
- The bridge has no SD formatting command. If a FAT32 card is mounted and the
  `/deskos` structure is missing, the bridge creates the DeskOS directories and
  manifests. If the card is absent, unmountable, not FAT32, or has invalid
  DeskOS manifests, the ESP32 keeps NVS fallback active and tells the user what
  to fix on a computer.
- `DESKOS_SD_FILE v=1 ...` provides bounded generic file operations under
  `/deskos`: `stat`, `read`, `write`, `append`, `delete`, and `rename`. Payloads
  use base64url without padding, CRC32 checks, sanitized relative paths, and
  192-byte chunks so each newline-delimited request remains under the 512-byte
  line cap. `rename replace=1` uses a backup/rollback step when replacing an
  existing final file so ordinary rename failures do not erase the old final.
- No formatting happens at boot, ping, status, mount, diagnostics, or file
  checks.
- Retained Public message history, DM history, route history, and packet history
  can use the SD file protocol once the ESP32 sees a ready card, file
  operations, and atomic rename. The ESP32 keeps onboard NVS mirrors for these retained
  history stores. Diagnostic exports, sampled user-data exports under
  `exports/data`, and the map-tile cache can use the same file gate. Settings,
  identity, contacts, read-state, crashlog, and the full map page/tile download
  policy remain onboard/fallback-backed or pending.
