# SIGUI / MeshCore DeskOS D1L — Final Production Plan

**Repository:** `n30nex/SIGUI`  
**Target branch:** `feature/meshcore-deskos-d1l`  
**Target hardware:** Seeed SenseCAP Indicator D1L, ESP32-S3 + RP2040 + 480×480 touch display + SX1262 LoRa  
**Goal:** Finish the firmware as a stable, beautiful, touch-first MeshCore desk companion that normal users can operate without serial logs or a phone.

---

## 0. Read this first

This plan supersedes the older roadmap where the product direction conflicts with the updated UX requirements below. The existing repo already has a strong hardware, MeshCore, storage, release, and smoke-test foundation. The remaining production work is mostly:

1. **Crash-proofing the LVGL shell.** Opening `Msg`, `Nodes`, and `Pkts` must never reboot/panic/freeze.
2. **Replacing the dense developer-style UI with a user-first desk companion UI.** Hide low-level data unless the user opens diagnostics.
3. **Making every long page scroll by touch.** No fixed-position data dumps that run off-screen.
4. **Finishing SD-card boot/use behavior.** Detect, validate, prepare, mount, and use SD automatically when safe; never reformat a card that already has the correct DeskOS structure.
5. **Completing Messaging, Nodes, Map, Packets, Settings as real user workflows.** Not just preview rows and status cards.

The Codex team should work in large parallel chunks, but merge through one integration lead. Do not allow agents to independently rewrite the same UI file at the same time.

---

## 1. Current repo observations that matter

### 1.1 Architecture is workable but the UI is monolithic

The project is now ESP-IDF based, using the Seeed BSP and components for display/touch/LVGL/LoRa. `main/CMakeLists.txt` registers a useful separation of app, comms, diagnostics, HAL, mesh, storage, and UI sources, but the actual LVGL shell is still effectively concentrated in `main/ui/ui_phase1.c`.

The CMake source list already includes:

- `main/app/app_model.c`
- `main/comms/connectivity_manager.c`
- `main/diagnostics/health_monitor.c`
- `main/hal/rp2040_bridge.c`
- `main/mesh/*_store.c`
- `main/storage/*_store.c`
- `main/ui/ui_phase1.c`

Production should split `ui_phase1.c` into small screen/component modules instead of continuing to grow the monolith.

### 1.2 Current content root and panels block normal scrolling

The main content root is created at `480×362` and explicitly marked non-scrollable. The generic panel helper also clears `LV_OBJ_FLAG_SCROLLABLE` on every panel. This directly explains the “we can’t scroll up or down on pages” issue.

Production must introduce a proper scrollable page container per screen, with fixed top status and bottom dock outside the scroll area.

### 1.3 Bottom dock is simple but tab rendering is heavy

The dock currently exposes six buttons: `Home`, `Msg`, `Nodes`, `Map`, `Pkts`, `Set`. Tab changes trigger a full `lv_obj_clean(s_content)` and rebuild of the active tab. That is simple, but on hardware it can expose LVGL allocation failure, stale callback user-data, stack pressure, and redraw timing issues.

Even if a previous checklist says bottom dock taps were validated once, the current user report says opening `Msg`, `Nodes`, and `Pkts` crashes the device. Treat the crash report as the production truth until reproduced and fixed on the user’s D1L.

### 1.4 Snapshot and store limits are still “bring-up sized”

Current preview constants are tiny:

- packet preview: 4
- message preview: 4
- DM preview: 5
- node preview: 4
- contact preview: 2
- route preview: 2

Current stores are also small:

- Public messages: 16 rows
- DM messages: 16 rows
- nodes: 64 active RAM rows / 16 NVS fallback rows / 512 SD-history target
- packet log: 128 active RAM rows / 8 NVS fallback rows / 2048 SD-history target

These are okay for Phase 1 validation. They are not enough for a desk companion with scrollable lists, packet terminal, local repeater panel, offline map, and SD-backed retention. Production should keep RAM previews bounded, but allow larger SD-backed retained history.

### 1.5 Public/DM message length is below the updated requirement

`D1L_MESSAGE_TEXT_LEN` is currently 96 including the NUL terminator. The updated requirement is **138 characters max**. Production should define:

```c
#define D1L_MESSAGE_MAX_CHARS 138U
#define D1L_MESSAGE_TEXT_LEN 139U
```

Then use that constant consistently in Public messages, DMs, composer max length, serial commands, storage serialization, SD export, simulator tests, and UI char counters.

### 1.6 SD foundation exists, but production behavior is not finished

The current firmware has RP2040 SD bridge status, format guard, file protocol constants, retained blob-store switching, exports, and map tile canaries. What is still missing for the user’s requested behavior is the complete boot-time SD state machine:

- Detect card on boot.
- Mount/validate card.
- If it already has the correct DeskOS structure, use it and do not format.
- If it has a valid filesystem but is missing the DeskOS structure, create the structure without formatting.
- If it is blank/unformatted/unsupported and allowed by the configured policy, format once, create the structure, then use it.
- If format would risk existing user data, require explicit confirmation and keep NVS fallback active.
- Prove retained stores, exports, map-tile cache, and remount after reboot.

---

## 2. Final product definition

### 2.1 Product identity

**MeshCore DeskOS D1L** is a square-display desk companion. It should feel like a small dark-mode communications console, not a firmware diagnostics dashboard.

The normal user should be able to:

- See unread Public and DM activity from the home screen.
- Read the last 5 Public/DM message previews immediately.
- Tap message tiles to jump to Public or DM threads.
- Compose and send 138-character MeshCore messages from touch.
- Browse nearby companions, repeaters, and room servers.
- Sort nodes by last heard, signal, role, name, and favorites.
- See local repeaters with last heard and signal strength.
- See a local node map after picking the D1L location.
- Download and cache free/offline map tiles once Wi-Fi is configured.
- View live RX/TX packets in a terminal-style diagnostic screen.
- Configure Wi-Fi/BLE/Radio/SD/Display safely from Settings.

### 2.2 What should disappear from the normal UI

Hide these from normal user surfaces unless inside Advanced/Diagnostics:

- Heap/PSRAM/LVGL byte counts.
- Path hash byte counts as a home metric.
- Raw packet hex.
- Boot counters.
- Store write counters.
- Internal backend labels unless the user opens Storage details.
- Developer wording like “NVS”, “protocol pending”, “file canary”, “bridge protocol” on Home.

Keep the data available under:

- `Settings → Diagnostics`
- `Settings → Storage → Advanced`
- `Packets → Detail → Raw`
- serial console

---

## 3. Non-negotiable production acceptance

The firmware is not production-ready until all of these pass on real D1L hardware.

### 3.1 Crash/stability

- Repeatedly tap `Home → Msg → Nodes → Map → Pkts → Set` for at least 100 tab changes without reboot, panic, watchdog, UI freeze, or crashlog entry.
- Open and close every modal sheet repeatedly: compose, keyboard, public history, DM thread, contact detail, route detail, packet detail, storage, radio, Wi-Fi/BLE setup.
- UI task stack watermark remains above the chosen safety threshold after tab abuse.
- LVGL allocation failures are handled without null dereference.
- `crashlog` stays empty after test start clear.

### 3.2 Scrolling

- Every page with content beyond the visible area scrolls by finger.
- Lists show a scrollbar when useful.
- Page scroll areas do not fight the bottom dock or top status bar.
- Key pages retain scroll position where sensible.
- Keyboard sheets remain usable without hiding the send button.

### 3.3 Messaging

- Public channel is the default message screen.
- Bottom compose button opens the on-screen keyboard.
- Send button is visible and reliable.
- 138-character max is enforced with a visible counter.
- Public and DM messages show sender, time/age, direction, status, RSSI/SNR if available, and route/path summary.
- Pressing a message starts a reply compose flow.
- Public/DM unread badges persist across reboot.
- Last 5 relevant previews are shown on Home.

### 3.4 Nodes

- Nodes screen shows companions, repeaters, and room servers.
- Sort modes: last heard, signal, role, name, favorites.
- Filters: all, companions, repeaters, room servers, favorites, reachable/keyed.
- Rows are virtualized or capped so large meshes do not crash.
- Node detail shows last heard, RSSI/SNR, path, role, public key state, actions.

### 3.5 SD-card behavior

- Boot detects SD state without crashing or blocking indefinitely.
- Correct DeskOS card: mount and use; no format.
- Valid filesystem without DeskOS root: create root and manifest; no format.
- Blank/unformatted card: prepare according to production policy, then use.
- Card with unrelated existing data: do not wipe silently; ask for confirmation or stay NVS fallback.
- SD-backed Public/DM/routes/packets/map tiles survive reboot/remount.
- Removing or failing SD falls back cleanly to onboard state.

### 3.6 Map

- First open asks the user to set D1L location.
- User can pan/zoom/select location using touch and drop a pin.
- Next open shows D1L pin plus nearby GPS-capable nodes.
- Map tile cache uses SD when ready.
- Free/offline tile download support exists behind Wi-Fi/user opt-in.
- Tile source is configurable and respects provider limits; sideloaded tile packs must be supported.

### 3.7 Settings

- Settings is grouped, plain-language, and touch-friendly.
- Wi-Fi and BLE logos/status chips are tappable from Home and go to setup.
- Wi-Fi setup can scan/connect/save local network credentials.
- BLE setup shows enabled/disabled, pairing state, and support status.
- Radio settings are clearly labeled as advanced and warn before isolating the user from the mesh.
- Storage setup shows friendly card state and only exposes destructive actions behind confirmation.

---

## 4. Team execution model

Use sub-agents, but keep one integration lead.

### Integration Lead

Owns branch health, merge order, build/test gate, coding conventions, and final release checklist. No agent merges directly into the production branch without the integration lead running host tests and reviewing UI behavior.

### Agent A — P0 Crash, LVGL Safety, and Scroll Foundation

Owns the current crash bugs, scroll infrastructure, LVGL guard rails, tab switching, task stack/memory instrumentation, and simulator/hardware tab-abuse tests.

### Agent B — UI Design System and Shell

Owns the visual system: theme tokens, icons, status bar, dock/app launcher, cards, list rows, modals, buttons, badges, typography, spacing, and high-contrast/night modes.

### Agent C — Messaging UX

Owns Public chat, DMs, composer, keyboard, reply flow, unread state, message detail, path display, 138-char enforcement, and Home message previews.

### Agent D — Nodes, Repeaters, Room Servers, Routes

Owns node data models, sort/filter, local repeater panel, node details, route/path summaries, room server presentation, and touch actions.

### Agent E — SD, Map, and Offline Tiles

Owns RP2040 SD boot state machine, DeskOS card structure, format policy, retained SD stores, map location setup, tile cache, tile sideload/download, and map node display.

### Agent F — Packets and Diagnostics

Owns the terminal-style packet page, color-coded RX/TX feed, packet detail, raw/developer mode, health diagnostics, crashlog UX, export/debug flow, and serial parity.

### Agent G — Wi-Fi, BLE, OTA, Connectivity Settings

Owns Wi-Fi scan/connect/save, time sync, BLE companion runtime feasibility, OTA/local management, coexistence policy, and Home status chip navigation.

### Agent H — QA, Release, Docs

Owns tests, hardware scripts, screenshots/photos, release package, checksums, docs, known limitations, final validation report, and Codex handoff notes.

---

## 5. Chunk 1 — P0 crash fix and scrollable screen framework

This is the first merge gate. Do not polish the UI until this passes.

### 5.1 Reproduce and instrument

Add or extend hardware scripts:

```powershell
python .\scripts\ui_tab_abuse_d1l.py --port $env:D1L_PORT --cycles 100 --tabs home,msg,nodes,map,pkts,set --clear-crashlog
python .\scripts\ui_modal_abuse_d1l.py --port $env:D1L_PORT --cycles 25 --surfaces compose,history,dm,contact,route,packet,radio,storage
python .\scripts\scroll_probe_d1l.py --port $env:D1L_PORT --screens messages,nodes,packets,settings,map
```

If exact touch automation is not available, add a temporary serial-only UI test command family:

```text
ui status
ui tab home|messages|nodes|map|packets|settings
ui open compose|history|packet-detail|radio|storage
ui close all
ui scroll <screen> <delta>
ui abuse tabs <count>
```

This does not replace manual touch proof, but it gives Codex repeatable crash coverage.

### 5.2 LVGL safety guard rails

Implement helpers and enforce them everywhere:

```c
lv_obj_t *d1l_ui_obj_checked(lv_obj_t *obj, const char *kind, const char *screen);
void d1l_ui_safe_delete(lv_obj_t **obj);
bool d1l_ui_can_render(void);
```

Rules:

- Check every `lv_label_create`, `lv_btn_create`, `lv_obj_create`, `lv_textarea_create`, `lv_keyboard_create`, and QR object creation.
- If allocation fails, show a small friendly error row instead of dereferencing null.
- Record allocation failures into diagnostics.
- Never store callback user-data pointing to transient stack memory.
- Avoid rebuilding a screen from inside its own event callback. Continue deferring tab switch, but add a tab switch lock and ensure no stale sheet callback fires during `lv_obj_clean`.

### 5.3 Split the UI monolith

Create this layout:

```text
main/ui/
  ui_shell.c/.h
  ui_theme.c/.h
  ui_icons.c/.h
  ui_components.c/.h
  ui_screen.h
  screens/
    home_screen.c/.h
    messages_screen.c/.h
    nodes_screen.c/.h
    map_screen.c/.h
    packets_screen.c/.h
    settings_screen.c/.h
    diagnostics_screen.c/.h
  sheets/
    compose_sheet.c/.h
    node_detail_sheet.c/.h
    message_detail_sheet.c/.h
    packet_detail_sheet.c/.h
    storage_sheet.c/.h
    wifi_sheet.c/.h
    ble_sheet.c/.h
    radio_sheet.c/.h
```

Keep the first refactor mechanical. Do not change product behavior and crash behavior in the same patch unless necessary.

### 5.4 New screen lifecycle

Introduce:

```c
typedef struct {
    const char *id;
    esp_err_t (*create)(lv_obj_t *page);
    esp_err_t (*enter)(const d1l_app_snapshot_t *snapshot);
    void (*leave)(void);
    void (*refresh)(const d1l_app_snapshot_t *snapshot);
    void (*destroy)(void);
} d1l_ui_screen_t;
```

Each screen owns its page object. The shell switches screens by hiding/showing or destroying/recreating according to memory budget. No screen should directly call `lv_obj_clean(s_content)` from arbitrary places.

### 5.5 Scrollable root for every main screen

Use one fixed shell:

```text
480x480
┌────────────────────────┐
│ Top status bar 48 px   │
├────────────────────────┤
│ Scrollable page 370 px │
├────────────────────────┤
│ Bottom dock 62 px      │
└────────────────────────┘
```

Every page root:

```c
lv_obj_t *page = lv_obj_create(parent);
lv_obj_set_size(page, 480, 370);
lv_obj_set_pos(page, 0, 48);
lv_obj_set_scroll_dir(page, LV_DIR_VER);
lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
lv_obj_set_style_pad_all(page, 12, 0);
```

Avoid fixed absolute y layouts for list-heavy pages. Use LVGL flex/column layouts or a simple row builder with content height.

### 5.6 Acceptance for Chunk 1

- Bottom tabs no longer crash when tapped manually and through serial/test hooks.
- Messages, Nodes, Packets, Settings, and Map scroll by finger.
- `smoke_d1l.py` passes.
- `ui_tab_abuse_d1l.py` passes.
- `tools/ui_simulator.py --scenario large-mesh` passes.
- Crashlog remains clean after tests.
- UI task stack and LVGL memory stay above thresholds.

---

## 6. Chunk 2 — New design system and Home screen

### 6.1 Visual style

Create a clean dark companion UI:

- Deep blue-black background.
- Rounded cards with subtle borders.
- Large readable labels.
- Simple original line icons.
- Cyan/teal online state.
- Amber warning state.
- Red only for errors/destructive actions.
- 44×44 minimum touch target; prefer 56×56 for main actions.
- No Apple/SF/proprietary assets.

### 6.2 Icon set

Implement an internal icon enum with LVGL label glyph fallback:

```c
typedef enum {
    D1L_ICON_HOME,
    D1L_ICON_CHAT,
    D1L_ICON_DM,
    D1L_ICON_NODES,
    D1L_ICON_REPEATER,
    D1L_ICON_ROOM,
    D1L_ICON_MAP,
    D1L_ICON_PACKET,
    D1L_ICON_WIFI,
    D1L_ICON_BLE,
    D1L_ICON_SD,
    D1L_ICON_RADIO,
    D1L_ICON_SETTINGS,
    D1L_ICON_WARNING,
} d1l_ui_icon_t;
```

Use simple original vector/line drawings or Unicode-safe text fallback. Do not block the firmware on custom fonts.

### 6.3 Top status bar

Replace the current developer-heavy top bar with:

```text
[time]   MeshCore ●   Wi-Fi icon   BLE icon   SD icon   battery/power optional
```

Behavior:

- Time shown if RTC/time-sync is available.
- If no time source, show `--:--` or `Time not set` in muted style.
- Touch Wi-Fi icon → Settings Wi-Fi sheet.
- Touch BLE icon → Settings BLE sheet.
- Touch SD icon → Storage sheet.
- Touch MeshCore status → Radio/mesh status sheet.

### 6.4 Home screen layout

Home should be glanceable, not diagnostic.

Recommended layout:

```text
Top status bar

[Unread Public card]     [Unread DMs card]
  icon + count             icon + count
  tap -> Public            tap -> DM list

[Last 5 messages]
  sender • age • preview • badge
  tap row -> Public or DM thread

[Local repeaters]
  repeater name • last heard • RSSI/SNR
  tap -> filtered Nodes screen

[Quick tiles]
  Public  Nodes  Map  Packets  Settings
```

Home data model additions:

```c
#define D1L_HOME_MESSAGE_PREVIEW 5U
#define D1L_HOME_REPEATER_PREVIEW 3U

typedef struct {
    bool is_dm;
    bool unread;
    char sender[24];
    char target_fingerprint[17];
    char text[139];
    uint32_t age_sec;
    int rssi_dbm;
    int snr_tenths;
} d1l_home_message_preview_t;
```

### 6.5 Acceptance for Chunk 2

- Home shows unread Public and DM icons/cards.
- Home shows last 5 Public/DM previews.
- Home shows time and Wi-Fi/BLE/SD status chips.
- Touching Wi-Fi/BLE/SD chips opens the right setup/status area.
- Home shows local repeaters with last heard and signal.
- All Home actions are reachable by touch.
- No developer counters on Home unless Advanced mode is enabled.

---

## 7. Chunk 3 — Messaging production workflow

### 7.1 Data model changes

Set message max to 138 user-visible characters:

```c
#define D1L_MESSAGE_MAX_CHARS 138U
#define D1L_MESSAGE_TEXT_LEN (D1L_MESSAGE_MAX_CHARS + 1U)
```

Add route/path display fields if not already complete:

```c
typedef struct {
    uint8_t hop_count;
    char path_summary[48];        // e.g. "direct", "via KWC-G", "2 hops"
    char first_hop[17];
    char last_hop[17];
    bool route_known;
} d1l_message_path_summary_t;
```

Do not require full raw packet details in the normal message row. Put deeper route/hash data in a message detail sheet.

### 7.2 Public channel screen

Default Messages tab opens Public.

Layout:

```text
Header: Public        [DMs] [Search optional]
Subheader: unread count / last heard
Scrollable message list
Bottom compose bar: [Compose]
```

Message row:

```text
sender / You
preview text
age • RSSI/SNR • path summary • sent/received/acked/new
```

Tap behavior:

- Tap received Public message → open compose sheet with reply context.
- Tap sent message → open detail sheet.
- Long press, if implemented → detail/actions sheet. Must also have visible action alternative.

### 7.3 Compose sheet

Requirements:

- Opens keyboard.
- Clear Send button stays visible.
- Shows `0/138` counter.
- Disables Send at 0 chars or >138 chars.
- Shows Public or DM recipient in title.
- Public send uses MeshCore Public channel.
- DM send uses retained public key/route backend.
- On send, close keyboard only after queue success or show friendly error.

Current implementation note: Public/DM text is capped by `D1L_MESSAGE_MAX_CHARS = 138` and the touch compose sheet now shows a live `<used>/138` counter that resets for new Public or DM compose sessions.

### 7.4 DM list and thread

Messages screen can have a segmented control:

```text
[Public] [DMs]
```

DMs screen:

- Conversation list sorted by unread first, then last message time.
- Each row: alias/fingerprint, unread badge, last text, age, signal/path.
- Thread view: scrollable history, bottom Reply button, 138-char composer.

### 7.5 Message detail sheet

Show:

- Sender / destination.
- Direction and status.
- Text.
- Time or uptime age.
- RSSI/SNR.
- Hop count/path summary.
- ACK hash if relevant.
- Packet sequence/hash only under Advanced.

### 7.6 Acceptance for Chunk 3

- Public opens by default.
- Compose/send works from touch.
- Reply compose opens when pressing a message.
- Public and DM enforce 138 chars.
- Sender and path are visible for messages.
- Unread badges update Home and Messages.
- Message list scrolls.
- Public/DM history survives reboot with SD when ready and NVS fallback when not.
- Serial `messages public`, `messages dm`, `messages unread`, and touch UI agree.

---

## 8. Chunk 4 — Nodes, repeaters, room servers, routes

### 8.1 Data model expansion

Current node capacity is bring-up sized. Production needs:

- Larger RAM index if safe.
- SD-backed retained node evidence if available.
- Last heard age and timestamp.
- Role classification: companion, repeater, room server, sensor/unknown.
- Sort key fields.
- Favorite flag.
- Reachability/keyed state.

Recommended constants:

```c
#define D1L_NODE_RAM_ACTIVE_CAPACITY 64U
#define D1L_NODE_SD_HISTORY_CAPACITY 512U
#define D1L_REPEATER_PREVIEW_CAPACITY 8U
#define D1L_ROOM_SERVER_PREVIEW_CAPACITY 8U
```

Tune after heap/PSRAM testing.

### 8.2 Nodes screen layout

```text
Header: Nodes          [Sort] [Filter]
Filter chips: All / Companions / Repeaters / Rooms / Favorites
Sort chips: Last heard / Signal / Name / Role
Scrollable virtual list
```

Row:

```text
[role icon] Name or fingerprint
role badge • last heard • RSSI/SNR • hops/path • key badge
```

Current implementation note: Nodes now renders compact ASCII role badges (`CMP`, `RPT`, `ROOM`, `SNS`, `NODE`) from `d1l_node_view_t` rows, preserving the existing bounded preview cap while avoiding new font/icon assets.

### 8.3 Node detail

Actions:

- DM if keyed.
- Favorite/unfavorite.
- Trace/path evidence.
- Request telemetry if supported.
- Export contact if public key retained.
- Advanced details.

Normal details:

- Role.
- Last heard.
- Signal.
- Path/hops.
- Public key/keyed state.
- Heard count.

Current implementation note: generic heard-node detail is read-only and opens from heard-node rows. It shows role, fingerprint, public-key state, favorite/mute/reachable flags, RSSI/SNR, path hash/hops, advert timestamp, first/last-heard timestamps, and heard count. DM/edit/export actions remain on promoted contacts.

### 8.4 Local repeaters panel

Use same evidence as nodes/routes:

- Repeater candidates.
- Last heard.
- RSSI/SNR.
- Heard count.
- Path summary.

Home shows top 3. Nodes screen can filter all repeaters.

### 8.5 Room servers

Room servers need a clear label and separate filter/list. The user should not have to infer from raw role codes.

### 8.6 Acceptance for Chunk 4

- Nodes screen scrolls and does not crash with large simulated and real data.
- Sort/filter works by last heard, signal, role, name, favorites.
- Companions, repeaters, and room servers are visually distinct.
- Node details open and close repeatedly without crash.
- Home repeater panel is backed by the same data.
- Serial `nodes`, `repeaters`, `roomservers`, `routes trace` agree with UI summaries.

---

## 9. Chunk 5 — SD-card boot/use and retained storage

### 9.1 Production SD card structure

Root on card:

```text
/deskos/
  manifest.json
  stores/
    messages/
      public.bin
      dm_threads.bin
    nodes/
      active_nodes.bin
    contacts/
      contacts.bin optional mirror
    routes/
      routes.bin
    packet_log/
      ring.bin
  map/
    manifest.json
    tiles/
      z{z}/x{x}/y{y}.tile
    packs/
      *.pack or future packed format
  exports/
    diagnostics/
    data/
  tmp/
  logs/
```

`manifest.json`:

```json
{
  "name": "MeshCore DeskOS D1L SD",
  "schema": 1,
  "created_by": "MeshCore DeskOS D1L",
  "device": "seeed-indicator-d1l",
  "stores": ["messages", "dm", "nodes", "routes", "packets", "map_tiles"]
}
```

### 9.2 Boot state machine

Add `d1l_storage_boot_prepare()`.

Pseudo-flow:

```text
boot
  init RP2040 bridge
  ping bridge
  probe SD with short timeout
  if no card:
      use NVS fallback, show SD absent
  if card ready + /deskos/manifest valid:
      mount + use SD stores
  if valid FS but missing /deskos:
      create /deskos structure + manifest, run write canary, use SD stores
  if blank/unformatted:
      if auto_prepare_new_cards enabled:
          format with internal confirmation, create structure, run canary, use SD stores
      else:
          show setup required, use NVS fallback
  if unknown FS or existing unrelated data:
      do not wipe silently; show confirmation UI, use NVS fallback
  if any timeout/error:
      use NVS fallback, show friendly Storage status
```

### 9.3 Reconciling “format cards on boot” with safety

Production setting:

```c
settings.sd.auto_prepare_new_cards = true;
settings.sd.auto_format_unformatted = true;
settings.sd.require_confirmation_for_existing_data = true;
```

This satisfies the product goal while avoiding accidental data loss:

- Correct DeskOS card: never format.
- Fresh blank/unformatted card: format on boot and use.
- Existing FAT/exFAT card without DeskOS root: create root, no format.
- Card with unrelated files or ambiguous state: ask before destructive format.

### 9.4 Retained store migration

Move from small NVS-only behavior to SD-first when ready:

- Public messages: larger retained history.
- DM threads: larger retained history.
- Routes: larger retained route/path evidence.
- Packet log: terminal scrollback on SD.
- Nodes: active node list and last-heard history.
- Exports: diagnostics and user-data exports.
- Map tiles: tile cache.

NVS remains for:

- Identity.
- Boot-critical settings.
- Wi-Fi credentials unless a secure storage plan says otherwise.
- Last known safe storage state.
- Minimal recent cache if SD unavailable.

### 9.5 Acceptance for Chunk 5

Run these scenarios on hardware:

1. No card → boot OK, NVS fallback, no crash.
2. Correct DeskOS card → boot uses SD, no format.
3. Valid blank FAT/exFAT card without `/deskos` → creates structure, no format.
4. Unformatted card → formats/prepares according to setting, creates structure, uses SD.
5. Existing unrelated files → does not silently wipe.
6. SD removed after boot → UI reports fallback/no crash.
7. Reboot after messages/routes/packets/map tile canary → data survives.
8. RP2040 bridge unavailable → boot OK with friendly warning.

---

## 10. Chunk 6 — Map and free/offline tile support

### 10.1 First-open location setup

Map first open flow:

```text
Map needs your D1L location
[Use approximate manual picker]
[Enter lat/lon]
[Skip for now]
```

Manual picker:

- Starts at a sensible default if no GPS/time/network.
- User pans with finger.
- Zoom + / - buttons.
- Crosshair in center.
- “Drop D1L Pin” button.
- Save to settings and SD manifest summary.

### 10.2 Map view

After location set:

- D1L pin centered by default.
- Nearby GPS-capable nodes displayed as simple pins.
- Node pins show role icon and signal ring.
- Tap node pin → compact node detail.
- Show tile cache state and offline/online indicator.

### 10.3 Tile model

Use a provider abstraction:

```c
typedef struct {
    char id[24];
    char name[40];
    char url_template[128];
    char attribution[96];
    uint8_t min_zoom;
    uint8_t max_zoom;
    bool bulk_download_allowed;
    bool enabled;
} d1l_map_tile_source_t;
```

Do not hardcode bulk downloads against public tile servers. Support:

- User-configured free tile URL template.
- Sideloaded tile folder under `/deskos/map/tiles/`.
- Optional packed tile format later.
- Small-radius download with explicit tile count/space estimate and user confirmation.

### 10.4 Tile download UI

```text
Map → Tiles
  Source: <name>
  Area: around D1L pin
  Radius: 1 / 5 / 10 / 25 km
  Zooms: 8-13 default
  Estimated tiles: N
  Estimated storage: N MB
  [Download]
  [Pause]
  [Clear cache]
```

Requirements:

- Wi-Fi must be connected.
- SD must be ready.
- User must opt in.
- Downloads must be resumable.
- Never block UI task during download.
- Save progress and errors.

### 10.5 Acceptance for Chunk 6

- First open location flow works by touch.
- Saved D1L pin persists across reboot.
- Map opens to saved location next time.
- GPS-capable local nodes appear when data exists.
- Tile cache reads from SD.
- Sideloaded tiles render without Wi-Fi.
- Downloaded tiles survive reboot.
- Failed Wi-Fi/tile download does not freeze the UI.

---

## 11. Chunk 7 — Packets terminal and diagnostics

### 11.1 Packet page UX

Packets should feel like a live terminal/log viewer, not metric cards.

Layout:

```text
Header: Packets       [RX] [TX] [All] [Pause]
Terminal list:
  12:41:03 RX PUBLIC  -92dBm  7.5dB  via 2 hops  from Krabs
  12:41:06 TX DM      queued  direct  to YKF Corebot
  12:41:08 RX ACK     acked   hash 1234
Bottom: packet count / dropped / filter
```

Color coding:

- RX: cyan/green.
- TX: purple/blue.
- Decode/decrypt fail: amber.
- Error/drop: red.
- Raw hex: muted gray and Advanced only.

### 11.2 Data model

Increase RAM ring if memory allows and SD ring when available:

```c
#define D1L_PACKET_LOG_RAM_CAPACITY 128U
#define D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY 8U
#define D1L_PACKET_LOG_SD_CAPACITY 2048U
#define D1L_PACKET_LOG_SD_FLUSH_DIRTY_THRESHOLD 16U
#define D1L_PACKET_LOG_SD_FLUSH_INTERVAL_MS 5000U
```

Keep a bounded visible virtual list. Never render thousands of LVGL rows at once. NVS remains a compact fallback; SD primary packet history is written through coalesced flushes instead of a full SD rewrite on every RF packet.

### 11.3 Packet detail

Normal detail:

- Direction.
- Type/kind.
- Source/destination if known.
- RSSI/SNR.
- Path/hops.
- Payload length.
- Time/age.

Advanced detail:

- Raw hex.
- Internal sequence.
- Hash bytes.
- Decoder notes.
- Store backend.

### 11.4 Acceptance for Chunk 7

- Packets page opens without crash.
- Live packets append without full-screen flicker.
- Pause/resume works.
- Filters work.
- Packet detail opens/closes repeatedly.
- Color coding is visible in low brightness.
- SD scrollback works when SD ready.
- Serial `packets`, `packets detail`, `packets search` match UI.

---

## 12. Chunk 8 — Settings, Wi-Fi, BLE, OTA, and friendly setup

### 12.1 Settings organization

Settings top page:

```text
Wireless
  Wi-Fi: Off / Connected / Setup needed
  BLE: Off / Pairing / Connected / Unsupported
MeshCore
  Radio preset, identity, advert
Storage
  SD ready / setup / fallback
Display
  Brightness, night mode, high contrast, timeout
Diagnostics
  Health, crashlog, packet export, serial info
About
  Version, build, attribution
```

### 12.2 Wi-Fi setup

Requirements:

- Wi-Fi status chip from Home opens Wi-Fi setup.
- Scan networks.
- Select SSID.
- Keyboard password entry.
- Save credentials.
- Connect/disconnect.
- Time sync when online.
- Use Wi-Fi for map tiles and OTA only after user opt-in.

### 12.3 BLE setup

Requirements:

- BLE status chip from Home opens BLE setup.
- Show build/runtime support state.
- Enable/disable.
- Pairing/PIN if implemented.
- If BLE remains unsupported, say that plainly in UI and docs.

### 12.4 OTA/local management

Optional for v1 production, but if included:

- Wi-Fi required.
- User-confirmed only.
- No cloud dependency.
- Clear progress and recovery warning.

### 12.5 Acceptance for Chunk 8

- Settings is usable without serial.
- Wi-Fi and BLE status chips navigate correctly.
- Wi-Fi credentials persist and can be cleared.
- Time appears on Home after sync.
- BLE state is honest.
- Radio changes are staged and warned.
- SD destructive actions are guarded.

---

## 13. QA and release gate

### 13.1 Host checks

Run every merge:

```powershell
python -m pytest -q
python .\tools\ui_simulator.py --out artifacts\ui-sim
python .\tools\ui_simulator.py --scenario large-mesh --out artifacts\ui-sim-large
python .\tools\ui_simulator.py --scenario storage-states --out artifacts\ui-sim-storage
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --active-public-text test
```

Add simulator assertions for:

- Every main page has a scrollable root.
- Every touch target is >=44×44.
- Required UI surfaces exist.
- No text overflow in common screen states.
- Home has unread Public, unread DM, last 5 previews, Wi-Fi, BLE, time, repeaters.
- Messages default to Public and has compose.
- Nodes has sort/filter.
- Packets has terminal rows.
- Map has location setup and tile state.
- Storage states show safe copy.

### 13.2 Hardware smoke

Run on the user-supplied D1L port only:

```powershell
$env:D1L_PORT = "COMx"
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT
python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
python .\scripts\ui_tab_abuse_d1l.py --port $env:D1L_PORT --cycles 100
python .\scripts\scroll_probe_d1l.py --port $env:D1L_PORT --screens messages,nodes,packets,settings,map
```

### 13.3 RF proof

With a known-good MeshCore node/bot:

- The other local MeshCore bot may be used as the controlled DM RF peer for production validation.
- Advert RX.
- Advert TX.
- Public message TX/RX.
- DM outbound proof.
- DM inbound proof.
- ACK/PATH proof.
- Direct-route proof.
- Packet log captures each event.
- UI updates without manual refresh.

Current evidence: `artifacts/hardware/com12/dm_probe_b841621c.json` passed on
COM12 against the local COM11 Meshcorebot with `send_ok=true`,
`messages_dm_has_token=true`, `packets_search_has_token=true`,
`route_trace_has_target=true`, `meshbot_rx_contact_delta=true`, and
`no_public_commands=true`. Treat outbound DM-to-peer proof as closed for this
checkpoint. Controlled inbound DM, ACK/PATH, direct-route RF proof, and manual
touch DM workflow review remain open.

### 13.4 SD proof

Run scenario matrix from Chunk 5 and archive JSON artifacts.

Required scripts:

```powershell
python .\scripts\rp2040_sd_bridge_preflight_d1l.py --port $env:D1L_PORT --artifact-dir artifacts\github\<run-id>\rp2040-sd-bridge-firmware
python .\scripts\sd_boot_prepare_acceptance_d1l.py --port $env:D1L_PORT --scenario correct-structure
python .\scripts\sd_boot_prepare_acceptance_d1l.py --port $env:D1L_PORT --scenario missing-structure
python .\scripts\sd_boot_prepare_acceptance_d1l.py --port $env:D1L_PORT --scenario unformatted --allow-format-confirm
python .\scripts\sd_retained_history_acceptance_d1l.py --port $env:D1L_PORT --token prod
python .\scripts\sd_map_tile_canary_d1l.py --port $env:D1L_PORT --token prod
python .\scripts\sd_reboot_remount_acceptance_d1l.py --port $env:D1L_PORT --token prod
python .\scripts\sd_data_export_d1l.py --port $env:D1L_PORT --token prod
```

The operator has allowed formatting the SD card inserted in the D1L for production validation. Use only the guarded unformatted-card path above and never silently wipe a correct DeskOS card or unrelated existing-data card.

Current evidence: Actions run `28550779389` for commit `540319d` rebuilt the
ESP32 release package and RP2040 SD bridge UF2. The downloaded release package,
firmware, and RP2040 checksum manifests verified, and the verified ESP32 package
flashed to COM12 passed current-commit smoke in
`artifacts/hardware/com12/smoke_540319d.json`. The RP2040 UF2 checksum is
`032FF80A0F94613BB18742E08CB97AA548BFF81BD627FF882C3AFACAF15F5C01`, but
`artifacts/hardware/com12/rp2040_uf2_volumes_540319d_after_esp32_flash.json`
found no mounted UF2 bootloader volume, so the new RP2040 bridge was not copied.
The preflight
`artifacts/hardware/com12/rp2040_preflight_540319d_after_esp32_flash.json` proves the
RP2040 UART, ping, protocol, and diag paths respond, and the inserted card
reaches `sd.state="setup_required"` with NVS fallback. The guarded
operator-approved format attempt in
`artifacts/hardware/com12/sd_boot_prepare_unformatted_540319d.json` remained
safe (`public_rf_tx=false`) but timed out before a ready file-operation gate, so
the firmware and host runner now allow a longer format window before the next
Actions-built flash/retest. Full SD auto-prepare, retained-history, export,
map-tile, and reboot/remount proof remain open until guarded format and file
canaries pass on the device SD card.

### 13.5 Soak

Required before production tag:

```powershell
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 43200 --sample-interval-sec 300 --sample-storage --allow-sd-unavailable
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 3600 --sample-interval-sec 60 --active-public-text test --active-interval-sec 120 --require-rx-delta --min-tx-delta 10
```

Pass criteria:

- No crashlog entries after start clear.
- No watchdog reset.
- No UI freeze.
- Heap/PSRAM/LVGL memory does not trend downward beyond threshold.
- Storage state stable.
- Packet/message counters advance in active test.

### 13.6 Release artifacts

Final package must include:

- ESP32 normal flash set.
- ESP32 app update image.
- Full 8MB recovery image behind typed confirmation.
- RP2040 SD bridge UF2.
- Manifest JSON.
- SHA256SUMS.
- Flash scripts requiring explicit port.
- User guide.
- Developer guide.
- Known limitations.
- Final validation report.
- Physical screen photos.
- Simulator screenshots.

Final gate audit:

```powershell
python .\scripts\release_gate_audit_d1l.py --github-run-id <run-id> --commit <commit-sha> --d1l-port <D1L_PORT> --meshbot-port <BOT_PORT> --hardware-dir artifacts\hardware\<d1l-port-folder> --soak-dir artifacts\soak --out artifacts\release-gate\release-gate-audit-<commit>.json --fail-on-open-p0
```

The latest local audit for `540319d` reports `ready_for_public_release=false`
with four P0 gates still open after current-commit COM12 smoke passed: SD
acceptance matrix, 12-hour idle/listening soak, manual physical UI/photos, and
full inbound/ACK/PATH/direct-route RF proof. Any later commit must be rebuilt by
GitHub Actions, flashed to COM12, and smoked before it can become the final
release commit.

---

## 14. Concrete implementation checklist

### P0 fixes

- [x] Add `ui_tab_abuse_d1l.py`.
- [x] Add `scroll_probe_d1l.py` or serial UI test commands.
- [x] Split `ui_phase1.c` or establish wrappers before deeper feature work.
- [x] Increase/measure UI task stack.
- [x] Add allocation guards to all LVGL object creation.
- [x] Replace fixed non-scrollable `s_content` with per-screen scroll roots.
- [x] Verify `Msg`, `Nodes`, `Pkts` no longer crash.

2026-07-01 COM12 evidence after the Actions-built `86e5e0e` package:
`artifacts/hardware/com12/smoke_86e5e0e.json` passed with board/UI ready,
`artifacts/hardware/com12/ui_tab_abuse_86e5e0e.json` passed 100 cycles across
`home,messages,nodes,map,packets,settings` with `failure_count=0` and no
crashlog entries, and `artifacts/hardware/com12/scroll_probe_86e5e0e.json`
passed `messages,nodes,packets,settings,map` with no failures or crashlog
entries.

### Home

- [x] Add time state to app snapshot.
- [x] Add Wi-Fi/BLE/SD status chips.
- [x] Add tappable chips routing to setup sheets.
- [x] Add unread Public/DM cards.
- [x] Add last 5 message previews.
- [x] Add local repeater panel.
- [x] Remove developer metrics from normal Home.

### Messages

- [x] Change max message length to 138 chars.
- [x] Update Public/DM stores and serialization.
- [x] Build default Public channel screen.
- [x] Build compose bar/sheet with keyboard and visible Send.
- [x] Add char counter.
- [x] Add reply-from-message tap behavior.
- [x] Add message detail with sender/path.
- [x] Add DM conversation list and thread.

### Nodes

- [x] Expand node active capacity safely.
- [x] Add sort/filter state.
- [x] Add role icons/badges.
- [x] Add repeaters/room server filters.
- [x] Add node detail sheet.
- [x] Add Home repeater data source.

### SD

- [x] Implement `d1l_storage_boot_prepare()` with bounded async-mount polling before retained stores initialize.
- [x] Add `/deskos/manifest.json` schema.
- [x] Create structure without format when FS is valid.
- [ ] Auto-prepare unformatted/new cards according to production policy.
- [x] Never format correct DeskOS cards.
- [x] Require confirmation for ambiguous/existing-data formats.
- [x] Add reboot/remount acceptance script.

Current blocker: `artifacts/hardware/com12/sd_boot_prepare_unformatted_fc08f59.json`
proved the guarded unformatted-card path stays safe and no longer wedges the
ESP32 (`classification=format_confirmed_not_ready`, `format_allowed=true`,
`public_rf_tx=false`, `formats_sd=false`, health ready), but the older RP2040
bridge timed out before reporting a concrete format result. Commit `b841621`
hardened RP2040 format replies, and Actions run `28549761003` for commit
`68350bf` rebuilt a verified RP2040 UF2 with SHA256
`032FF80A0F94613BB18742E08CB97AA548BFF81BD627FF882C3AFACAF15F5C01`; however,
`artifacts/hardware/com12/rp2040_uf2_volumes_68350bf_after_reset.json` found
`candidate_volumes=[]` even after a safe `rp2040 reset`, so that bridge firmware
has not yet been copied to the RP2040.
`artifacts/hardware/com12/rp2040_preflight_68350bf_after_reset.json` shows the
current COM12 bridge still responds to ping/protocol/diag and detects the
inserted card as `setup_required`, but `ready_for_sd_acceptance=false`.
Full SD auto-prepare, retained-history, export, map-tile, and reboot/remount
proof remain open until the RP2040 UF2 is flashed and the SD matrix is rerun.

### Map

- [x] First-open location picker.
- [x] D1L pin persistence.
- [ ] Nearby GPS node pins.
- [ ] Tile cache renderer from SD.
- [ ] Free/user-configurable tile source.
- [ ] Sideloaded tiles.
- [ ] Wi-Fi tile download with explicit opt-in.

### Packets

- [x] Replace packet metric-card page with terminal-style feed.
- [x] Add color-coded RX/TX/fail/error rows.
- [x] Add pause/resume.
- [x] Add filter/search.
- [x] Add packet detail normal/advanced split.
- [x] Increase packet backing capacity and add split SD/NVS flush foundation.
- [ ] Prove full SD packet scrollback on hardware after Actions build.

### Settings

- [x] Plain-language settings groups.
- [ ] Wi-Fi scan/connect/save/clear.
- [ ] BLE support state/enable flow.
- [ ] Time sync.
- [ ] Display settings.
- [x] Friendly storage setup.
- [x] Advanced diagnostics.

---

## 15. Copy/paste Codex kickoff prompt

```text
You are GPT-5.5 Codex xhigh acting as the lead implementation team for n30nex/SIGUI on branch feature/meshcore-deskos-d1l.

Read these first:
- README.md
- docs/ROADMAP.md
- docs/KNOWN_LIMITATIONS.md
- docs/RELEASE_CHECKLIST.md
- docs/USER_GUIDE_D1L.md
- docs/DEVELOPER_GUIDE_D1L.md
- this final production handoff plan

Mission: finish MeshCore DeskOS D1L as a production-quality Seeed SenseCAP Indicator D1L touch-first MeshCore desk companion. The user reports major current issues: opening bottom tabs Msg/Nodes/Pkts can crash the device, pages do not scroll, the UI exposes too much developer data, SD card support must boot-detect/prepare/use cards, and the product UI must be overhauled with friendly icons and workflows.

Hard requirements:
1. Fix bottom-tab crashes first. Msg, Nodes, Map, Pkts, Settings must open repeatedly without panic/reboot/freeze.
2. Make all long pages scroll by touch. Do not keep fixed non-scrollable data dumps.
3. Replace the current dense developer UI with a clean dark desk companion UI using original icons, large touch targets, readable cards, and user-friendly language.
4. Home must show time, Wi-Fi status, BLE status, SD status, unread Public/DM icons, last 5 message previews, and local repeaters with last heard/signal. Tapping Wi-Fi/BLE icons must open setup.
5. Messages must default to Public, have bottom Compose, on-screen keyboard, visible Send, 138-character max with counter, sender/path metadata, and tap-to-reply.
6. Nodes must list companions, repeaters, and room servers with sorting by last heard/signal/name/role and filtering by role/favorites.
7. Map must first ask the user to choose/drop the D1L location by touch, persist it, then show nearby GPS nodes. Add SD-backed free/offline tile cache and Wi-Fi/user-confirmed downloads or sideloaded tiles.
8. Packets must become a terminal-style live RX/TX feed with color-coded rows, pause/filter/search, and details.
9. Settings must be friendly and grouped: Wi-Fi, BLE, MeshCore radio, Storage, Display, Diagnostics, About.
10. SD behavior: on boot, detect card, validate /deskos structure, use it if correct, create structure without formatting if filesystem is valid, auto-prepare clearly blank/unformatted cards according to production policy, and never format an already-correct DeskOS card. Ambiguous/existing-data destructive format must require confirmation and NVS fallback must remain safe.
11. Preserve Canada/USA MeshCore defaults: 910.525 MHz, BW62.5, SF7, CR5, 20 dBm, TCXO NONE unless hardware/regulatory checks justify changes.
12. Keep flash/testing scripts explicit-port only via D1L_PORT or --port. Do not hardcode COM ports.

Use sub-agents:
- A: P0 crash/LVGL safety/scroll foundation
- B: UI design system/icons/shell
- C: Messaging UX
- D: Nodes/repeaters/room servers/routes
- E: SD/map/offline tiles
- F: Packets/diagnostics
- G: Wi-Fi/BLE/OTA/settings
- H: QA/release/docs

Merge order:
1. Crash + scroll framework must land first.
2. UI shell/design system lands next.
3. Home + Messaging + Nodes can then integrate in parallel.
4. SD/map and Wi-Fi/BLE can proceed in parallel but must not block offline messaging.
5. Packets/diagnostics must preserve serial parity.
6. Final release only after hardware tab abuse, scroll probe, RF proof, SD matrix, manual touch review, and soak tests pass.

Start by auditing main/ui/ui_phase1.c, app_model snapshot limits, store capacities, storage_status/rp2040 bridge behavior, and current simulator tests. Add repeatable hardware tests for tab abuse and scroll behavior. Then fix the bottom-tab crash and scrolling before redesigning the screens.
```

---

## 16. Final definition of done

Production is done when a normal user can plug in the D1L, optionally insert an SD card, complete onboarding, see mesh activity on Home, open Public chat, compose a message, browse local nodes/repeaters/room servers, inspect packet traffic, set location on Map, configure Wi-Fi/BLE/SD safely, and leave the device running without crashes.

No release tag should be cut until:

- P0 tab crash is fixed on the user’s real D1L.
- Touch scrolling is proven on every long page.
- Manual touch review is complete.
- 12-hour idle/listening soak passes.
- 1-hour active messaging soak passes.
- SD boot/use matrix passes.
- RF Public + DM proof passes.
- Final docs match actual behavior.
- Known limitations are honest and specific.
- `scripts/release_gate_audit_d1l.py --fail-on-open-p0` passes against the final
  release evidence bundle.
