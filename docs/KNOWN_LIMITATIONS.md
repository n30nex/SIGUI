# Known Limitations

As of the 2026-06-29 D1L hardware validation:

- Manual visual confirmation of display bars and touch target movement is still pending; the serial `display test` and `touch test` commands return OK on hardware.
- MeshCore Public group text TX/RX and signed advert TX/RX are implemented and validated through the serial console, but full MeshCore C++ DM, contacts, route, and store integration is still pending.
- Settings are implemented as an NVS-backed firmware model and have passed reboot persistence smoke on real D1L flash.
- `identity status` now generates and reports an NVS-retained Ed25519 local identity fingerprint. This is a minimal firmware identity model, not the final full MeshCore C++ store integration.
- MeshCore 3-byte companion framing is implemented and host-tested, but the live binary bridge to MeshCore protocol frames is not enabled in Phase 1.
- Packet log storage is an in-memory Phase 1 ring only. Persistent message/contact/route stores are still pending.
- Public chat UI, DMs, nodes, contacts, routes, packet stores, BLE companion, Wi-Fi management, OTA, and message/contact persistence are not implemented yet.
- The SX1262 Phase 1 probe intentionally avoids TCXO/DIO3 control. TCXO remains `NONE` by default.
- Wi-Fi and BLE are reported disabled by the Phase 1 console.
