# Known Limitations

As of the 2026-06-29 D1L hardware validation:

- Manual visual confirmation of display bars and touch target movement is still pending; the serial `display test` and `touch test` commands return OK on hardware.
- MeshCore protocol integration is stubbed until Phase 2.
- Settings are implemented as an NVS-backed firmware model and have passed reboot persistence smoke on real D1L flash.
- `identity status` is a placeholder for the future MeshCore C++ `LocalIdentity`; no MeshCore public/private keypair is generated yet.
- MeshCore 3-byte companion framing is implemented and host-tested, but the live binary bridge to MeshCore protocol frames is not enabled in Phase 1.
- Packet log storage is an in-memory Phase 1 ring only. Persistent message/contact/route stores are still pending.
- Public chat, DMs, nodes, contacts, routes, packet stores, BLE companion, Wi-Fi management, OTA, and message/contact persistence are not implemented yet.
- The SX1262 Phase 1 probe intentionally avoids TCXO/DIO3 control. TCXO remains `NONE` by default.
- Wi-Fi and BLE are reported disabled by the Phase 1 console.
