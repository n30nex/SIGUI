# D1L RP2040 SD Bridge Protocol

This is the DeskOS line protocol expected on the ESP32-S3 to RP2040 UART for optional D1L SD-card data storage.

The ESP32 side uses raw newline-delimited ASCII at the board UART pin contract documented in `boards/seeed_indicator_d1l/pinmap.json`: ESP32 TX GPIO19, ESP32 RX GPIO20, 115200 baud. The RP2040-side Arduino target lives at `firmware/rp2040_sd_bridge/deskos_sd_bridge`, and `tools/rp2040_sd_protocol.py` is the host reference simulator for this contract.

## Status Request

ESP32 sends:

```text
DESKOS_SD_STATUS
```

RP2040 replies with one line:

```text
DESKOS_SD_STATUS state=ready present=1 mounted=1 deskos=1 fs=fat32 format_required=0 format_supported=1 capacity_kb=31166976 free_kb=31100000 note=ready
```

Required tokens:

- `state`: stable machine state. Use `no_card`, `ready`, `setup_required`, `unformatted`, or `error` when possible.
- `present`: `1` when a card is electrically present.
- `mounted`: `1` when the filesystem is mounted and usable.
- `deskos`: `1` when the `/deskos` data root exists or has been created.
- `fs`: filesystem label such as `fat32`, `fatfs`, `exfat`, `unknown`, or `none`.
- `format_required`: `1` only when the card is present but cannot be used without setup/format.
- `format_supported`: `1` only when the RP2040 firmware can perform the confirmed format command.
- `capacity_kb` and `free_kb`: unsigned decimal kilobytes, `0` when unknown.

Values must not contain spaces. Use underscores in `note`.

## Confirmed Format Request

Formatting is never sent during boot, `storage status`, or plain `storage setup`. It is sent only after the user enters the exact confirmation phrase and only if the latest status reported `present=1`, `format_supported=1`, and setup is required.

ESP32 sends:

```text
DESKOS_SD_FORMAT FORMAT-DESKOS-SD
```

RP2040 should reply with the post-format status, preferably using the format prefix:

```text
DESKOS_SD_FORMAT state=ready present=1 mounted=1 deskos=1 fs=fat32 format_required=0 format_supported=1 capacity_kb=31166976 free_kb=31166976 note=format_complete
```

The ESP32 parser also accepts a `DESKOS_SD_STATUS ...` line after a format request, but `DESKOS_SD_FORMAT ...` is preferred for traceability.

## Safety Rules

- No automatic format on boot.
- No format from incidental touch events.
- No format when no card is present.
- No format when the RP2040 did not first report `format_supported=1`.
- Settings, identity, and minimum boot-critical state remain on onboard NVS.
- Until SD-backed stores are implemented, valid SD cards are reported as `store_migration_pending` and retained stores remain on NVS.
- The RP2040 bridge may create `/deskos` on a mounted card, but message, packet, route, export, and map-tile stores remain on onboard NVS until the SD-backed store migration lands.
