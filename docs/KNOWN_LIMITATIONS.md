# Known Limitations

As of the 2026-06-29 D1L hardware validation:

- Manual visual confirmation of display bars and touch target movement is still pending; the serial `display test` and `touch test` commands return OK on hardware.
- MeshCore Public group text TX/RX and signed advert TX/RX are implemented and validated through the serial console, and a firmware-local contact store can promote heard nodes by fingerprint. Full MeshCore C++ DM, route, and store integration is still pending.
- Settings are implemented as an NVS-backed firmware model and have passed reboot persistence smoke on real D1L flash.
- `identity status` now generates and reports an NVS-retained Ed25519 local identity fingerprint. This is a minimal firmware identity model, not the final full MeshCore C++ store integration.
- MeshCore TX timestamps are retained monotonically in NVS to avoid duplicate filtering, but a real RTC/network time source is not wired yet.
- MeshCore 3-byte companion framing is implemented and host-tested, but the live binary bridge to MeshCore protocol frames is not enabled in Phase 1.
- Packet log storage is an in-memory bounded ring only. The Public message store, heard-node store, and contact store are NVS-backed, but persistent routes, packet stores, and DM stores are still pending.
- The Phase 3/4 touch shell is implemented with Home, Public `test` action, first Public free-text composer, persisted recent Public rows, newest contact/heard-node rows, Packet log, Settings, advert sheet, toasts, and lock overlay, but full Public scrollback/search, DMs, contact detail/editing, routes, packet stores, BLE companion, Wi-Fi management, OTA, and route persistence are not implemented yet.
- The Public composer has passed host contract tests, local ESP-IDF build, flash, serial smoke, and RF regression through the known Public `test` responder path; manual physical touch entry on the composer keyboard is still pending.
- The SX1262 Phase 1 probe intentionally avoids TCXO/DIO3 control. TCXO remains `NONE` by default.
- Wi-Fi and BLE are reported disabled by the Phase 1 console.
