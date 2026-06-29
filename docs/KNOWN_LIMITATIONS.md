# Known Limitations

As of the 2026-06-29 D1L hardware validation:

- Manual visual confirmation of display bars and touch target movement is still pending; the serial `display test` and `touch test` commands return OK on hardware.
- MeshCore Public group text TX/RX and signed advert TX/RX are implemented and validated through the serial console, firmware-local contacts can promote heard nodes by fingerprint, full advertised public keys are retained for DM targeting and have passed reboot persistence on `COM7`, firmware-local routes are learned from Public/advert path metadata, and serial DM flood TX plus the bounded DM store are validated. DM ACK/PATH receive parsing and direct-route TX selection are implemented, but controlled ACK/PATH RF proof, inbound DM proof, and touch DM workflow are still pending.
- Settings are implemented as an NVS-backed firmware model and have passed reboot persistence smoke on real D1L flash.
- `identity status` now generates and reports an NVS-retained Ed25519 local identity fingerprint. This is a minimal firmware identity model, not the final full MeshCore C++ store integration.
- MeshCore TX timestamps are retained monotonically in NVS to avoid duplicate filtering, but a real RTC/network time source is not wired yet.
- MeshCore 3-byte companion framing is implemented and host-tested, but the live binary bridge to MeshCore protocol frames is not enabled in Phase 1.
- Packet log storage is an in-memory bounded ring only. The Public message store, DM store, heard-node store, contact store, and route store are NVS-backed, but persistent packet stores are still pending.
- The Phase 3/4 touch shell is implemented with Home, Public `test` action, first Public free-text composer, first contact-row DM composer, persisted recent Public/DM preview rows, newest contact/heard-node rows, route count, Packet log, Settings, advert sheet, toasts, and lock overlay, but full Public scrollback/search, threaded DM view, contact detail/editing, route detail views, packet stores, BLE companion, Wi-Fi management, and OTA are not implemented yet.
- The Public composer and contact-row DM composer have passed host contract tests, local ESP-IDF build, flash, and serial smoke/backend probes; manual physical touch entry on the composer keyboard is still pending.
- The SX1262 Phase 1 probe intentionally avoids TCXO/DIO3 control. TCXO remains `NONE` by default.
- Wi-Fi and BLE are reported disabled by the Phase 1 console.
