# 3-Byte Companion Compatibility

Date: 2026-06-29

MeshCore DeskOS D1L must be compatible with MeshCore companion clients in both meanings used by current MeshCore references.

## 1. Companion Transport Header

MeshCore serial and Wi-Fi companion links delimit each companion protocol payload with a 3-byte transport header:

```text
[type][length_lsb][length_msb][payload...]
```

- App/client to radio: type byte `<` (`0x3c`).
- Radio to app/client: type byte `>` (`0x3e`).
- Length is an unsigned 16-bit little-endian payload length.
- Payload is the MeshCore companion protocol frame.

Reference evidence:

- `third_party/MeshCore/src/helpers/ArduinoSerialInterface.cpp` writes `>` plus length LSB/MSB and reads `<` plus length LSB/MSB.
- `third_party/MeshCore/src/helpers/esp32/SerialWifiInterface.cpp` uses the same framing for Wi-Fi and documents the 3-byte frame header.

Project status:

- `main/comms/companion_3byte.*` implements the ESP-IDF C codec.
- `tools/d1l/companion3.py` mirrors the codec for host tests and future tooling.
- `companion status` reports the active compatibility contract through the Phase 1 USB JSONL console.
- The binary USB/Wi-Fi bridge to live MeshCore frames remains a Phase 2/5 integration item so Phase 1 can stay focused on D1L hardware validation.

## 2. 3-Byte Path Hash Support

MeshCore packet path metadata can encode 1-, 2-, or 3-byte path hashes. The path length byte stores hop count in bits 0-5 and hash-size code in bits 6-7:

- `0b00`: 1-byte path hashes.
- `0b01`: 2-byte path hashes.
- `0b10`: 3-byte path hashes.
- `0b11`: reserved / unsupported.

Reference evidence:

- `third_party/MeshCore/docs/packet_format.md` documents 3-byte path hashes as supported in current firmware.
- `third_party/MeshCore/docs/faq.md` explains that firmware 1.14+ repeaters forward 1-, 2-, and 3-byte path-hash packets, while older repeaters drop 2- and 3-byte path-hash packets.

Project policy:

- D1L companion metadata and packet logs must preserve the encoded hash size and raw path bytes.
- Default message path hash size remains 1 byte for maximum legacy repeater compatibility until the user or client selects 2 or 3 bytes.
- The UI/settings model must expose path-hash mode later without implying that it changes repeater forwarding behavior.
- Diagnostics should warn that 3-byte paths can be dropped by repeaters older than MeshCore firmware 1.14.
