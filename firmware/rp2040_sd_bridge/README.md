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
- UART baud: 115200.

The pin values are based on Seeed's SenseCAP Indicator RP2040 Arduino examples.
This bridge code is original project code and intentionally keeps the protocol
plain ASCII rather than Seeed's sensor `PacketSerial` framing.

## Build

Firmware builds are run in GitHub Actions. The workflow installs Arduino CLI,
adds the `earlephilhower/arduino-pico` board package URL, installs
`rp2040:rp2040`, and compiles the sketch with FQBN
`rp2040:rp2040:seeed_indicator_rp2040`.

The bridge emits checksummed artifacts under `rp2040-sd-bridge-firmware`.
Do not use the Windows host for firmware compilation.

## Runtime Notes

- `DESKOS_SD_STATUS` mounts the card if possible and creates `/deskos` when the
  filesystem is usable.
- `DESKOS_SD_FORMAT FORMAT-DESKOS-SD` is the only formatting command.
- No formatting happens at boot or during status checks.
- Retained DeskOS stores still remain on ESP32 onboard NVS until the SD-backed
  store migration lands.
