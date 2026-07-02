# MeshCore DeskOS D1L — Final Production Roadmap, Audit, and Codex Handoff

**Project:** MeshCore firmware for Seeed SenseCAP Indicator D1L  
**Repo:** `n30nex/SIGUI`  
**Branch audited:** `feature/meshcore-deskos-d1l`  
**Target release:** public binary release candidate, then production binary  
**Prepared:** 2026-07-02  
**Primary executor:** GPT-5.5 Codex xhigh with sub-agents  
**User hardware evidence:** latest binary tested on a real D1L, with photos showing the current Home, Messages, Message Detail, Compose, Nodes, Node Detail, Map, Packets, and Settings screens.

---

## 0. Executive instruction to Codex

Treat this file as the new source-of-truth handoff plan for finishing **MeshCore DeskOS D1L**. The current repo is no longer just a bring-up skeleton: it has working radio, UI, storage, CI, release packaging, diagnostics, and real-hardware evidence. The remaining work is now a production-readiness push.

Do **not** keep expanding the firmware by adding more half-finished features. First close the release blockers found by real hardware testing:

1. **Remove all on-device SD formatting.**
2. **Fix the 480×480 UI layout so no text overlaps and the keyboard is usable.**
3. **Make all core lists scrollable and page-backed, especially nodes and packets.**
4. **Finish Wi-Fi setup enough to support controlled tile downloads.**
5. **Make the Settings page a simple end-user setup center.**
6. **Pass a fail-closed release gate with hardware evidence before any public binary.**

The product goal is a desk-ready MeshCore companion that a normal user can flash and use without serial commands.

---

## 1. Updated product goal

MeshCore DeskOS D1L must turn the Seeed Indicator D1L into a polished, touch-first MeshCore desk console:

- Public MeshCore chat.
- Direct messages.
- Heard nodes, contacts, repeaters, room servers, paths, and routes.
- Packet feed and signal diagnostics.
- Optional SD-backed history and map tiles.
- Optional Wi-Fi setup for time, tile downloads, OTA, and local management.
- Optional BLE companion mode once memory and coexistence are proven.
- Canada/USA MeshCore defaults.
- Fully offline normal operation.
- Public binary release package with checksums, flashing scripts, recovery docs, and a clear known-limitations document.

The UI should feel like a modern dark mobile OS adapted to a square 480×480 desk display. It must not feel like a cramped terminal or developer dashboard.

---

## 2. Current repo audit summary

### 2.1 What is already strong

The repo already has a serious firmware foundation:

- ESP-IDF based build targeting the D1L.
- Seeed Indicator BSP integration.
- D1L pin/profile contracts.
- MeshCore identity and Canada/USA radio defaults.
- SX1262 LoRa bring-up and MeshCore Public TX/RX validation.
- First DM TX/ACK/PATH plumbing.
- App model snapshot architecture.
- NVS-backed settings, identity, contacts, read state, and fallback history.
- RP2040 SD bridge firmware target.
- SD file-operation protocol and retained-blob abstraction.
- Home, Messages, Nodes, Map, Packets, Settings, sheets, and lock screen.
- Serial diagnostics and hardware smoke scripts.
- UI simulator screenshots and dry-run CI coverage.
- Release package generation with checksums and flashing scripts.
- Release gate script that currently fails closed while evidence is missing.

This is much further along than a mockup. The remaining work is mostly product hardening, UI layout correction, storage policy cleanup, and release validation.

### 2.2 Important audited paths

Codex must inspect these files first:

```text
README.md
docs/ROADMAP.md
docs/KNOWN_LIMITATIONS.md
docs/USER_GUIDE_D1L.md
docs/DEVELOPER_GUIDE_D1L.md
docs/FLASH_RECOVERY_D1L.md
docs/RELEASE_CHECKLIST.md

CMakeLists.txt
main/CMakeLists.txt
main/d1l_config.h

main/app/app_model.h
main/app/app_model.c
main/app/settings_model.h
main/app/settings_model.c

main/ui/ui_phase1.c

main/comms/connectivity_manager.h
main/comms/connectivity_manager.c
main/comms/usb_console.c

main/storage/storage_status.h
main/storage/storage_status.c
main/storage/retained_blob_store.h
main/storage/retained_blob_store.c
main/storage/map_tile_store.h
main/storage/map_tile_store.c
main/storage/export_store.h
main/storage/export_store.c

main/hal/rp2040_bridge.h
main/hal/rp2040_bridge.c
firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino

main/mesh/message_store.h
main/mesh/message_store.c
main/mesh/dm_store.h
main/mesh/dm_store.c
main/mesh/node_store.h
main/mesh/node_store.c
main/mesh/contact_store.h
main/mesh/contact_store.c
main/mesh/route_store.h
main/mesh/route_store.c
main/mesh/packet_log.h
main/mesh/packet_log.c
main/mesh/meshcore_service.h
main/mesh/meshcore_service.c

scripts/smoke_d1l.py
scripts/ui_tab_abuse_d1l.py
scripts/scroll_probe_d1l.py
scripts/soak_d1l.py
scripts/manual_ui_review_d1l.py
scripts/release_gate_audit_d1l.py
scripts/package_release_d1l.py
.github/workflows/d1l-ci.yml
```

### 2.3 Current architecture risk

`main/ui/ui_phase1.c` has become a monolithic UI file. It now owns the shell, every tab, many modals, keyboard sheets, map picker, packet details, settings sheets, storage sheets, Wi-Fi sheets, BLE sheets, and many static state buffers. This is acceptable for early bring-up, but it is now a production risk.

Before final release, split it into smaller modules or at minimum isolate the layout helpers, row widgets, sheets, and screen renderers:

```text
main/ui/
  ui_shell.c/.h
  ui_theme.c/.h
  ui_layout.h
  ui_widgets.c/.h
  screens/ui_home.c/.h
  screens/ui_messages.c/.h
  screens/ui_nodes.c/.h
  screens/ui_map.c/.h
  screens/ui_packets.c/.h
  screens/ui_settings.c/.h
  sheets/ui_compose_sheet.c/.h
  sheets/ui_message_detail_sheet.c/.h
  sheets/ui_node_detail_sheet.c/.h
  sheets/ui_map_location_sheet.c/.h
  sheets/ui_wifi_sheet.c/.h
  sheets/ui_storage_sheet.c/.h
```

Do not perform a huge refactor as the first task. First fix release blockers. Then split files once behavior is stable.

---

## 3. Real hardware UI audit from latest binary

The user’s real-device photos are current ground truth. Fold these findings into the release gate.

### 3.1 Home screen

Observed:

- Home status bar is readable and communicates “Mesh ready.”
- Top chips for Time, Wi-Fi, BLE, SD are useful.
- Public and DM cards are useful.
- Latest messages rows have serious overlap when sender name/id and message text are more than one line.
- The first latest-message row shows sender and text colliding in the same vertical area.
- The UI is visually close to the intended dark mobile style, but spacing is too tight for multiline mesh content.

Root cause in current UI:

- Home message preview rows are fixed-height.
- Sender is placed top-left.
- Status is placed top-right.
- Message text plus age/signal metadata is placed at the bottom in a single label.
- If the message wraps, it overlaps the sender/header zone.

Target:

- Each latest message row must have a dedicated header line and a dedicated body line.
- Sender must never share a vertical zone with text.
- Metadata should be secondary, smaller, and truncated or moved to detail.
- Home should show 3–5 clean previews, not 5 cramped rows if content is long.

Required design:

```text
[Sender / room / DM badge]                         [new]
Message preview, max 2 lines with wrap or dot
rssi -48 · snr 3.0 · hops 3 · 0s
```

Minimum row height: **72 px**.  
Long-message row height: **88 px** if two preview lines are enabled.

---

### 3.2 Messages screen

Observed:

- Header has title, metadata, and four buttons squeezed into one card.
- Public/DM tabs are good but need stronger active-state styling.
- Public rows overlap when text is long.
- Repeated sender name and message body collide.
- The screen currently shows only a small recent preview set rather than a proper scrollable conversation.
- “Read,” “Compose,” “History,” and “Test” are too cramped and compete with the title.

Target:

- Make the screen a proper messaging view, not a dashboard card.
- Header should be compact but not overloaded.
- Public/DM tabs should be obvious.
- Message rows should support multiline content without overlap.
- The list must scroll through retained history, not only preview items.
- The Test quick action should move into a developer/diagnostic menu or be hidden behind an advanced toggle before public release.

Required layout:

```text
Top: Messages
Subline: Public 16 unread · DM 0
Tabs: [Public] [DMs]
Primary action: floating or toolbar Compose button
Secondary: Read all, Search, History in overflow/tools sheet

Scrollable list:
  Sender                            new / sent / received
  Message preview, max 2–3 lines
  Signal/path summary, optional small text
```

Do not show raw sequence numbers in normal row UI.

---

### 3.3 Message detail sheet

Observed:

- The popup is functional but feels developer-oriented.
- It shows `#23 new` beside sender.
- It shows `uptime 101900ms`.
- It uses “Path hash 3 byte” wording directly in the main view.
- It does not visually separate user content from radio/debug metadata.

Target:

- Message detail must be user-friendly first, developer-friendly second.
- Remove the message number from the sender line.
- Remove uptime from normal view.
- Show timestamp/age instead when available.
- Keep path hash/sequence/raw details only under an “Advanced” toggle or developer mode.
- Make Reply and Close reliable and large.

Required normal fields:

```text
Message Detail

Sender
Krabs Node

Message
Can't get in your car...

Signal
RSSI -29 · SNR 3.0 · direct / hops 0

Path
Direct, no repeater path seen
```

Advanced-only fields:

```text
seq
uptime_ms
path_hash_bytes
raw_hex
ack hash
store backend
```

---

### 3.4 Compose screen and keyboard

Observed:

- Compose sheet opens above the dock but the bottom dock covers part of the keyboard.
- There is too much dead space above the keyboard.
- Text area is too small relative to available space.
- Keyboard is constrained in a sheet while the persistent bottom dock remains visible.
- Current layout is frustrating for Public and DM composition.

Target:

- When keyboard is active, hide the bottom dock or make compose a true full-screen mode.
- Use the full 480×480 screen.
- Keep Send/Clear/Close visible at top.
- Message preview/textarea should use meaningful space.
- Character counter should be above keyboard and not floating awkwardly.
- Do not let keyboard or text input collide with nav dock.

Preferred layout:

```text
Top bar: Public / DM target                 Send Clear Close
Textarea: 120–150 px tall
Counter + helper text
Keyboard: 230–260 px tall, aligned to bottom
No bottom dock while composing
```

Acceptance:

- On real hardware, every keyboard row is touchable.
- No part of the keyboard is hidden by dock/nav.
- Close/cancel returns to the previous tab.
- Ready/Enter sends only if that behavior is deliberate and documented.
- Draft survives accidental close if feasible.

---

### 3.5 Nodes screen

Observed:

- Heard Nodes and Contacts cards are good.
- Only a few contacts/nodes are visible.
- Node list must show all heard nodes and be scrollable.
- Node selection currently shows details, but action model is incomplete.
- For companions, user expects DM.
- For repeaters/room servers, user expects management/login options eventually.

Target:

- Nodes tab becomes a full browser with filters, sorting, and virtualized scrolling.
- It must not be limited to the small app snapshot preview.
- Show all heard nodes retained in node store.
- Node actions must depend on role and key availability.

Required actions:

For **companion/contactable nodes**:

- DM
- Add/Promote Contact
- Route Trace
- Ping
- Export Contact QR if public key retained
- Favorite
- Mute

For **repeaters / room servers**:

- Details
- Route Trace
- Ping
- Login / Manage
- Management must be gated behind Advanced/Admin mode.
- Login must not be fake UI. Implement only when the MeshCore management protocol, auth model, and safety rules are known.
- Until then, show “Management pending” with a clear explanation.

For **unknown nodes**:

- Details
- Add Contact if key retained
- Route Trace if data exists

Acceptance:

- A live mesh with 100+ heard nodes can scroll smoothly.
- No node list crash, stutter, or stack overflow.
- Role badges are readable.
- Node detail modal does not cover essential actions.
- Admin actions cannot be triggered accidentally.

---

### 3.6 Map page and location setup

Observed:

- Initial location picker is messy.
- Current N/S/E/W manual picker is slow and confusing.
- Map page says tile cache unavailable and downloads pending.
- User wants manual lat/lon entry through onscreen keyboard.
- If no tiles exist, map must clearly explain how to get tiles.

Target:

- Replace manual pan/zoom/drop-pin sheet with a simple manual coordinate entry sheet.
- Use two text fields: latitude and longitude.
- Use numeric keyboard.
- Validate ranges.
- Show examples and current saved coordinates.
- Keep a quick “Use saved/default” only if it makes sense.
- Do not auto-download map data on boot.

Required Set Location sheet:

```text
Set D1L Location

Latitude
[ 43.6532000 ]

Longitude
[ -79.3832000 ]

Save Pin     Clear     Cancel
```

Validation:

- Latitude: -90.0000000 to 90.0000000
- Longitude: -180.0000000 to 180.0000000
- Store internally as E7 integers.
- Show friendly validation errors.

No-tile state copy:

```text
No offline map tiles found.

1. Connect Wi‑Fi in Settings.
2. Set your D1L location.
3. Download a small tile area for this location.
4. Tiles will be stored on the SD card.

Normal mesh messaging works without maps.
```

Tile download policy:

- Use a provider configuration, not a hardcoded provider.
- Never bulk-download from a provider that prohibits offline prefetch.
- Show attribution on the map screen.
- Respect provider cache headers and rate limits.
- Default release should either:
  - use a provider that explicitly permits offline tile downloads, or
  - require a user-configured tile endpoint/self-hosted tile service.

Do **not** add an “offline download” button pointed at the public OSM tile server. That would be a release blocker unless policy permission is confirmed.

---

### 3.7 Packets page

Observed:

- Packets page is useful and visually close.
- It shows live tail status and filter buttons.
- Only a few latest packets are visible.
- User wants to scroll at least 100 latest packets and load older.
- User wants 24 hours on SD, less on NVS.

Current storage foundation:

- Packet log has RAM, NVS fallback, and SD capacity constants.
- UI snapshot previews only a few packets.
- The UI uses a filtered array with preview capacity, so visible list is too short even if backend stores more.

Target:

- Packet page must be a real list browser, not a preview.
- Latest 100 packets should be scrollable by default.
- “Load older” should page through SD-backed packet history.
- On SD, retain last 24 hours or configured max count, whichever is smaller.
- On NVS fallback, retain a small bounded ring and clearly show that SD is required for 24h history.
- Packet details should be tap-friendly and searchable.
- Raw hex stays developer/advanced only.

Required packet retention policy:

```text
SD present + DeskOS structure ready:
  Retain packet records for 24 hours.
  Also enforce max storage count/size limit.
  Flush in batches.
  Recover cleanly after reboot.

No SD:
  Keep a small NVS fallback ring.
  UI says "NVS mode: limited packet history".
```

Required packet UI:

```text
Packets
live tail · 25 samples · latest RSSI -50 · avg -46
[All] [RX] [TX] [Text] [Search] [Pause]

Scrollable list, latest first:
  RX advert                 rssi -33 · snr 3.2
  Krabs Lagoon 937D2908 · #14
  ...
[Load older]
```

Acceptance:

- Scroll probe verifies at least 100 rows.
- Packet search works over loaded/paged rows.
- Load older works from SD.
- NVS mode clearly states its limit.
- No crash after repeated filter/search/pause/detail operations.

---

### 3.8 Settings page

Observed:

- Settings page has useful pieces but is not yet an end-user setup center.
- Wireless, MeshCore, Storage, Display are mixed in a scroll view.
- It uses developer-state words such as `nvs`, `offline_first_one`, and pending bridge text.
- User wants every setup feature represented simply: SD, Wi-Fi, BLE, map/tile downloads, radio, display, diagnostics.

Target:

Make Settings the single place a public user goes to make features work.

Required Settings landing cards:

```text
Setup

SD Card
Ready / Not inserted / Needs FAT32 / Creating files / Error

Wi‑Fi
Off / Needs network / Connected / Failed

BLE
Off / Pairing / Ready / Build disabled

Radio
US/CAN 910.525 · BW62.5 · SF7 · CR5

Map Tiles
No tiles / Ready / Download needed / Downloading

Display
Brightness · Night mode · High contrast

Identity
Node name · fingerprint · reset/export options

Diagnostics
Health · crash log · export support bundle

About
Version · commit · license · release notes
```

Each card opens a guided sheet with clear next steps. The user should not need serial commands to understand what to do.

---

## 4. Release blockers and severity

### P0 — Must fix before public binary

| Blocker | Why it blocks release | Required fix |
|---|---|---|
| On-device SD formatting still exists | User explicitly rejects it; destructive operation is unacceptable for public release | Remove/disable all firmware format paths and docs/scripts references |
| Home latest-message overlap | Core screen looks broken on real hardware | Dynamic row heights and separated header/body/meta labels |
| Messages screen text overlap | Core chat workflow is visually broken | Rebuild message rows and header layout |
| Compose keyboard covered by dock | User cannot reliably type | Full-screen compose or hide dock while keyboard is open |
| Node list limited to preview | Cannot browse all heard nodes | Add paged/virtualized node browser |
| Packets limited in UI | Cannot inspect latest 100/older packets | Add paged packet query + scroll + load older |
| Map set-location sheet messy | First map setup is confusing | Replace with lat/lon keyboard sheet |
| Wi-Fi runtime not complete | Tile downloads and setup cannot work | Implement Wi-Fi scan/connect/status runtime |
| Tile downloader missing | Map cannot become useful | Implement compliant tile download manager |
| Settings page not user-friendly | Public users cannot configure features | Settings setup-center overhaul |
| Release gate still fail-closed | No public release without evidence | Complete SD matrix, 12h soak, manual UI review/photos, inbound DM proof |

### P1 — Should complete for first production release if possible

| Item | Notes |
|---|---|
| BLE companion runtime | Can be documented as limited if not stable, but settings must be honest |
| DM inbound proof and ACK/PATH RF proof | Important for confidence |
| Repeater/room server login groundwork | Gate as advanced/admin; do not ship fake management |
| Contact export scan/import proof | QR exists; verify with client |
| Better time display | Wi-Fi/NTP or retained fallback |
| Support bundle export | SD and serial support bundle for user bug reports |
| UI module split | Improves maintainability before post-release changes |

### P2 — Can follow public binary if documented

| Item | Notes |
|---|---|
| GPS/location-source integration | Manual lat/lon is enough for first release |
| Advanced telemetry history | Useful but not mandatory |
| OTA | Nice but not mandatory if flashing package is solid |
| Full map rendering polish | No-tile state + tile storage/download pipeline are more urgent |
| Observer upload | Optional and off by default |

---

## 5. New SD-card policy: no formatting, FAT32 only

This is the biggest policy change from the previous roadmap.

### 5.1 Required public behavior

The firmware must **never format an SD card**.

New rule:

```text
End user supplies a freshly formatted FAT32 SD card.

On boot:
  If no SD card:
    Use onboard NVS fallback.
    UI says "No SD card — limited local history."

  If SD card present and FAT32 mount succeeds:
    If /deskos structure exists and manifest is valid:
      Use SD-backed storage.
    If /deskos structure is missing:
      Create the DeskOS directories and manifests.
      Use SD-backed storage.
    If /deskos manifest exists but is invalid:
      Do not overwrite user data.
      Show "DeskOS files invalid — back up/reformat FAT32 on a computer."
      Use NVS fallback.

  If card is present but not FAT32 or cannot mount:
    Do not format.
    Show "Format this card as FAT32 on a computer."
    Use NVS fallback.
```

### 5.2 Required directory structure

Keep or update the existing RP2040 bridge directory creation model:

```text
/deskos/
  manifest.json
  stores/
    messages/
      public/
      dm/
    nodes/
    contacts/
    routes/
    packet_log/
  map/
    manifest.json
    tiles/
    packs/
  exports/
    diagnostics/
    data/
  tmp/
  logs/
```

### 5.3 Remove destructive code paths

Codex must remove or compile-disable these paths:

```text
main/hal/rp2040_bridge.h
  D1L_RP2040_SD_FORMAT_CONFIRMATION
  d1l_rp2040_bridge_format_sd(...)

main/hal/rp2040_bridge.c
  D1L_RP2040_SD_FORMAT_QUERY_PREFIX
  D1L_RP2040_SD_FORMAT_REPLY_PREFIX
  D1L_RP2040_SD_FORMAT_PROGRESS_PREFIX
  parse_sd_format_line(...)
  d1l_rp2040_bridge_format_sd(...)

main/storage/storage_status.c
  d1l_storage_format_sd_confirmed(...)
  format_confirmation_required action
  confirm_required action

firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino
  FORMAT_REQUEST
  FORMAT_REPLY
  FORMAT_PROGRESS_REPLY
  FORMAT_CONFIRMATION
  FatFormatter usage
  format_card()
  command handler for DESKOS_SD_FORMAT
```

Do not leave hidden serial or UI paths that can format. Do not leave a destructive path “for developers.” Public and developer builds should share the same non-destructive storage policy.

### 5.4 Update SD state vocabulary

Replace format-oriented states with user-safe states.

Use:

```text
no_card
checking
fat32_ready
creating_deskos_files
ready
not_fat32_or_unmountable
deskos_manifest_invalid
bridge_unavailable
bridge_protocol_pending
error
```

Avoid in user UI:

```text
format_required
format_supported
format_confirmation_required
confirm_required
```

Internal compatibility fields can remain only temporarily if too risky to migrate immediately, but UI and public docs must not tell users to format on-device.

### 5.5 Required SD acceptance tests

Add or update tests:

```text
tests/test_sd_policy_no_format.py
```

Must assert:

- No source file contains `DESKOS_SD_FORMAT` except a release-note entry saying it was removed, if absolutely necessary.
- No UI string contains “format confirmation.”
- `storage status` for an unmountable card says “format FAT32 on computer,” not “format on device.”
- FAT32 card missing `/deskos` creates dirs and manifest.
- FAT32 card with valid `/deskos` uses SD.
- No card uses NVS.
- Invalid `/deskos/manifest.json` does not overwrite data.

Update hardware scripts:

```text
scripts/sd_boot_prepare_acceptance_d1l.py
scripts/sd_reboot_remount_acceptance_d1l.py
scripts/sd_file_canary_d1l.py
scripts/sd_retained_history_acceptance_d1l.py
scripts/release_gate_audit_d1l.py
```

Remove destructive format scenarios from all scripts.

---

## 6. UI layout foundation tasks

### 6.1 Add common layout constants

Create:

```text
main/ui/ui_layout.h
```

Suggested constants:

```c
#define D1L_UI_W 480
#define D1L_UI_H 480
#define D1L_TOP_BAR_H 56
#define D1L_DOCK_H 62
#define D1L_CONTENT_Y D1L_TOP_BAR_H
#define D1L_CONTENT_H_WITH_DOCK (D1L_UI_H - D1L_TOP_BAR_H - D1L_DOCK_H)
#define D1L_CONTENT_H_FULLSCREEN (D1L_UI_H - D1L_TOP_BAR_H)
#define D1L_SIDE_PAD 18
#define D1L_CARD_W 424
#define D1L_TOUCH_MIN 44
#define D1L_ROW_GAP 8
#define D1L_ROW_H_MESSAGE_COMPACT 72
#define D1L_ROW_H_MESSAGE_TALL 88
#define D1L_ROW_H_PACKET 58
#define D1L_ROW_H_NODE 70
```

### 6.2 Add safe label helpers

Create or improve helpers:

```c
lv_obj_t *d1l_ui_label_dot(lv_obj_t *parent, const char *text, uint32_t color, int width);
lv_obj_t *d1l_ui_label_wrap(lv_obj_t *parent, const char *text, uint32_t color, int width);
lv_obj_t *d1l_ui_meta_label(lv_obj_t *parent, const char *text, int width);
void d1l_ui_place_header_body_meta(...);
```

Rules:

- Header labels use `LV_LABEL_LONG_DOT`.
- Body preview labels use either wrap with max height or dot.
- Meta labels are small, single-line, and optional.
- No label may depend on “bottom align” inside a row that can wrap.

### 6.3 Hide dock during keyboard sheets

Option A: full-screen compose/search/Wi-Fi coordinate sheets that cover the dock.

Option B: add a global dock pointer and hide it while keyboard sheet is visible.

Preferred:

```c
static lv_obj_t *s_dock;

static void dock_set_hidden_for_modal(bool hidden) {
    if (s_dock) {
        if (hidden) lv_obj_add_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
    }
}
```

Any sheet with keyboard:

- Compose Public
- Compose DM
- Public Search
- Packet Search
- Contact Edit
- Wi-Fi SSID/password
- Map lat/lon
- Onboarding name

must either cover the dock or hide it.

### 6.4 Add text-overflow CI from simulator

Extend `tools/ui_simulator.py` report to fail when:

- Labels overlap in known problem screens.
- Keyboard is partially below visible area.
- A modal with keyboard shows the dock.
- Text is clipped in Home message previews.
- Messages screen row body overlaps author/status.
- Settings card descriptions overflow their bounds.

---

## 7. Messaging implementation plan

### 7.1 Public channel

Required changes:

- Replace current preview-only message list with a paged query API.
- Keep the main Messages tab as a scrollable list.
- Use newest-first or oldest-first consistently.
- If oldest-first, auto-scroll to bottom only on first open or when user is already near bottom.
- Add “Load older” button or infinite scroll trigger.
- Keep Search as a sheet.
- Hide Test action in normal mode.

New app API:

```c
typedef struct {
    uint32_t before_seq;
    size_t limit;
    const char *search;
    bool newest_first;
} d1l_message_query_page_t;

size_t d1l_app_model_query_public_messages_page(
    const d1l_message_query_page_t *query,
    d1l_message_entry_t *out_entries,
    size_t max_entries
);
```

### 7.2 DMs

Required changes:

- DM list should show conversations, not only recent messages.
- DM thread should show retained thread history with scrolling.
- Thread rows need real row heights.
- Reply opens full-screen compose, not cramped sheet.
- Read action applies to current thread and updates state immediately.

New app API:

```c
typedef struct {
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char alias[D1L_CONTACT_ALIAS_LEN];
    uint32_t last_seq;
    uint32_t unread_count;
    uint32_t last_uptime_ms;
    char preview[D1L_MESSAGE_TEXT_LEN];
    bool muted;
    bool favorite;
} d1l_dm_conversation_t;

size_t d1l_app_model_query_dm_conversations(
    d1l_dm_conversation_t *out,
    size_t max_entries,
    const char *search,
    uint32_t offset
);
```

### 7.3 Message detail

Implement normal/advanced detail modes.

Normal mode excludes:

- raw seq number in title
- uptime_ms
- raw packet hex
- path hash byte count

Advanced mode may show these.

Add tests:

```text
tests/test_ui_message_detail_contract.py
```

Assertions:

- Normal detail string does not contain `uptime`.
- Normal detail title does not include `#<seq>`.
- Advanced detail can contain seq/uptime/raw if developer mode is enabled.

---

## 8. Nodes and repeater management plan

### 8.1 Full node browser

Do not rely on:

```c
D1L_APP_SNAPSHOT_NODE_PREVIEW
```

for the actual Nodes screen. Snapshot previews are for Home/dashboard only.

Add a nodes page model:

```c
typedef struct {
    d1l_node_filter_t filter;
    d1l_node_sort_t sort;
    const char *search;
    size_t offset;
    size_t limit;
    bool keyed_only;
    bool reachable_only;
} d1l_node_page_query_t;

size_t d1l_app_model_query_nodes_page(
    const d1l_node_page_query_t *query,
    d1l_node_view_t *out,
    size_t max_entries
);
```

If `node_store_query` cannot support offsets, add it.

### 8.2 Node row content

Each row:

```text
Name / alias                         [ROLE]
fingerprint short · key retained/reachable
rssi -50 · snr 3.0 · hops 2 · last heard 4m
```

Avoid showing too many words in the row. Move detailed fields to detail sheet.

### 8.3 Node detail actions

Detail action buttons are role-aware:

```text
Companion/contact:
  DM
  Trace
  Ping
  Add/Edit Contact
  Export QR

Repeater/room:
  Trace
  Ping
  Login / Manage
  Add Contact
  Details

Unknown:
  Details
  Add Contact if key retained
  Trace if data exists
```

### 8.4 Repeater login / management

This is a new feature and must be scoped safely.

For first production binary, implement the UI shell and backend contract, but do not claim management works until a real controlled repeater test passes.

Required safe behavior:

- “Login” appears only for `role=repeater` or `role=room`.
- First tap opens an explanation sheet.
- User must enable Advanced/Admin mode in Settings.
- Login requires a retained key/contact and a defined MeshCore management protocol.
- No management TX is sent from a casual tap.
- All admin actions log to packet/diagnostic log.
- Until protocol is implemented, sheet says:
  “Repeater management is not available in this build.”

Do not ship a fake login form.

---

## 9. Packets and retention plan

### 9.1 Backend

Current packet log has a RAM ring and NVS/SD persistence foundation. Extend it to enforce time-based retention.

Add timestamp support. Uptime-only is not enough for 24-hour retention across reboot. Use:

- Wi-Fi/NTP time if available.
- If no wall-clock time, use monotonic uptime for session retention and mark records as “time unknown.”
- Once time becomes available, store epoch seconds for new records.

Extend entry:

```c
uint64_t epoch_ms;      // 0 if unavailable
uint32_t uptime_ms;
```

If binary compatibility is risky, create schema v4 and migration.

SD retention:

```text
D1L_PACKET_LOG_SD_RETENTION_HOURS = 24
D1L_PACKET_LOG_SD_MAX_RECORDS = 4096 or storage-dependent
```

NVS retention:

```text
D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY = 32 or keep 8 if NVS pressure demands it
```

### 9.2 Query API

Add:

```c
typedef struct {
    uint32_t before_seq;
    uint64_t since_epoch_ms;
    const char *direction;
    const char *kind;
    const char *search_text;
    size_t limit;
} d1l_packet_query_page_t;

size_t d1l_packet_log_query_page(
    const d1l_packet_query_page_t *query,
    d1l_packet_log_entry_t *out_entries,
    size_t max_entries
);
```

### 9.3 UI

- Latest 100 visible through scroll.
- Load older button at bottom.
- Filter/search applies to currently loaded and can request additional pages.
- Pause freezes view but not backend capture.
- Details open from every row.
- Raw hex hidden unless Advanced mode.

### 9.4 Tests

Add:

```text
tests/test_packet_log_retention.py
tests/test_packet_ui_paging_contract.py
```

Acceptance:

- With generated 150 packets, UI simulator shows 100 scrollable and load older.
- With SD enabled, old packets prune after 24h policy.
- With no SD, UI shows limited NVS mode.
- Search can find rows beyond the first 4 snapshot items.

---

## 10. Map and tile-download plan

### 10.1 Manual location sheet

Replace existing N/S/E/W map picker with keyboard entry.

Files:

```text
main/ui/ui_phase1.c
main/app/app_model.c
main/app/settings_model.c
```

Required helpers:

```c
bool d1l_parse_coord_to_e7(const char *text, bool latitude, int32_t *out_e7);
void d1l_format_coord_e7(char *dest, size_t dest_size, int32_t e7);
```

UI:

- Two textareas.
- Numeric keyboard.
- Save, Clear, Cancel.
- Error label.
- Current saved coordinates visible.
- No default Toronto coordinate unless explicitly documented as placeholder; prefer blank/unset.

### 10.2 Map no-tile state

If no tile cache:

- Explain messaging still works.
- Explain Wi-Fi and SD requirements.
- Button: `Open Wi‑Fi Setup`.
- Button: `Set Location`.
- Button: `Download Tiles` only enabled when:
  - Wi-Fi connected,
  - SD ready,
  - location set,
  - provider configured and allowed.

### 10.3 Tile provider rules

Implement a tile provider abstraction:

```c
typedef struct {
    char name[32];
    char url_template[128];
    char attribution[96];
    bool offline_download_allowed;
    uint16_t min_zoom;
    uint16_t max_zoom;
    uint16_t max_tiles_per_batch;
    uint16_t min_delay_ms;
} d1l_tile_provider_t;
```

Default state for public binary:

- No hardcoded forbidden bulk/offline downloads.
- If provider not configured or not allowed, UI says tile downloads are unavailable.
- Provide docs for self-hosted tile endpoint or a provider that explicitly allows offline/prefetch.
- Ensure visible attribution.

### 10.4 Download manager

Add:

```text
main/comms/wifi_manager.c/.h
main/storage/map_tile_downloader.c/.h
```

Required features:

- Wi-Fi must be active and connected.
- SD card must be ready.
- User chooses a small area around current pin.
- Estimate tile count before download.
- Require confirmation if tile count above safe threshold.
- Progress UI: downloaded / total / failed.
- Cancel button.
- Resumable by checking existing tiles.
- Store metadata manifest.
- Do not download in background without user action.

Acceptance:

- Can download a tiny test tile set from a configured local/test tile server in CI or hardware bench.
- Can render or at least detect cached tiles.
- If downloads fail, UI remains usable and says why.

---

## 11. Wi-Fi and BLE plan

### 11.1 Wi-Fi

Current state is a settings/persistence foundation, not a live Wi-Fi stack. Finish:

- `wifi scan`
- touch Wi-Fi scan list
- save network
- connect/disconnect
- connection status with IP
- failure reason
- NTP time sync
- tile download readiness
- optional OTA/local management later

Required app states:

```text
off
build_disabled
profile_required
scanning
connecting
connected
failed_auth
failed_no_ap
failed_timeout
disconnecting
```

UI sheet:

```text
Wi‑Fi Setup

Status: Off / Connected / Failed
Saved network: ...
[Scan]
[Connect]
[Disconnect]
[Forget]
```

Do not require typing SSID manually if scan works.

### 11.2 BLE

BLE can remain limited if heap/coexistence is not proven, but UI must be honest.

States:

```text
off
build_disabled
pairing_pending_stack
pairing
ready
connected
error
```

If build disabled, say “BLE companion is not included in this build,” not just “BLE off.”

### 11.3 Coexistence

Keep offline-first one-companion-radio policy unless testing proves Wi-Fi and BLE can safely coexist.

Rules:

- Enabling Wi-Fi disables BLE if required.
- Enabling BLE disables Wi-Fi if required.
- UI explains this before toggling.
- MeshCore LoRa must not be degraded by Wi-Fi/BLE mode.

---

## 12. Settings overhaul plan

Replace the current long developer settings scroll with a setup dashboard.

### 12.1 Settings landing

Cards:

```text
SD Card
Wi‑Fi
BLE
Radio
Map Tiles
Display
Identity
Diagnostics
About
Advanced
```

Card state is friendly:

```text
SD Card: Ready
SD Card: Insert FAT32 card
SD Card: Creating DeskOS files
SD Card: Card not FAT32

Wi‑Fi: Off
Wi‑Fi: Connected to <ssid>
Wi‑Fi: Needs password
Wi‑Fi: Build disabled

BLE: Off
BLE: Pairing
BLE: Build disabled

Map Tiles: No tiles
Map Tiles: Ready
Map Tiles: Needs Wi‑Fi + SD
```

### 12.2 Storage setup sheet

New copy:

```text
SD Card

Use a FAT32 card prepared on your computer.
DeskOS will create its folders automatically.
No formatting is performed on this device.

Status: Ready / No card / Needs FAT32 / Error
Storage: SD / NVS fallback
```

Buttons:

- Refresh
- Open diagnostics
- Export support bundle
- Close

No Format button.

### 12.3 Advanced mode

Advanced mode unlocks:

- raw packet hex
- repeater login/management experiments
- radio profile editing beyond defaults
- factory reset
- crash/debug exports

Advanced mode must have a warning and require confirmation.

---

## 13. Release-gate acceptance plan

Public binary release is allowed only when `scripts/release_gate_audit_d1l.py` outputs:

```json
{
  "ready_for_public_release": true
}
```

### 13.1 Required CI checks

Run:

```powershell
python -m pytest tests -q
python .\scripts\smoke_d1l.py --dry-run
python .\scripts\ui_tab_abuse_d1l.py --dry-run --cycles 100
python .\scripts\scroll_probe_d1l.py --dry-run --screens messages,nodes,packets,settings,map
python .\scripts\soak_d1l.py --dry-run --duration-sec 60 --sample-interval-sec 15 --sample-storage --allow-sd-unavailable
python .\tools\ui_simulator.py --out artifacts/ui-sim
python .\tools\ui_simulator.py --scenario large-mesh --out artifacts/ui-sim-large
python .\tools\ui_simulator.py --scenario storage-states --out artifacts/ui-sim-storage
python .\scripts\release_gate_audit_d1l.py --out artifacts/release-gate/d1l-release-gate-audit.json
```

### 13.2 Required hardware evidence

Use the user-provided COM port. Do not hardcode a port.

```powershell
$env:D1L_PORT = "COM12"

python .\scripts\backup_flash_d1l.py --port $env:D1L_PORT --size 8MB
.\scripts\flash_d1l.ps1 -Port $env:D1L_PORT

python .\scripts\smoke_d1l.py --port $env:D1L_PORT --manual-touch
python .\scripts\ui_tab_abuse_d1l.py --port $env:D1L_PORT --cycles 100
python .\scripts\scroll_probe_d1l.py --port $env:D1L_PORT --screens messages,nodes,packets,settings,map

python .\scripts\sd_file_canary_d1l.py --port $env:D1L_PORT --allow-unavailable
python .\scripts\sd_retained_history_acceptance_d1l.py --port $env:D1L_PORT --allow-unavailable --token prod
python .\scripts\sd_map_tile_canary_d1l.py --port $env:D1L_PORT --allow-unavailable --token prod
python .\scripts\sd_reboot_remount_acceptance_d1l.py --port $env:D1L_PORT --token prod

python .\scripts\rf_full_acceptance_d1l.py --port $env:D1L_PORT --commit <sha>
python .\scripts\soak_d1l.py --port $env:D1L_PORT --duration-sec 43200 --sample-interval-sec 300 --sample-storage --allow-sd-unavailable
python .\scripts\manual_ui_review_d1l.py --port $env:D1L_PORT --photo-dir .\artifacts\hardware\com12\photos --confirm-display-stable --confirm-touch-accurate --confirm-bottom-tabs --confirm-messages-public --confirm-dm-workflow --confirm-nodes-contacts-routes --confirm-map-storage --confirm-settings-sheets --confirm-photos-current
```

### 13.3 Manual UI photo checklist

The final release artifact must include photos/screenshots proving:

- Home with long latest message: no overlap.
- Messages public with long messages: no overlap.
- DM thread: no overlap.
- Compose Public: keyboard not covered.
- Compose DM: keyboard not covered.
- Message detail normal mode: no # in title, no uptime.
- Nodes list with many nodes: scroll works.
- Node detail for companion: DM action visible.
- Node detail for repeater/room: login/manage gated or pending.
- Map with no tiles: clear Wi-Fi/download instructions.
- Set location sheet: lat/lon keyboard.
- Packets list with at least 100 rows or simulated/hardware-generated proof.
- Settings landing cards: SD, Wi-Fi, BLE, Radio, Map Tiles, Display, Identity, Diagnostics.
- Storage sheet: no Format button.

---

## 14. File-by-file implementation tasks

### 14.1 `main/ui/ui_phase1.c`

Immediate fixes:

- Home message row height and layout.
- Message row height and layout.
- Messages header crowding.
- Full-screen compose or dock hiding.
- Message detail normal/advanced split.
- Map location lat/lon sheet.
- Settings landing dashboard.
- Packet list paging UI.
- Node list paging UI.

Medium-term:

- Split into modules.
- Add reusable row widgets.
- Add modal manager so only one sheet is active.
- Add keyboard modal behavior that hides dock.
- Add theme/layout constants.

### 14.2 `main/app/app_model.h/.c`

Add paged query APIs:

- messages
- DM conversations
- DM thread pages
- nodes
- packets
- map tile status/download job state
- Wi-Fi scan/connect state

Keep snapshot previews for Home only.

### 14.3 `main/storage/storage_status.*`

Remove format-facing state/action.

Change to:

- no card
- FAT32 ready
- creating files
- ready
- card not mountable / needs FAT32 on computer
- invalid DeskOS manifest
- NVS fallback

### 14.4 `main/hal/rp2040_bridge.*`

Remove format command support.

Keep:

- ping
- status
- mount
- diag
- file stat/read/write/append/delete/rename

### 14.5 `firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino`

Remove:

- FatFormatter include/dependency if only used for formatting.
- format constants.
- `format_card()`.
- command handler for `DESKOS_SD_FORMAT`.

Keep and harden:

- manual probe.
- mount.
- FAT type detection.
- `prepare_deskos_structure()`.
- manifest validation.
- file operations.

### 14.6 `main/mesh/packet_log.*`

Add:

- time-based retention.
- paged query.
- 24h SD policy.
- NVS limit messaging.
- schema migration if needed.

### 14.7 `main/comms/connectivity_manager.*`

Finish real Wi-Fi stack:

- scan
- connect
- disconnect
- IP/status
- NTP
- error states

BLE can remain pending if documented.

### 14.8 `main/storage/map_tile_store.*`

Add:

- tile provider config.
- tile path read/stat APIs.
- download job metadata.
- tile count estimation.
- provider attribution.
- no-tile state support.

Create new downloader file if cleaner:

```text
main/storage/map_tile_downloader.c
main/storage/map_tile_downloader.h
```

### 14.9 Scripts

Update:

- remove destructive SD format tests.
- add no-format policy test.
- add long-message UI simulator scenario.
- add keyboard/dock occlusion detector.
- add 100-packet scroll proof.
- update release gate P0 list.

---

## 15. Sub-agent work plan

Use sub-agents, but integrate through one lead.

### Agent A — Storage policy and SD bridge

Goal: enforce no-format SD policy.

Tasks:

- Remove ESP32 format commands.
- Remove RP2040 format code.
- Update storage states/actions.
- Keep FAT32 mount and DeskOS folder creation.
- Update scripts/docs/tests.
- Produce SD matrix evidence.

Acceptance:

- Grep for `DESKOS_SD_FORMAT` fails except possibly in a migration note saying removed.
- FAT32 card with no `/deskos` becomes ready and directories are created.
- No card uses NVS.
- Unmountable card never formats and tells user to prepare FAT32 on a computer.

### Agent B — UI layout and keyboard

Goal: fix real hardware UI overlap.

Tasks:

- Add layout constants.
- Fix Home latest messages.
- Fix Messages rows/header.
- Fix Message Detail sheet.
- Make Compose full-screen or hide dock.
- Add simulator overlap tests.

Acceptance:

- User photo checklist passes.
- UI simulator reports no overlap.
- Keyboard never intersects dock.

### Agent C — Lists and paging

Goal: make nodes/messages/packets real browsers.

Tasks:

- Add paged app-model APIs.
- Implement scrollable nodes list.
- Implement scrollable packet list latest 100 + load older.
- Improve Public/DM history paging.

Acceptance:

- 100+ nodes simulated smooth.
- 150 packets simulated with latest 100 visible and load older.
- Message history scrolls beyond preview.

### Agent D — Wi-Fi and map tiles

Goal: finish Wi-Fi setup and controlled tile download readiness.

Tasks:

- Wi-Fi scan/connect/status.
- NTP time.
- Tile provider config.
- Tile download manager.
- Map no-tile instructions.
- Lat/lon keyboard sheet.

Acceptance:

- Wi-Fi can connect on hardware.
- Map page gives clear tile instructions.
- Tile download only enabled when provider policy allows.

### Agent E — Settings and release docs

Goal: turn settings into public setup center and update docs.

Tasks:

- Settings dashboard.
- Friendly copy.
- Known limitations update.
- User guide update.
- Release checklist update.
- About/release info.

Acceptance:

- User can understand SD/Wi-Fi/BLE/map setup from UI.
- Docs match actual behavior.

### Agent F — QA/release gate

Goal: make release gate pass.

Tasks:

- CI updates.
- Hardware scripts.
- 12h soak.
- RF full acceptance.
- Manual photo checklist.
- Release package verification.

Acceptance:

- `ready_for_public_release=true`.
- Release package includes checksums and non-destructive flash instructions.
- Known limitations are honest.

---

## 16. Public binary release checklist

Before tagging:

```text
[ ] No on-device SD formatting code remains.
[ ] FAT32 SD auto-creates DeskOS folders on boot.
[ ] No-card NVS fallback is clean and documented.
[ ] Home screen long messages do not overlap.
[ ] Messages screen long messages do not overlap.
[ ] Compose keyboard is not covered by dock.
[ ] Message detail hides seq/uptime in normal mode.
[ ] Nodes list scrolls all heard nodes.
[ ] Companion node detail offers DM when key/contact allows.
[ ] Repeater/room detail offers gated Login/Manage or clear pending state.
[ ] Map location uses lat/lon keyboard.
[ ] Map no-tile state tells user to connect Wi-Fi and download allowed tiles.
[ ] Wi-Fi scan/connect works or feature is honestly disabled.
[ ] Tile downloads only use policy-compliant provider config.
[ ] Packets page scrolls latest 100 and can load older.
[ ] SD packet retention keeps 24h or documented bounded cap.
[ ] NVS packet fallback is documented as limited.
[ ] Settings landing page is user-friendly.
[ ] BLE state is honest.
[ ] 1-hour active messaging soak passes.
[ ] 12-hour idle/listening soak passes.
[ ] SD matrix passes.
[ ] RF full acceptance passes.
[ ] Manual UI photo checklist passes.
[ ] Release gate says ready_for_public_release=true.
[ ] Release package has SHA256SUMS and manifest.
[ ] Flash scripts require explicit port.
[ ] Recovery docs include backup/restore.
[ ] Known limitations are specific and honest.
```

---

## 17. Copy/paste Codex goal prompt

Paste this into GPT-5.5 Codex xhigh:

```text
You are GPT-5.5 Codex xhigh acting as the lead firmware engineer for MeshCore DeskOS D1L.

Read this source-of-truth handoff first:
docs/MeshCore_DeskOS_D1L_Final_Roadmap_and_Codex_Handoff_2026-07-02.md

Project:
- Repo: n30nex/SIGUI
- Branch: feature/meshcore-deskos-d1l
- Firmware target: Seeed SenseCAP Indicator D1L
- Hardware: ESP32-S3 + RP2040, 480x480 touch display, SX1262 LoRa, Wi-Fi/BLE, SD through RP2040 bridge
- Product goal: production-ready public binary release of MeshCore DeskOS D1L

Critical user-tested blockers from latest hardware binary:
1. Give up on-device SD formatting entirely. Users must supply FAT32 SD cards. On boot, if a FAT32 card is present and lacks DeskOS files, create the directory structure and use SD. If no card is present, use NVS fallback. Never format from ESP32, RP2040, serial, scripts, or UI.
2. Fix Home latest-message overlap where sender id collides with multiline message text.
3. Fix Messages screen overlap and crowded header/buttons.
4. Fix Compose Public/DM so bottom dock never covers the keyboard and the screen space is used well.
5. Improve Message Detail: remove message # and uptime from normal view, move developer metadata behind Advanced.
6. Make Nodes list scroll all heard nodes with role-aware actions: DM for companions/contactable nodes, gated Login/Manage for repeaters/room servers only when protocol/auth is implemented.
7. Replace messy Map location picker with lat/lon onscreen keyboard entry.
8. Map page with no tiles must clearly tell users to connect Wi-Fi and download allowed tiles for their area; finish Wi-Fi setup and policy-compliant tile download support.
9. Packets page must scroll at least 100 latest packets and support Load Older; keep 24h packet history on SD and a smaller NVS fallback.
10. Overhaul Settings into an end-user setup dashboard for SD, Wi-Fi, BLE, Radio, Map Tiles, Display, Identity, Diagnostics, About, and Advanced.

Implementation rules:
- Do not hardcode COM ports. Use D1L_PORT or --port.
- Do not add destructive storage behavior anywhere.
- Do not use public OSM tile servers for offline/bulk prefetch unless policy allows it. Use a configurable provider and visible attribution.
- Keep normal mesh messaging fully offline.
- Hide developer jargon from normal UI.
- Use Advanced mode for raw hex, seq, uptime, experimental management, and risky radio edits.
- Run tests after each phase and record exact commands/results.
- Be honest about anything that cannot be validated without hardware.

Workstreams:
A. Storage policy: remove SD format paths, harden FAT32 boot auto-provision, update scripts/docs/tests.
B. UI layout: fix Home/Messages/Detail/Compose, add keyboard modal behavior and overlap tests.
C. Lists/paging: nodes/messages/packets paged query APIs, 100+ packet UI, all heard nodes UI.
D. Wi-Fi/map: scan/connect/status, lat/lon keyboard, map no-tile state, compliant tile downloader/provider config.
E. Settings/docs: setup dashboard, friendly copy, user guide, known limitations, release checklist.
F. QA/release: CI, hardware smoke, scroll probes, SD matrix, RF acceptance, 12h soak, manual photo review, release gate.

Definition of done:
- scripts/release_gate_audit_d1l.py reports ready_for_public_release=true.
- A new user can flash the release package with an explicit COM port.
- UI photos prove no overlap and usable keyboard.
- SD behavior is non-destructive and FAT32-only.
- Wi-Fi/map/packets/settings behavior matches this handoff.
- Release artifacts include checksums, docs, and known limitations.

Start by auditing current code against this document, then implement P0 blockers in the order above.
```

---

## 18. Final notes for release positioning

Suggested release label:

```text
MeshCore DeskOS D1L v1.0.0-rc1
```

Use `v1.0.0` only when:

- no-format SD policy is proven,
- UI hardware photos pass,
- SD/NVS storage matrix passes,
- RF acceptance passes,
- 12-hour soak passes,
- release gate returns true.

Until then, release artifacts should be marked:

```text
developer preview
not for public binary release
```

The project is close, but the current real-device photos show enough UI and setup friction that it should not be published as a public binary until this plan is complete.
