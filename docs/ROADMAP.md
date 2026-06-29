# MeshCore DeskOS D1L — Codex Handoff Roadmap

**Target:** Seeed SenseCAP Indicator D1L desktop GUI firmware for MeshCore  
**Prepared for:** GPT-5.5 Codex xhigh / agentic implementation  
**Date:** 2026-06-29  
**Working name:** `MeshCore DeskOS D1L`  
**Primary goal:** A beautiful, reliable, desktop-style MeshCore standalone Wi-Fi/BLE/USB companion firmware for the Seeed Indicator D1L, using the large 480×480 touch display as the main control surface for local MeshCore mesh activity.

---

## 1. Executive goal

Build a production-quality firmware for the **Seeed SenseCAP Indicator D1L** that turns it into a desk-console for a local MeshCore mesh. It should feel like a polished modern dark mobile OS scaled up to a 4-inch square display: large cards, readable text, low-glare colors, iPhone-style dark spacing and hierarchy, smooth transitions, and no cramped retro terminal feel except inside the optional developer console.

The firmware should let a normal MeshCore user interact with nearby nodes without needing a phone open all the time:

- See mesh status at a glance.
- Send and receive public room/channel chat.
- Send and receive MeshCore direct messages.
- Browse heard nodes, repeaters, room servers, routes, telemetry, SNR/RSSI, and last-heard data.
- Advertise the local node.
- Configure radio settings safely, defaulting to the Canada/USA MeshCore preset.
- Use USB serial, BLE, and Wi-Fi companion/management modes where feasible.
- Use a large-format touch-first UI inspired by SigurdOS/LimitlezzOS, but adapted for the D1L hardware and a desk-node workflow.

This is **not** a generic dashboard mockup. It must become buildable, flashable firmware with staged hardware validation.

---

## 2. Non-negotiables

1. **Target hardware is Seeed SenseCAP Indicator D1L, not LilyGO T-Deck.**  
   The source inspirations are T-Deck-heavy, but all board code must be ported to D1L.

2. **Canada/USA radio defaults.**  
   Default first-boot radio profile:
   - Frequency: `910.525 MHz`
   - Bandwidth: `62.5 kHz`
   - Spreading Factor: `SF7`
   - Coding Rate: `CR5`
   - TX power: safe legal/hardware-limited default, initially `20 dBm` unless the board/HAL proves `21 dBm` is safe and region-compliant.
   - LoRa TCXO voltage for the D1L SX1262 must be `NONE`, not `2.4V`, unless board revision testing proves otherwise.

3. **MeshCore first.**  
   Do not spend v1 time on Meshtastic dual-stack unless it is isolated behind a compile-time experimental flag. This firmware is for MeshCore Canada / MeshCore local mesh interaction.

4. **Local mesh first, cloud optional.**  
   The device must work fully offline. Wi-Fi can provide OTA, settings, time sync, optional observer upload, and optional browser management, but no cloud dependency is allowed for normal messaging.

5. **Touch-first, desk-first.**  
   The D1L has a large 480×480 capacitive touch display and no physical keyboard. All core workflows must be easy by touch. Serial console is for developers and recovery only.

6. **No hardcoded serial port.**  
   The user will tell Codex which COM port to use. Implementation scripts must accept `D1L_PORT` or `--port`. Preserve the previous safety note: do not default to COM11 or COM29.

7. **Do not brick the device.**  
   Add safe flash/recovery scripts, document erase vs non-erase flashing, and where possible add a read/backup step before first full erase.

8. **No copied Apple assets or third-party trademarks.**  
   “iPhone dark UI” means dark-mode mobile UX principles: spacing, typography hierarchy, rounded grouped cards, status indicators, sheet modals, badges, and motion. Do not use Apple logos, SF Symbols, proprietary artwork, or copied UI assets.

9. **License hygiene.**  
   SigurdOS is GPL-3.0-or-later. If code is copied/adapted from SigurdOS, keep GPL-compatible licensing and attribution. If the desired final repo needs a different license, reimplement patterns instead of copying GPL code.

---

## 3. Source/reference hierarchy

### 3.1 Primary base/reference: SigurdOS T-Deck

Repository: `https://github.com/hermes-gadget/SigurdOS-tdeck`

Use SigurdOS as the main software architecture reference because it already has:

- MeshCore integration via a submodule.
- LVGL UI architecture.
- Mesh wrapper, message store, contact store, region handling, BLE companion bridge, diagnostics, packet telemetry, Wi-Fi OTA, QR display, and screen routing.
- A large native test suite covering build contracts, messaging, storage, touch, Wi-Fi, packet logs, preferences, theme, UI contracts, and more.

Do **not** assume the T-Deck HAL works on the D1L. The D1L display, touch, IO expander, power model, RP2040, and radio pins differ.

### 3.2 Strong feature-parity reference: MeshCoreTerm

Repository: `https://github.com/dabeani/meshcoreterm`

Use MeshCoreTerm as the best checklist for expected MeshCore companion UI features, especially:

- Contacts, channels/users/DMs, map, and management tabs.
- Messenger-style channel/DM threads.
- Unread counts, timestamps, message details, send confirmations.
- Path/hops, RSSI/SNR, overheard repeaters, route hints.
- Sortable/filterable contacts, reachability and GPS badges.
- Radio settings, Wi-Fi, BLE PIN, screen timeout, GPS pins/baud where applicable.
- Raw RX log, parsed packet metadata, telemetry requests/history, ping helper, neighbor repeater scan.
- Persistence with NVS/preferences.

### 3.3 UX/navigation inspiration: LimitlezzOS

Repository: `https://github.com/ItsLimitlezz/LimitlezzOS`

Use LimitlezzOS for interaction ideas:

- iPhone-style dark UI language.
- Unread badges and muted chats.
- Lock screen notification cards.
- Virtualized node/message lists so large meshes do not exhaust memory.
- First-boot onboarding identity flow.
- Simulator-first workflow.
- USB/BLE companion bridge testing patterns.

Do not copy Meshtastic-only logic into the MeshCore v1 path unless isolated.

### 3.4 Scaffold/reference only: realtag meshcore-tdeck-plus-lvgl

Repository: `https://github.com/realtag-github/meshcore-tdeck-plus-lvgl`

Treat this repo as a planning/scaffold reference only. It explicitly warns that it is experimental and not ready for normal use. Useful ideas include:

- Simulator-first LVGL workflow.
- Firmware service abstraction.
- Test/release checklist structure.
- Packaging strategy.

Do not use it as the main working base.

### 3.5 Hardware source of truth: Seeed D1L docs/SDK

Use these as D1L board/HAL references:

- `https://wiki.seeedstudio.com/Sensor/SenseCAP/SenseCAP_Indicator/Get_started_with_SenseCAP_Indicator/`
- `https://github.com/Seeed-Solution/sensecap_indicator_esp32`
- `https://devices.esphome.io/devices/seeed-sensecap/`

Important: Seeed’s ESP32 SDK asks for ESP-IDF `v5.1.x`. If SigurdOS/PlatformIO Arduino makes the D1L display/radio too painful, port the app/UI architecture to ESP-IDF v5.1.x and keep the UI/mesh/application layers portable.

---

## 4. Hardware facts and D1L HAL checklist

### 4.1 D1L hardware profile

Expected D1L model characteristics:

- ESP32-S3 main MCU.
- RP2040 coprocessor.
- 3.95/4-inch 480×480 capacitive RGB touch screen.
- ST7701S / RGB panel path with PCA9535/PCA9554-style expander usage.
- FT5x06 touch controller over I2C.
- SX1262 LoRa radio on D1L/D1Pro variants.
- Wi-Fi 2.4 GHz and BLE 5.0 LE.
- USB-C powered; no internal battery on the normal Indicator spec.
- MicroSD support is handled by the RP2040 side in common docs, so storage strategy must be verified.

### 4.2 Known ESP32-S3 pin map to verify in code

| Subsystem | Known/expected pins |
|---|---|
| SPI | CLK `GPIO41`, MOSI `GPIO48`, MISO `GPIO47` |
| I2C | SDA `GPIO39`, SCL `GPIO40` |
| IO expander | I2C address commonly `0x20`; verify PCA9535/PCA9554 handling |
| Display RGB | HSYNC `GPIO16`, VSYNC `GPIO17`, DE `GPIO18`, PCLK `GPIO21`; RGB data pins `GPIO0–15` as documented |
| Display SPI control | CS expander pin `4`, RESET expander pin `5` |
| Touch | FT5x06 over I2C; TP_INT expander `6`, TP_RESET expander `7` in references |
| Backlight | `GPIO45` PWM |
| User button | `GPIO38`, normally high / inverted press |
| ESP32 ↔ RP2040 UART | ESP RX `GPIO20`, ESP TX `GPIO19` |
| SX1262 | SPI shared pins plus CS expander `0`, RESET expander `1`, BUSY expander `2`, DIO1 expander `3` |
| SX1262 TCXO | `NONE` on Seeed Indicator v1 D1L style references |

### 4.3 Board bring-up order

Bring up in this order. Do not skip ahead to UI polish until these pass:

1. Serial boot log with build/version info.
2. I2C scan and IO expander detection.
3. Backlight PWM and display panel init.
4. LVGL full-screen color test and touch calibration screen.
5. Button wake/dim behavior.
6. SX1262 hardware version/read test.
7. Radio RX idle test on Canada/USA preset.
8. MeshCore advert receive/decode.
9. MeshCore local identity and advert transmit.
10. Basic message TX/RX with a second known-good MeshCore node.

---

## 5. Product UX model

### 5.1 Visual language

Create an original dark mobile-style UI:

- Background: near-black / deep blue-black, not pure black.
- Cards: rounded dark graphite panels with subtle borders and soft elevation.
- Accent colors: cyan/teal for online mesh health, amber for warnings/routes, red only for errors/unread critical alerts.
- Typography: large readable labels; prioritize 480×480 readability at desk distance.
- Motion: short transitions, no distracting animation loops; status changes should be visible but calm.
- Icons: simple original vector/LVGL icons, no proprietary mobile OS assets.
- Touch target minimum: 44×44 px equivalent; prefer 56×56+ for home tiles.
- High contrast mode: must be available.
- Low-brightness night mode: must be available.

### 5.2 Shell/navigation

Use a simple model:

- Top status bar: time, radio status, MeshCore state, Wi-Fi/BLE/USB state, TX/RX activity, alert indicator.
- Home dashboard: glanceable mesh health plus app tiles.
- Bottom dock or side rail: Messages, Nodes, Mesh, Tools, Settings.
- System sheets: slide-up or centered modal cards for quick actions.
- Back navigation: persistent top-left back affordance and swipe/back gesture where reliable.
- Long press: use sparingly for mute/favorite/quick actions; always provide a visible menu alternative.

### 5.3 First-boot onboarding

On clean NVS/storage, show:

1. Welcome: “MeshCore DeskOS D1L”.
2. Choose node name / short label.
3. Confirm region: Canada/USA preset selected by default.
4. Choose role: Desk Companion default.
5. Choose optional radios: BLE companion on/off, Wi-Fi off by default unless needed for setup.
6. Privacy/observer note: optional MeshCore.ca observer upload is disabled by default.
7. Save identity and show the home screen.

---

## 6. Required feature set

### 6.1 Home dashboard

Must show at a glance:

- Current radio profile: `US/CAN 910.525 / BW62.5 / SF7 / CR5`.
- Node identity and public key short fingerprint.
- Mesh status: listening, TX, RX, last packet age.
- Last message preview.
- Heard nodes count in last 15 min / 1 h / 24 h.
- Repeaters heard.
- Room servers heard.
- Signal summary: latest RSSI/SNR and packet quality.
- Quick buttons: Send, Advertise, Nodes, Packets, Settings.

### 6.2 Messaging

Implement:

- Public MeshCore chat/room view.
- Direct messages.
- Message bubbles with timestamps and delivery state.
- Retry/resend failed messages.
- Quick replies.
- Draft persistence.
- Unread badges.
- Muted conversations.
- Message detail sheet: packet ID/hash if available, path/hops, RSSI/SNR, repeated/overheard hints, timestamp, delivery/ACK state.
- Search/filter within recent messages if feasible; otherwise phase it after v1.

### 6.3 Nodes and contacts

Implement:

- Heard nodes list with virtualized rows.
- Contact list separate from all heard nodes.
- Node cards with name, role, public-key fingerprint, last heard, RSSI/SNR, path, route confidence, and source.
- Favorites/pinned nodes.
- Node actions: DM, ping, trace/path, request telemetry, copy/export contact QR.
- Role badges: Companion, Repeater, Room Server, Sensor/Telemetry, Unknown.
- Sort modes: last heard, signal, name, role, favorites.

### 6.4 Repeaters, routes, and room servers

Implement:

- Repeaters heard list.
- Room servers heard list.
- Route/path view for known contacts.
- Path reliability summary.
- Traceroute/ping helper if MeshCore APIs support it on this firmware path.
- Remote management should start read-only or behind an “advanced/admin” mode until thoroughly tested.

### 6.5 Packets and diagnostics

Implement:

- Recent packet feed.
- Parsed metadata: type, source, destination/room if known, RSSI, SNR, hop/path, timestamp.
- Raw packet hex only in developer mode.
- Counters: RX, TX, decode fail, decrypt fail, dropped, duplicate, queue depth.
- Health page: heap, PSRAM, LVGL memory, task watermarks, uptime, reset reason, radio state, Wi-Fi/BLE state.
- Export/debug dump over serial.

### 6.6 Radio settings

Implement safely:

- Canada/USA preset default.
- Editable frequency, bandwidth, SF, CR, TX power, RX boost where supported.
- Warn before changing settings because mismatched RF settings isolate the user from the local mesh.
- Save to NVS/preferences.
- “Reset to Canada/USA MeshCore defaults” button.
- “Temporary test until reboot” option for experimental radio profile changes.

### 6.7 BLE / Wi-Fi / USB companion behavior

Expected companion modes:

- USB serial console for developer diagnostics and scripted smoke tests.
- BLE companion mode for MeshCore phone/client app compatibility if MeshCore APIs allow it.
- Wi-Fi setup/management mode for OTA and local browser UI if feasible.
- Wi-Fi off by default in normal desk mode unless user enables it.
- BLE/Wi-Fi coexistence must be measured; if memory pressure is high, enforce one active high-level companion radio at a time.

### 6.8 MeshCore.ca / Canada desk mode

Add compile-time flag or settings namespace:

- `MESHCORE_CA_DESK_MODE=1`
- Default region label: Canada/USA.
- Optional branding text: “MeshCore Canada” / “Canadaverse” only in About/onboarding, not overdone.
- Optional observer upload stub, disabled by default.
- Optional MQTT observer settings screen, disabled by default until validated.
- Use terms familiar to MeshCore Canada tools: nodes, repeaters, room servers, routes, observers, packets, SNR/RSSI.

---

## 7. Firmware architecture

### 7.1 Recommended repository layout

```text
/
├── boards/
│   └── seeed_indicator_d1l/          # board definition, pin map, sdk notes
├── docs/
│   ├── MeshCore_DeskOS_D1L_Codex_Handoff_Roadmap.md
│   ├── D1L_BOARD_BRINGUP.md
│   ├── UI_SPEC_480x480_DARK.md
│   ├── TEST_PLAN_D1L.md
│   ├── FLASH_RECOVERY_D1L.md
│   └── RELEASE_CHECKLIST.md
├── src/
│   ├── main.cpp or main.c
│   ├── hal/
│   │   ├── indicator_board.*
│   │   ├── indicator_pins.*
│   │   ├── display_st7701_rgb.*
│   │   ├── touch_ft5x06.*
│   │   ├── backlight.*
│   │   ├── button.*
│   │   ├── sx1262_indicator.*
│   │   ├── rp2040_bridge.*
│   │   └── storage.*
│   ├── mesh/
│   │   ├── meshcore_service.*
│   │   ├── meshcore_radio_profile.*
│   │   ├── message_store.*
│   │   ├── contact_store.*
│   │   ├── route_store.*
│   │   └── packet_log.*
│   ├── app/
│   │   ├── event_bus.*
│   │   ├── app_model.*
│   │   ├── settings_model.*
│   │   └── notification_model.*
│   ├── ui/
│   │   ├── theme_dark_mobile.*
│   │   ├── layout_480.*
│   │   ├── nav.*
│   │   ├── screens/
│   │   └── widgets/
│   ├── comms/
│   │   ├── usb_console.*
│   │   ├── ble_companion.*
│   │   ├── wifi_manager.*
│   │   └── ota_server.*
│   └── diagnostics/
│       ├── log.*
│       ├── crash_ring.*
│       ├── health_monitor.*
│       └── smoke_hooks.*
├── test/
│   ├── native/
│   ├── hardware_smoke/
│   └── screenshots/
├── scripts/
│   ├── build_d1l.*
│   ├── flash_d1l.*
│   ├── monitor_d1l.*
│   ├── smoke_d1l.py
│   └── backup_flash_d1l.py
└── platformio.ini / CMakeLists.txt / sdkconfig
```

### 7.2 Build-framework decision

Codex must start with a short build-framework spike and write the decision into `docs/D1L_BUILD_DECISION.md`.

Preferred path:

1. Preserve SigurdOS application, MeshCore, storage, diagnostics, and LVGL organization where legally and technically practical.
2. Add a D1L board environment.
3. Replace T-Deck HAL with Seeed Indicator D1L HAL.

Decision fork:

- **If PlatformIO/Arduino can reliably drive ST7701S RGB, FT5x06, PCA9535/PCA9554, and SX1262 on D1L**, keep the PlatformIO flow for speed.
- **If the D1L display/radio stack is blocked**, switch the D1L build to ESP-IDF v5.1.x using Seeed’s SDK examples, while keeping app/UI/mesh services portable.

Acceptance for the framework decision:

- Display color test works.
- Touch logs real coordinates.
- SX1262 can be queried.
- Build can be repeated from clean checkout.

---

## 8. Sub-agent work plan

Use sub-agents for speed. The integration lead owns merge order and prevents conflicting edits.

### Agent A — Source audit and licensing

- Map SigurdOS, LimitlezzOS, MeshCoreTerm, realtag scaffold, MeshCore upstream, and Seeed SDK.
- Identify reusable modules and license constraints.
- Produce `docs/SOURCE_AUDIT_AND_ATTRIBUTION.md`.
- Flag any GPL/MIT/Apache incompatibility.

### Agent B — D1L HAL / board bring-up

- Implement board definition and pin map.
- Bring up display, touch, backlight, button, I2C, IO expander, SX1262.
- Create serial diagnostics commands: `board`, `i2c`, `touch`, `display`, `radiohw`.
- Produce `docs/D1L_BOARD_BRINGUP.md`.

### Agent C — MeshCore integration

- Wire MeshCore library/service to the D1L radio abstraction.
- Implement Canada/USA defaults.
- Implement identity, advert, RX/TX, packet parsing, message events.
- Add unit tests and a serial smoke test.

### Agent D — UI/UX and LVGL

- Build 480×480 dark UI shell.
- Implement home, messages, nodes, packet log, settings, radio profile, and diagnostics screens.
- Add responsive layout helpers and virtualized lists.
- Produce screenshots from simulator or hardware capture where possible.

### Agent E — Companion and connectivity

- Implement USB console.
- Implement BLE companion compatibility if MeshCore APIs allow.
- Implement Wi-Fi setup/OTA management.
- Enforce safe Wi-Fi/BLE memory policy.

### Agent F — Persistence and data model

- NVS/settings.
- Message store.
- Contact store.
- Route store.
- Packet ring buffer.
- Crash/reset health ring.

### Agent G — QA, flashing, packaging

- Build scripts.
- Flash/monitor scripts accepting `--port` / `D1L_PORT`.
- Backup/recovery docs.
- Hardware smoke tests.
- Release artifacts and checksums.

### Agent H — Documentation and final handoff

- Keep README and docs current.
- Write user guide.
- Write developer guide.
- Write known limitations.
- Produce final release checklist.

---

## 9. Phased implementation roadmap

### Phase 0 — Repo prep and decision spike

**Goal:** Prepare the codebase and make the D1L build-framework decision.

Tasks:

- Create branch: `feature/meshcore-deskos-d1l`.
- Add this roadmap to `docs/`.
- Clone/update submodules.
- Add source audit and license notes.
- Decide PlatformIO vs ESP-IDF path for D1L.
- Add `D1L_PORT` support to scripts.
- Add initial CI/build workflow for host/unit tests.

Acceptance:

- Clean checkout builds whatever native/simulator target exists.
- D1L framework decision is documented.
- No hardcoded COM port.

### Phase 1 — D1L display/touch/radio hardware bring-up

**Goal:** D1L boots and proves the hardware stack.

Tasks:

- Serial boot banner with version, commit, build time.
- I2C scan detects expander/touch.
- Display color bars and LVGL hello screen.
- Touch coordinate test and calibration persistence.
- Backlight dim/wake.
- Button handling.
- SX1262 reset/busy/dio1 and register/version test.
- LoRa RX idle and packet log stub.

Acceptance:

- User can flash to supplied COM port.
- Screen shows a stable LVGL test UI.
- Touch input moves/selects a visible target.
- Serial `radiohw` confirms SX1262 wiring or reports exact failure.

### Phase 2 — MeshCore minimum viable radio

**Goal:** The D1L becomes a MeshCore companion radio at the protocol level.

Tasks:

- Integrate MeshCore library.
- Create `MeshCoreService` event API.
- Canada/USA preset defaults.
- Identity generation/storage. Status: validated on 2026-06-29 with Ed25519 fingerprint retained across reboot.
- Advert receive/decode. Status: validated on 2026-06-29 with signed local advert decode.
- Advert transmit. Status: validated on 2026-06-29 with local Meshcorebot `rx_advert_total +1`.
- Public message send/receive with a second MeshCore device. Status: serial-console Public `test` TX/RX validated on 2026-06-29; touch UI workflow still pending.
- Packet counters and basic parse logs. Status: validated for Public text and advert entries; TX timestamps are retained in NVS to avoid duplicate filtering across reboots.

Acceptance:

- D1L can hear at least one live MeshCore packet or controlled test packet. Status: validated on 2026-06-29 with Public replies from local bots.
- D1L can transmit an advert on the Canada/USA preset. Status: validated on 2026-06-29 with two signed advert TX entries and local Meshcorebot advert RX counter movement.
- D1L can send/receive a basic public message in controlled testing. Status: serial-console Public `test` TX/RX validated on 2026-06-29; touch UI workflow still pending.

### Phase 3 — 480×480 dark UI shell

**Goal:** Replace test UI with the real desk-console shell.

Tasks:

- Dark mobile theme. Status: first desk-console shell slice implemented.
- Top status bar. Status: implemented with MeshCore state, counters, identity, and lock action.
- Home dashboard cards. Status: implemented from the app snapshot model.
- Bottom dock/primary navigation. Status: implemented for Home, Messages, Nodes, Packets, and Settings.
- Modal sheets/toasts. Status: implemented for advert actions and touch feedback.
- Lock/standby screen. Status: implemented as a tap-to-unlock overlay.
- Onboarding flow. Status: first persisted first-boot setup sheet is implemented with node-name entry, Canada/USA preset confirmation, Desk Companion/offline-radio defaults, identity generation, serial onboarding diagnostics, simulator coverage, and NVS migration from the previous settings schema; richer optional-radio choices are pending live Wi-Fi/BLE runtime support.
- Simulator/screenshot target if feasible. Status: first deterministic host simulator is implemented in `tools/ui_simulator.py`; it renders the current 480x480 shell and sheet surfaces to PNG and emits `ui-sim-report.json` with required-label and text-overflow checks.

Acceptance:

- UI is readable from desk distance. Status: shell layout is implemented; manual visual confirmation on the physical screen is still pending.
- Home screen updates from simulated and real mesh events. Status: host contract tests cover snapshot wiring, and COM7 hardware smoke plus controlled Public `test` RF validation passed on 2026-06-29.
- No full-screen flicker during updates. Status: no boot-loop or panic observed in hardware smoke; visual flicker review is still pending.
- Touch navigation is reliable. Status: touch actions are routed through the app model and hardware smoke passes; manual navigation review is still pending.

### Phase 4 — Messaging, nodes, contacts

**Goal:** Core MeshCore user workflows are usable without a phone.

Tasks:

- Public chat screen. Status: first touch Public screen is implemented with persisted recent message rows.
- DM screen. Status: first persisted DM preview is implemented in the Messages screen, prerequisite full public-key retention from signed adverts into heard nodes and promoted contacts is implemented and validated on `COM7`, a first contact-row touch DM compose flow is implemented/built/flashed, and a first bounded DM thread/detail sheet opens from recent DM rows with a `Reply` action; manual touch review and richer full-thread history are still pending.
- Message compose with on-screen keyboard or large input flow. Status: first Public free-text composer and contact-row DM composer are implemented beside the fixed `test` quick action and were built/flashed on `COM7`; manual physical touch entry review is pending.
- Quick replies. Status: first fixed Public `test` quick action is implemented.
- Message delivery states. Status: persisted rows show queued TX, received RX, and DM TX ACK hash fields; MeshCore ACK RX parsing can mark persisted TX rows acked, but controlled ACK RF proof is still pending.
- Unread/mute/favorite logic. Status: first contact favorite/mute flags are editable through serial diagnostics and the touch contact detail sheet, and favorite/mute persistence across reboot is validated on `COM7`. First persisted Public/DM read-state cursors, `messages unread`, `messages read <public|dm|all>`, Messages-screen unread counters, unread row highlighting, and a touch `Read` action are implemented. Public unread/read persistence is validated on `COM7` with a fresh Public `test` RX window; per-thread DM read cursors and controlled inbound DM unread proof are pending.
- Heard nodes list with virtualization. Status: first bounded persisted heard-node store and newest-node UI rows are implemented and validated on `COM7` with signed local adverts; large-list virtualization and richer filters are pending.
- Contact detail cards. Status: first bounded persisted contact store, `contacts` diagnostics, heard-node promotion command, full advertised public-key retention, first contact detail sheet, and favorite/mute edit actions are implemented and validated on `COM7`; richer detail cards and rename/delete actions are pending.
- Route/path metadata. Status: first bounded persisted route store, `routes` and `routes detail <seq>` diagnostics, MeshCore Public/advert route observation wiring, encrypted PATH return parsing, contact return-path retention, direct-route DM TX selection, Packet-tab route rows, and a first route detail sheet are implemented and locally built/flashed on `COM7`; controlled PATH RF proof, manual touch review of route detail, and trace/ping helpers are pending.
- Node actions: DM, ping/trace if available, telemetry request if available. Status: serial `mesh send dm <fingerprint> <text>`, first touch contact-row `DM` action, and contact-detail `DM` action are implemented for promoted contacts with retained public keys; ping/trace and telemetry actions are pending.

Acceptance:

- User can send and receive public and DM messages from touch UI. Status: controlled Public `test` TX/RX works and persists in the touch Public view; first free-text Public composer is implemented; first contact-row touch DM composer is implemented and routes through the same DM backend as serial; first bounded DM thread/detail sheet with `Reply` is implemented; serial DM flood TX/store/reboot persistence is validated with `YKF Corebot`; DM ACK/PATH receive parsing and direct-route TX selection are implemented. First Public unread/read behavior is validated on `COM7`: after `messages read all`, a fresh Public `test` produced two RX rows, raised `public_unread=2`, cleared with `messages read public`, and stayed cleared after reboot. Manual physical touch entry/thread/reply/read review, inbound DM proof, controlled ACK/PATH RF proof, and direct-route RF proof are pending.
- Node list does not crash or stutter with large simulated meshes. Status: bounded preview rows avoid unbounded UI work, and a stack-overflow boot loop found during hardware smoke was fixed by moving UI snapshot storage off the main task stack; large simulated mesh stress is still pending.
- Message store survives reboot. Status: bounded Public message store validated on `COM7` on 2026-06-29; `messages public` kept TX/RX rows after reboot. Bounded DM store TX persistence was validated on `COM7` with `YKF Corebot`; `messages dm` retained the TX row after reboot. Public read state also survived reboot with `last_public_read_seq=10` and `public_unread=0`. Packet evidence now also persists the newest 8 rows across reboot.
- Contact store survives reboot. Status: bounded contact store validated on `COM7` on 2026-06-29; `contacts add 937D290883817CBD` promoted heard node `Krabs Lagoon` and the row survived reboot. Full public-key retention was validated on `COM7` with `YKF Corebot` fingerprint `0BF0A701D5AE2DB6`; the heard-node and promoted contact public key survived reboot. Favorite/mute contact flags were toggled on, survived reboot, and were toggled off again on `COM7`.
- Route store survives reboot. Status: bounded route store validated on `COM7` on 2026-06-29; Public TX/RX route rows survived reboot, and `routes detail <seq>` matched a fresh Public RX route row after a local Public `test` window.

### Phase 5 — Companion modes and management

**Goal:** D1L behaves like a modern companion device.

Tasks:

- USB serial console commands. Status: implemented for hardware smoke and management diagnostics.
- BLE companion bridge if supported. Status: Phase 5 status/persistence foundation added; BLE runtime stack remains build-disabled until heap/coexistence testing.
- Wi-Fi scan/connect screen. Status: Phase 5 status/persistence foundation added; `wifi status`, safe `wifi scan`, `wifi off`, and Settings-tab companion state are implemented, but DeskOS does not start the Wi-Fi runtime yet.
- Wi-Fi OTA/local management page if feasible.
- Time sync over Wi-Fi if enabled.
- Safe Wi-Fi/BLE coexistence policy. Status: first offline-first one-companion-radio policy is reported by serial and UI.

Acceptance:

- USB console can run hardware smoke tests. Status: implemented and used throughout D1L validation.
- BLE or Wi-Fi companion mode is documented with exact supported clients. Status: pending live BLE/Wi-Fi runtime enablement; current build documents runtime-pending/build-disabled state.
- Wi-Fi can be disabled and remains disabled across reboot if user chooses. Status: `wifi off` persists disabled state through the settings model and was validated on `COM7` with the Phase 5 smoke persistence test.

### Phase 6 — Advanced MeshCore tools

**Goal:** Make the D1L feel like a serious mesh desk instrument.

Tasks:

- Repeaters screen. Status: first read-only serial visibility and a first touch Mesh Roles browser sheet are implemented, built, flashed, smoke-tested, reboot-checked, and Public `test` RF-regression tested on `COM7`; trace/ping actions are pending later UX expansion.
- Room servers screen. Status: first read-only serial/dashboard visibility and a first touch Mesh Roles browser sheet are implemented, built, flashed, smoke-tested, reboot-checked, and Public `test` RF-regression tested on `COM7` from signed heard-node adverts with role `room`.
- Routes/path screen.
- Signal/SNR screen. Status: first `signal` diagnostic plus Home/Packet-tab signal summary cards are implemented, built, flashed, and validated on `COM7` from recent packet, route, and heard-node evidence.
- Packet log with parsed detail. Status: first NVS-backed packet evidence store, `packets detail <seq>`, `packets clear`, touchable Packet-tab rows, and a first packet detail sheet are implemented, built, flashed, and validated on `COM7`; live ring keeps 32 rows and NVS persists the newest 8 rows.
- Telemetry history.
- Neighbor/repeater scan helper.
- QR contact/export.
- Optional manual location/map view.

Acceptance:

- User can inspect how the mesh is behaving without using serial logs. Status: first Home/Packet-tab signal, room-server, and repeater-candidate summaries are validated on `COM7`; richer full-list screens are pending.
- Packet detail screen is useful for debugging real mesh traffic. Status: first detail sheet and serial `packets detail <seq>` are validated with fresh Public TX/RX packet rows; richer filtering/search and raw hex developer mode are pending.
- Advanced/admin actions are gated and cannot be triggered accidentally.

### Phase 7 — Polish, performance, soak

**Goal:** Make it stable and pleasant.

Tasks:

- Fix memory leaks.
- Task watchdog tuning.
- Long-run soak test. Status: first `scripts/soak_d1l.py` harness is implemented for idle/listening and active Public `test` soak windows with heap/PSRAM, task-stack, LVGL, uptime, packet-counter, signal, unread, and crashlog sampling. A 3-minute active Public `test` hardware soak passed on `COM7`; long acceptance soaks are still pending.
- Touch edge-case fixes.
- Dark theme contrast pass.
- UI animation performance pass.
- Crash ring and reset reason display. Status: first NVS-backed reset ring, `crashlog`, `crashlog clear`, richer `health`, and Settings/Home reset/stack display are implemented, built, flashed, and validated on `COM7`.
- Error copy that is understandable to non-developers.

Acceptance:

- 12-hour idle/listening soak without crash. Status: repeatable soak command exists; full-duration run is pending.
- 1-hour active messaging test without UI freeze. Status: repeatable active Public `test` soak command exists and a 3-minute active hardware probe passed with `mesh_tx_packet_delta=3` and `mesh_rx_packet_delta=8`; full-duration run is pending.
- No steadily declining heap/PSRAM in normal use. Status: richer heap/PSRAM/LVGL/task stack telemetry is implemented and validated in smoke; soak harness now records deltas/floors/peaks, but long-run soak is pending.

### Phase 8 — Release packaging and docs

**Goal:** Produce a handoff-ready release.

Tasks:

- Build artifacts: full flash image and update image if applicable. Status: `scripts/package_release_d1l.py` packages the ESP-IDF flash set, app update image, and padded 8MB full-flash image; GitHub Actions uploads `d1l-release-package`.
- Checksums. Status: release packages include `SHA256SUMS.txt` plus per-file hashes in `manifest.json`.
- Flash instructions. Status: package README, generated `flash_project.ps1`, generated `flash_project.sh`, and `docs/USER_GUIDE_D1L.md` document explicit-port normal flashing.
- Recovery instructions. Status: generated `flash_full_8mb.ps1` requires typed confirmation and `docs/FLASH_RECOVERY_D1L.md` documents backup/erase recovery flow.
- User guide. Status: first `docs/USER_GUIDE_D1L.md` added.
- Developer guide. Status: first `docs/DEVELOPER_GUIDE_D1L.md` added.
- Known limitations. Status: `docs/KNOWN_LIMITATIONS.md` is updated through the Phase 7 soak harness.
- Screenshots/photos. Status: host simulator screenshots are generated locally and by GitHub Actions under `artifacts/ui-sim`; physical screen photos/manual visual review are still pending.
- Final test report. Status: hardware validation notes are current through the short active soak, release package checkpoint, and release-package sanity smoke; final full-release report is pending long soaks and manual UI review.

Acceptance:

- A new user can flash using documented steps and a specified COM port. Status: generated package scripts and user guide are in place; final release dry-run from CI artifact is pending.
- A developer can rebuild from clean checkout. Status: CI builds firmware and packages release artifacts from checkout.
- Final docs match actual behavior. Status: docs are updated through this packaging slice; final audit remains pending.

---

## 10. Serial console requirements

Implement a compact serial command shell. Required commands:

```text
help
version
board
i2c
display test
touch test
backlight <0-100>
radiohw
radio get
radio set preset uscan
radio set freq 910.525
radio set bw 62.5
radio set sf 7
radio set cr 5
mesh status
mesh advert zero
mesh advert flood
mesh send public <text>
mesh send dm <fingerprint> <text>
messages public
messages dm
messages unread
messages read <public|dm|all>
messages clear
messages dm clear
nodes
nodes clear
contacts
contacts add <fingerprint> [alias]
contacts clear
routes
routes detail <seq>
routes clear
packets
packets detail <seq>
packets clear
health
crashlog
wifi scan
wifi off
ble status
reboot
factory-reset-confirm
```

Serial output must be machine-parseable enough for `scripts/smoke_d1l.py`.

---

## 11. Test plan

### 11.1 Host/native tests

Cover:

- Radio profile validation.
- Canada/USA defaults.
- Message store append/load/dedup.
- Contact store.
- Packet ring buffer.
- Route store.
- UI model state transitions.
- Settings persistence serialization.
- List virtualization calculations.
- Theme contrast helper.

### 11.2 Simulator/screenshot tests

Where feasible:

- Run UI with fake mesh events.
- Generate screenshots for every main screen. Status: `tools/ui_simulator.py --out artifacts/ui-sim` renders Home, Messages, Nodes, Packets, Settings, compose/contact/DM/route/packet/mesh-role sheets, lock overlay, and first-boot onboarding.
- Assert required labels and button surfaces are present.
- Assert measured text stays inside its assigned 480x480 layout boxes.
- Assert screen construction does not leak obvious objects.
- Test large node/message lists.

### 11.3 Hardware smoke tests

`scripts/smoke_d1l.py --port <D1L_PORT>` should verify:

- Serial responsive.
- Version command.
- I2C scan.
- Display test command.
- Touch test can be manually confirmed or times out cleanly.
- SX1262 hardware check.
- Radio profile readback.
- Mesh status.
- Health/heap output.

### 11.4 Real mesh validation

Use at least one known-good MeshCore device on Canada/USA settings.

Validate:

- RX live advert.
- TX advert.
- Public message send/receive.
- DM if keys/contact state allow.
- Packet log captures RSSI/SNR.
- UI updates without manual refresh.

---

## 12. Definition of done

The project is done for v1 when:

- D1L boots reliably into MeshCore DeskOS.
- Display/touch/backlight/button/radio work on real hardware.
- Canada/USA defaults are applied on first boot.
- User can send/receive MeshCore messages from the touch UI.
- User can browse nodes, packets, signal, and settings.
- Basic companion mode via USB is working; BLE/Wi-Fi status is either working or clearly documented as limited.
- Build, flash, monitor, smoke, and recovery scripts exist.
- No hardcoded COM port.
- Documentation matches the build.
- Final release artifacts are produced with checksums.
- Known limitations are honest and specific.

---

# 13. Copy/paste Codex goal prompt

Paste the prompt below into GPT-5.5 Codex xhigh. Place this roadmap at `docs/MeshCore_DeskOS_D1L_Codex_Handoff_Roadmap.md` in the target repo/workspace first.

```text
You are GPT-5.5 Codex xhigh acting as the lead firmware engineer for MeshCore DeskOS D1L.

Source of truth: read and follow `docs/MeshCore_DeskOS_D1L_Codex_Handoff_Roadmap.md` before editing code. The goal is to build a production-quality Seeed SenseCAP Indicator D1L desktop GUI firmware for MeshCore, based on the architecture and visual direction of SigurdOS T-Deck, with feature ideas from LimitlezzOS, MeshCoreTerm, and the realtag LVGL scaffold. This is MeshCore-first firmware for Canada/USA use, not a Meshtastic dual-stack project.

Target hardware: Seeed SenseCAP Indicator D1L. It has ESP32-S3 + RP2040, 480x480 capacitive touch display, SX1262 LoRa, Wi-Fi/BLE, USB-C desk power, and D1L-specific display/touch/radio wiring. Do not assume LilyGO T-Deck pins or keyboard/trackball input.

Default radio preset: Canada/USA MeshCore recommended profile: 910.525 MHz, BW 62.5 kHz, SF7, CR5, safe TX power initially 20 dBm unless board/regulatory checks justify another value. The Seeed Indicator SX1262 TCXO setting must default to NONE.

Serial flashing/testing: do not hardcode a COM port. Accept `D1L_PORT` or `--port`. The user will provide the actual COM port. Do not default to COM11 or COM29. If no port is provided, build and run non-hardware tests, then stop before flashing.

Use sub-agents for speed if the environment supports them. Launch these parallel workstreams, then integrate carefully:
1. Source Audit & Licensing Agent: map SigurdOS, LimitlezzOS, MeshCoreTerm, realtag scaffold, MeshCore upstream, and Seeed SDK; produce attribution/license notes.
2. D1L HAL Agent: implement/verify display, touch, backlight, button, IO expander, RP2040 bridge, and SX1262 pin map.
3. MeshCore Agent: wire MeshCore service, Canada/USA presets, identity, advert, RX/TX, message events, packet metadata.
4. UI/UX Agent: build the 480x480 dark mobile-style LVGL shell, home dashboard, messages, nodes, packet log, settings, radio, diagnostics screens.
5. Companion/Connectivity Agent: USB console, BLE companion feasibility, Wi-Fi setup/OTA/local management, memory coexistence policy.
6. Persistence/Diagnostics Agent: NVS/settings, message/contact/route stores, packet ring, crash ring, health monitor.
7. QA/Release Agent: build/flash/monitor/smoke scripts, tests, screenshots, docs, release binaries, checksums.

Implementation rules:
- Work in phases from the roadmap. Do not jump to polish before D1L board bring-up and radio validation.
- Start with a framework decision spike: keep SigurdOS/PlatformIO if D1L ST7701S RGB + FT5x06 + PCA9535/PCA9554 + SX1262 are practical there; otherwise port the app architecture to ESP-IDF v5.1.x using Seeed’s SDK as the HAL base. Document the decision in `docs/D1L_BUILD_DECISION.md`.
- Preserve a clean architecture: HAL, MeshCore service, app model/event bus, UI, companion/comms, persistence, diagnostics, scripts, docs.
- Keep the UI touch-first and desk-readable. Use iPhone-inspired dark spacing/cards/status hierarchy, but no Apple assets or copied proprietary UI.
- MeshCore features required for v1: onboarding, identity, Canada/USA radio preset, advert, public chat, DMs if MeshCore API/key flow allows, heard nodes, contacts, repeaters/room servers, packet log, RSSI/SNR, route/path metadata, radio settings, diagnostics, USB serial control.
- Wi-Fi and BLE must be optional and safe. Wi-Fi defaults off unless user enables setup/OTA. If RAM pressure prevents Wi-Fi and BLE coexistence, enforce one active mode at a time and document it.
- Add hardware smoke commands and `scripts/smoke_d1l.py --port <D1L_PORT>`.
- Add flash/recovery docs and backup/read-flash step where possible.
- Run all available tests after each phase. Commit or summarize phase checkpoints with exact commands and results.
- Be honest about any hardware feature that cannot be validated without the user’s D1L connected.

Deliverables:
- Working source code for the D1L firmware.
- D1L board definition and HAL.
- MeshCore DeskOS 480x480 LVGL UI.
- Build, flash, monitor, smoke, backup/recovery scripts.
- Tests and screenshot/simulator artifacts where feasible.
- Release binaries/checksums if hardware build succeeds.
- Updated README and docs: board bring-up, UI spec, test plan, flash/recovery, known limitations, release checklist.

Start now by reading the roadmap, auditing the repos, creating the branch `feature/meshcore-deskos-d1l`, and producing the framework decision. Then implement Phase 1 hardware bring-up before any deeper UI polish.
```
