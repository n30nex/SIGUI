#!/usr/bin/env python3
"""Generate deterministic 480x480 UI check screenshots for DeskOS D1L."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Callable

from PIL import Image, ImageDraw, ImageFont


WIDTH = 480
HEIGHT = 480
TOP_BAR_H = 54
HOME_TOP_BAR_H = 18
DOCK_Y = 420
DOCK_H = 60
MIN_TOUCH_TARGET = 44

BG = (8, 13, 20)
SURFACE = (20, 28, 40)
SURFACE_2 = (27, 38, 52)
BORDER = (54, 68, 86)
TEXT = (238, 244, 250)
MUTED = (142, 160, 174)
ACCENT = (77, 219, 204)
GREEN = (167, 243, 208)
AMBER = (251, 191, 36)
RED = (248, 113, 113)
BLUE = (147, 197, 253)
VIOLET = (196, 181, 253)
DIM = (5, 8, 13)
SAMPLE_PUBLIC_KEY = "0BF0A701D5AE2DB660B6ABA17831F883937D290883817CBD1122334455667788"


@dataclass(frozen=True)
class Message:
    source: str
    text: str
    meta: str
    unread: bool = False


@dataclass(frozen=True)
class Node:
    name: str
    fingerprint: str
    role: str
    signal: str
    meta: str


@dataclass(frozen=True)
class Packet:
    kind: str
    direction: str
    meta: str
    note: str
    raw_hex: str


@dataclass(frozen=True)
class Snapshot:
    node_name: str
    fingerprint: str
    radio_profile: str
    mesh_state: str
    rx_total: int
    tx_total: int
    unread_public: int
    unread_dm: int
    ble_transport_supported: bool
    latest_signal: str
    rooms: tuple[Node, ...]
    repeaters: tuple[Node, ...]
    contacts: tuple[Node, ...]
    heard: tuple[Node, ...]
    public_messages: tuple[Message, ...]
    dm_messages: tuple[Message, ...]
    packets: tuple[Packet, ...]
    routes: tuple[Packet, ...]
    storage_state: str
    storage_backend: str
    storage_detail: str
    storage_stores: str
    storage_setup_action: str
    map_tile_backend: str
    map_tile_cache_ready: bool
    map_tile_cache_policy: str
    map_tile_cache_path_template: str
    map_tile_download_state: str
    map_tile_download_requires: str
    map_tile_download_supported: bool
    map_tile_render_supported: bool
    map_tile_sideload_supported: bool
    map_tile_provider_saved: bool
    map_tile_url_template: str
    map_tile_attribution: str
    map_tile_zoom: int
    map_location_set: bool
    map_lat_e7: int
    map_lon_e7: int
    map_center_source: str


def sample_snapshot() -> Snapshot:
    """Return a stable fake mesh snapshot used by CI screenshot checks."""

    room = Node("YKF Room", "937D290883817CBD", "Room Server", "-44 dBm / 29 dB", "last 12s, signed advert")
    bot = Node("YKF Corebot", "0BF0A701D5AE2DB6", "Companion", "-41 dBm / 30 dB", "direct route, public key")
    repeater = Node("Krabs Lagoon", "60B6ABA17831F883", "Repeater", "-52 dBm / 22 dB", "1 hop via Public")
    return Snapshot(
        node_name="D1L Desk",
        fingerprint="60B6ABA17831F883",
        radio_profile="US/CAN 910.525 / BW62.5 / SF7 / CR5",
        mesh_state="ready, listening",
        rx_total=128,
        tx_total=34,
        unread_public=2,
        unread_dm=1,
        ble_transport_supported=False,
        latest_signal="-41 dBm / 30 dB",
        rooms=(room,),
        repeaters=(repeater,),
        contacts=(bot, room),
        heard=(bot, room, repeater),
        public_messages=(
            Message("D1L Desk", "test", "TX queued, seq 31"),
            Message("YKF Corebot", "Public test reply received", "RX new, RSSI -41", True),
            Message("Local Meshcorebot", "test ack on Public", "RX new, 1 hop", True),
        ),
        dm_messages=(
            Message("YKF Corebot", "route is direct", "acked, hash 9A2B"),
            Message("D1L Desk", "desk check", "TX stored", True),
        ),
        packets=(
            Packet("Public", "RX", "RSSI -41 SNR 30 hop 1", "YKF Corebot test reply", "80245100A62F34B9"),
            Packet("Advert", "RX", "RSSI -44 SNR 29 hop 0", "YKF Room signed advert", "C0019880BF9B9B1DD605"),
            Packet("DM", "TX", "ack hash 9A2B direct", "YKF Corebot desk check", "41000BF060B6ABA1"),
            Packet("Retry", "FAIL", "send failed after retry", "route not confirmed", "4100FF00"),
        ),
        routes=(
            Packet("Public route", "RX", "target Public hop 1", "via Krabs Lagoon", ""),
            Packet("DM route", "TX", "target 0BF0A direct", "direct path retained", ""),
        ),
        storage_state="pending bridge",
        storage_backend="NVS fallback",
        storage_detail="RP2040 SD bridge pending",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="bridge_protocol_pending",
        map_tile_backend="unavailable",
        map_tile_cache_ready=False,
        map_tile_cache_policy="sd_offline_cache_when_ready",
        map_tile_cache_path_template="map/tiles/z{z}/x{x}/y{y}.tile",
        map_tile_download_state="provider_required",
        map_tile_download_requires="Connected Wi-Fi, ready SD cache, saved location, allowed provider, attribution, and tile rendering proof",
        map_tile_download_supported=False,
        map_tile_render_supported=False,
        map_tile_sideload_supported=True,
        map_tile_provider_saved=False,
        map_tile_url_template="",
        map_tile_attribution="",
        map_tile_zoom=12,
        map_location_set=False,
        map_lat_e7=0,
        map_lon_e7=0,
        map_center_source="unset",
    )


def large_mesh_snapshot() -> Snapshot:
    """Return an intentionally large fake mesh snapshot for bounded-list stress checks."""

    contacts = tuple(
        Node(
            f"Corebot Contact {i:02d}",
            f"0BF0A701D5AE{i:04X}",
            "Companion",
            f"-{38 + (i % 12)} dBm / {24 + (i % 8)} dB",
            "retained key, direct candidate",
        )
        for i in range(18)
    )
    heard = tuple(
        Node(
            f"Large Mesh Heard Node With Long Name {i:03d}",
            f"937D290883{i:06X}",
            "Room" if i % 5 == 0 else ("Repeater" if i % 5 == 1 else "Chat"),
            f"-{42 + (i % 20)} dBm / {18 + (i % 12)} dB",
            f"{i % 4} hop, signed advert, seen {i + 1}",
        )
        for i in range(96)
    )
    public_messages = tuple(
        Message(
            "Public" if i % 3 else f"Long Alias Sender {i:02d}",
            f"large simulated public message {i:02d} with enough text to truncate safely",
            f"RX seq {200 + i}, RSSI -{40 + (i % 9)}, hop {i % 4}",
            unread=i % 4 == 0,
        )
        for i in range(48)
    )
    dm_messages = tuple(
        Message(
            f"Contact {i:02d}",
            f"dm stress row {i:02d} with ACK/path metadata",
            f"ack {'yes' if i % 2 else 'pending'}, hash {0x9000 + i:X}",
            unread=i % 5 == 0,
        )
        for i in range(32)
    )
    packets = tuple(
        Packet(
            "Public" if i % 2 else "Advert",
            "RX" if i % 3 else "TX",
            f"RSSI -{35 + i % 18} SNR {20 + i % 12} hop {i % 4}",
            f"stress packet row {i:02d}",
            f"{i:02X}" * 16,
        )
        for i in range(128)
    )
    routes = tuple(
        Packet(
            "Route",
            "RX" if i % 2 else "TX",
            f"target {i:02d} hop {i % 5}",
            f"route stress evidence {i:02d}",
            "",
        )
        for i in range(24)
    )

    return Snapshot(
        node_name="D1L Desk Large Mesh",
        fingerprint="60B6ABA17831F883",
        radio_profile="US/CAN 910.525 / BW62.5 / SF7 / CR5",
        mesh_state="ready, 96 heard",
        rx_total=4096,
        tx_total=1024,
        unread_public=12,
        unread_dm=7,
        ble_transport_supported=False,
        latest_signal="-42 dBm / 30 dB",
        rooms=heard[:6],
        repeaters=heard[6:18],
        contacts=contacts,
        heard=heard,
        public_messages=public_messages,
        dm_messages=dm_messages,
        packets=packets,
        routes=routes,
        storage_state="pending bridge",
        storage_backend="NVS fallback",
        storage_detail="RP2040 SD bridge pending",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="bridge_protocol_pending",
        map_tile_backend="unavailable",
        map_tile_cache_ready=False,
        map_tile_cache_policy="sd_offline_cache_when_ready",
        map_tile_cache_path_template="map/tiles/z{z}/x{x}/y{y}.tile",
        map_tile_download_state="provider_required",
        map_tile_download_requires="Connected Wi-Fi, ready SD cache, saved location, allowed provider, attribution, and tile rendering proof",
        map_tile_download_supported=False,
        map_tile_render_supported=False,
        map_tile_sideload_supported=True,
        map_tile_provider_saved=False,
        map_tile_url_template="",
        map_tile_attribution="",
        map_tile_zoom=12,
        map_location_set=False,
        map_lat_e7=0,
        map_lon_e7=0,
        map_center_source="unset",
    )


def storage_no_card_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="no card",
        storage_backend="NVS fallback",
        storage_detail="No SD card reported",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="insert_card",
    )


def storage_needs_fat32_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="needs FAT32",
        storage_backend="NVS fallback",
        storage_detail="Prepare FAT32 on a computer",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="prepare_fat32_on_computer",
    )


def storage_mount_error_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="mount error",
        storage_backend="NVS fallback",
        storage_detail="RP2040 SdFat mount diagnostics",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="inspect_rp2040_sd_mount_error_firmware_path",
    )


def storage_probe_error_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="probe error",
        storage_backend="NVS fallback",
        storage_detail="RP2040 CMD0/CMD8 diagnostics",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="inspect_rp2040_sd_cmd0_firmware_path",
    )


def storage_root_missing_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="root missing",
        storage_backend="NVS fallback",
        storage_detail="DeskOS root missing",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="retry_storage_mount",
    )


def storage_ready_pending_migration_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="ready",
        storage_backend="NVS fallback",
        storage_detail="SD valid, stores pending",
        storage_stores="messages NVS / packets NVS / routes NVS",
        storage_setup_action="store_migration_pending",
    )


def storage_ready_packet_log_sd_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="ready",
        storage_backend="Mixed storage",
        storage_detail="SD packet-log canary",
        storage_stores="messages NVS / packets SD / routes NVS",
        storage_setup_action="packet_log_canary_enabled",
    )


def storage_ready_retained_history_sd_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="ready",
        storage_backend="Mixed storage",
        storage_detail="SD retained-history stores",
        storage_stores="messages SD / packets SD / routes SD",
        storage_setup_action="retained_history_sd_enabled",
    )


def storage_ready_map_tiles_sd_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        storage_state="ready",
        storage_backend="Mixed storage",
        storage_detail="SD retained stores + map cache",
        storage_stores="msg/pkt/route/map SD",
        storage_setup_action="retained_history_sd_enabled",
        map_tile_backend="sd_map_tiles_ready",
        map_tile_cache_ready=True,
        map_tile_download_supported=False,
        map_tile_render_supported=False,
        map_tile_download_state="tile_render_pending",
    )


def manual_location_snapshot() -> Snapshot:
    return replace(
        sample_snapshot(),
        map_location_set=True,
        map_lat_e7=436532000,
        map_lon_e7=-793832000,
        map_center_source="manual",
    )


SCENARIOS: dict[str, Callable[[], Snapshot]] = {
    "default": sample_snapshot,
    "large-mesh": large_mesh_snapshot,
    "storage-states": storage_ready_pending_migration_snapshot,
    "storage-no-card": storage_no_card_snapshot,
    "storage-needs-fat32": storage_needs_fat32_snapshot,
    "storage-mount-error": storage_mount_error_snapshot,
    "storage-probe-error": storage_probe_error_snapshot,
    "storage-root-missing": storage_root_missing_snapshot,
    "storage-ready-pending-migration": storage_ready_pending_migration_snapshot,
    "storage-ready-packet-log-sd": storage_ready_packet_log_sd_snapshot,
    "storage-ready-retained-history-sd": storage_ready_retained_history_sd_snapshot,
    "storage-ready-map-tiles-sd": storage_ready_map_tiles_sd_snapshot,
    "manual-location": manual_location_snapshot,
}


class Surface:
    def __init__(self, view: str):
        self.view = view
        self.image = Image.new("RGB", (WIDTH, HEIGHT), BG)
        self.draw = ImageDraw.Draw(self.image)
        self._fonts: dict[tuple[int, bool], ImageFont.ImageFont] = {}
        self.text_records: list[dict[str, object]] = []
        self.touch_targets: list[dict[str, object]] = []
        self.labels: list[str] = []
        self.metrics: dict[str, int | str | bool] = {}

    def font(self, size: int, bold: bool = False) -> ImageFont.ImageFont:
        key = (size, bold)
        if key in self._fonts:
            return self._fonts[key]

        names = []
        if bold:
            names.extend(
                [
                    "C:/Windows/Fonts/segoeuib.ttf",
                    "C:/Windows/Fonts/arialbd.ttf",
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                ]
            )
        names.extend(
            [
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "DejaVuSans.ttf",
            ]
        )
        for name in names:
            try:
                font = ImageFont.truetype(name, size)
                self._fonts[key] = font
                return font
            except OSError:
                continue

        font = ImageFont.load_default()
        self._fonts[key] = font
        return font

    def rect(self, box: tuple[int, int, int, int], fill: tuple[int, int, int], outline: tuple[int, int, int] | None = None):
        self.draw.rectangle(box, fill=fill, outline=outline)

    def round_rect(
        self,
        box: tuple[int, int, int, int],
        fill: tuple[int, int, int] = SURFACE,
        outline: tuple[int, int, int] = BORDER,
        radius: int = 8,
    ):
        self.draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=1)

    def line(self, points: tuple[tuple[int, int], tuple[int, int]], fill: tuple[int, int, int] = BORDER):
        self.draw.line(points, fill=fill, width=1)

    def touch_target(
        self,
        label: str,
        box: tuple[int, int, int, int],
        *,
        kind: str = "button",
        action: str | None = None,
        destination: str | None = None,
        rf_tx: bool = False,
        public_rf_tx: bool = False,
        dm_tx: bool = False,
        destructive: bool = False,
        formats_sd: bool = False,
    ):
        target = self._minimum_touch_box(box)
        x0, y0, x1, y1 = target
        width = x1 - x0
        height = y1 - y0
        offscreen = x0 < 0 or y0 < 0 or x1 > WIDTH or y1 > HEIGHT
        top_bar_limit = HOME_TOP_BAR_H if self.view == "home" else TOP_BAR_H
        top_bar_overlap = kind not in ("screen", "top_bar") and self.view != "lock_overlay" and y0 < top_bar_limit
        dock_overlap = (
            kind != "dock_tab"
            and self.view not in ("home", "lock_overlay", "onboarding_sheet")
            and y1 > DOCK_Y
        )
        self.touch_targets.append(
            {
                "label": label,
                "kind": kind,
                "action": action or normalize_action(label),
                "destination": destination,
                "visual_box": list(box),
                "box": [x0, y0, x1, y1],
                "center": [(x0 + x1) // 2, (y0 + y1) // 2],
                "width": width,
                "height": height,
                "too_small": width < MIN_TOUCH_TARGET or height < MIN_TOUCH_TARGET,
                "offscreen": offscreen,
                "top_bar_overlap": top_bar_overlap,
                "dock_overlap": dock_overlap,
                "rf_tx": rf_tx or public_rf_tx or dm_tx,
                "public_rf_tx": public_rf_tx,
                "dm_tx": dm_tx,
                "destructive": destructive,
                "formats_sd": formats_sd,
            }
        )

    def _minimum_touch_box(self, box: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
        x0, y0, x1, y1 = box
        width = x1 - x0
        height = y1 - y0
        if width < MIN_TOUCH_TARGET:
            pad = MIN_TOUCH_TARGET - width
            x0 -= pad // 2
            x1 += pad - pad // 2
        if height < MIN_TOUCH_TARGET:
            pad = MIN_TOUCH_TARGET - height
            y0 -= pad // 2
            y1 += pad - pad // 2
        if x0 < 0:
            x1 -= x0
            x0 = 0
        if x1 > WIDTH:
            x0 -= x1 - WIDTH
            x1 = WIDTH
        if y0 < 0:
            y1 -= y0
            y0 = 0
        if y1 > HEIGHT:
            y0 -= y1 - HEIGHT
            y1 = HEIGHT
        return (max(0, x0), max(0, y0), min(WIDTH, x1), min(HEIGHT, y1))

    def text(
        self,
        label: str,
        box: tuple[int, int, int, int],
        size: int = 14,
        color: tuple[int, int, int] = TEXT,
        bold: bool = False,
        align: str = "left",
    ):
        font = self.font(size, bold)
        x0, y0, x1, y1 = box
        max_w = max(1, x1 - x0)
        max_h = max(1, y1 - y0)
        drawn_text, truncated = self._fit(label, font, max_w)
        bbox = self.draw.textbbox((0, 0), drawn_text, font=font)
        text_w = bbox[2] - bbox[0]
        text_h = bbox[3] - bbox[1]
        if align == "center":
            x = x0 + max(0, (max_w - text_w) // 2) - bbox[0]
        elif align == "right":
            x = x1 - text_w - bbox[0]
        else:
            x = x0 - bbox[0]
        y = y0 + max(0, (max_h - text_h) // 2) - bbox[1]
        actual = self.draw.textbbox((x, y), drawn_text, font=font)
        overflow = actual[0] < x0 or actual[1] < y0 - 2 or actual[2] > x1 or actual[3] > y1 + 2
        self.draw.text((x, y), drawn_text, fill=color, font=font)
        self.labels.append(label)
        self.text_records.append(
            {
                "label": label,
                "drawn": drawn_text,
                "box": [x0, y0, x1, y1],
                "actual": [int(actual[0]), int(actual[1]), int(actual[2]), int(actual[3])],
                "truncated": truncated,
                "overflow": overflow,
            }
        )

    def _fit(self, label: str, font: ImageFont.ImageFont, max_w: int) -> tuple[str, bool]:
        if self.draw.textlength(label, font=font) <= max_w:
            return label, False
        suffix = "..."
        shortened = label
        while shortened and self.draw.textlength(shortened + suffix, font=font) > max_w:
            shortened = shortened[:-1]
        return (shortened.rstrip() + suffix if shortened else suffix, True)

    def save(self, path: Path):
        self.image.save(path)

    def summary(self, screenshot: Path, required: tuple[str, ...]) -> dict[str, object]:
        missing = [label for label in required if label not in self.labels]
        touch_issues = [
            target
            for target in self.touch_targets
            if target["too_small"] or target["offscreen"] or target["top_bar_overlap"] or target["dock_overlap"]
        ]
        return {
            "name": self.view,
            "screenshot": screenshot.as_posix(),
            "labels": self.labels,
            "touch_targets": self.touch_targets,
            "touch_target_count": len(self.touch_targets),
            "touch_target_issues": touch_issues,
            "missing_required_labels": missing,
            "truncated_labels": [r for r in self.text_records if r["truncated"]],
            "overflow": [r for r in self.text_records if r["overflow"]],
            "text_count": len(self.text_records),
            "metrics": self.metrics,
        }


def normalize_action(label: str) -> str:
    return "tap_" + "".join(ch.lower() if ch.isalnum() else "_" for ch in label).strip("_")


def format_e7(value: int) -> str:
    sign = "-" if value < 0 else ""
    scaled = abs(value)
    return f"{sign}{scaled // 10_000_000}.{scaled % 10_000_000:07d}"


def role_badge_text(role: str) -> str:
    normalized = role.lower()
    if "room" in normalized:
        return "ROOM"
    if "repeat" in normalized:
        return "RPT"
    if "sensor" in normalized:
        return "SNS"
    if "companion" in normalized:
        return "CMP"
    return "NODE"


def role_badge_color(role: str) -> tuple[int, int, int]:
    normalized = role.lower()
    if "room" in normalized:
        return GREEN
    if "repeat" in normalized:
        return AMBER
    if "sensor" in normalized:
        return VIOLET
    if "companion" in normalized:
        return ACCENT
    return BLUE


def draw_top_bar(s: Surface, snap: Snapshot, *, compact: bool = False):
    if compact:
        s.rect((0, 0, WIDTH, HOME_TOP_BAR_H), (11, 18, 28))
        s.text("DeskOS", (12, 1, 150, 17), 11, TEXT, True)
        s.line(((0, HOME_TOP_BAR_H), (WIDTH, HOME_TOP_BAR_H)))
        return
    s.rect((0, 0, WIDTH, TOP_BAR_H), (11, 18, 28))
    s.text("MeshCore DeskOS", (16, 8, 190, 30), 18, TEXT, True)
    s.text(snap.node_name, (16, 30, 150, 49), 12, MUTED)
    s.text(f"--:--  Mesh {snap.mesh_state}", (202, 10, 464, 28), 12, ACCENT, True, "right")
    s.text(f"Wi-Fi off  BLE off  SD {snap.storage_state}", (202, 31, 464, 49), 11, MUTED, align="right")
    s.line(((0, TOP_BAR_H), (WIDTH, TOP_BAR_H)))


def draw_dock(s: Surface, active: str):
    s.rect((0, DOCK_Y, WIDTH, HEIGHT), (10, 16, 25))
    tabs = [("Home", "Home"), ("Messages", "Msg"), ("Nodes", "Nodes"), ("Map", "Map"), ("Packets", "Pkts"), ("Settings", "Set")]
    w = WIDTH // len(tabs)
    for i, (name, label) in enumerate(tabs):
        x0 = i * w
        x1 = WIDTH if i == len(tabs) - 1 else (i + 1) * w
        active_tab = name == active
        if active_tab:
            s.round_rect((x0 + 8, DOCK_Y + 8, x1 - 8, HEIGHT - 8), (29, 48, 62), (58, 88, 104), 8)
        s.text(label, (x0 + 6, DOCK_Y + 17, x1 - 6, HEIGHT - 15), 13, TEXT if active_tab else MUTED, active_tab, "center")
        s.touch_target(f"{name} tab", (x0, DOCK_Y, x1, HEIGHT), kind="dock_tab", action=f"open_{name.lower()}", destination=name.lower())


def draw_metric(
    s: Surface,
    box: tuple[int, int, int, int],
    title: str,
    value: str,
    detail: str,
    color: tuple[int, int, int] = ACCENT,
    *,
    action: str | None = None,
    destination: str | None = None,
):
    x0, y0, x1, y1 = box
    s.round_rect(box)
    s.text(title, (x0 + 12, y0 + 8, x1 - 12, y0 + 28), 12, MUTED, True)
    s.text(value, (x0 + 12, y0 + 30, x1 - 12, y0 + 57), 20, color, True)
    s.text(detail, (x0 + 12, y1 - 27, x1 - 12, y1 - 8), 11, MUTED)
    if action or destination:
        s.touch_target(title, box, kind="card", action=action, destination=destination)


def draw_button(
    s: Surface,
    box: tuple[int, int, int, int],
    label: str,
    color: tuple[int, int, int] = ACCENT,
    *,
    action: str | None = None,
    destination: str | None = None,
    rf_tx: bool = False,
    public_rf_tx: bool = False,
    dm_tx: bool = False,
    destructive: bool = False,
    formats_sd: bool = False,
):
    s.round_rect(box, (24, 43, 54), (52, 92, 105), 8)
    s.text(label, (box[0] + 8, box[1] + 8, box[2] - 8, box[3] - 8), 14, color, True, "center")
    s.touch_target(
        label,
        box,
        action=action,
        destination=destination,
        rf_tx=rf_tx,
        public_rf_tx=public_rf_tx,
        dm_tx=dm_tx,
        destructive=destructive,
        formats_sd=formats_sd,
    )


def draw_chip(
    s: Surface,
    box: tuple[int, int, int, int],
    title: str,
    value: str,
    color: tuple[int, int, int] = ACCENT,
    *,
    action: str | None = None,
    destination: str | None = None,
):
    x0, y0, x1, y1 = box
    s.round_rect(box, SURFACE_2, BORDER, 8)
    s.text(title, (x0 + 8, y0 + 5, x1 - 8, y0 + 20), 10, MUTED, True)
    s.text(value, (x0 + 8, y0 + 20, x1 - 8, y1 - 5), 12, color, True)
    if action or destination:
        s.touch_target(title, box, kind="chip", action=action, destination=destination)


def draw_launcher_icon(s: Surface, box: tuple[int, int, int, int], icon: str, color: tuple[int, int, int]):
    x0, y0, x1, y1 = box
    cx = (x0 + x1) // 2
    cy = (y0 + y1) // 2
    w = x1 - x0
    h = y1 - y0
    if icon == "chat":
        s.round_rect((cx - 13, cy - 8, cx + 13, cy + 9), (15, 28, 24), color, 3)
        s.line(((cx - 12, cy - 7), (cx, cy + 2)), color)
        s.line(((cx + 12, cy - 7), (cx, cy + 2)), color)
    elif icon == "dm":
        s.rect((cx - 9, cy - 13, cx + 8, cy + 13), color)
        s.rect((cx + 2, cy - 13, cx + 8, cy - 7), (15, 28, 24))
    elif icon == "folder":
        s.round_rect((cx - 16, cy - 5, cx + 17, cy + 12), (15, 28, 24), color, 3)
        s.rect((cx - 15, cy - 12, cx - 3, cy - 5), color)
    elif icon == "phone":
        s.draw.arc((cx - 17, cy - 16, cx + 17, cy + 18), 35, 145, fill=color, width=4)
        s.draw.arc((cx - 17, cy - 16, cx + 17, cy + 18), 215, 325, fill=color, width=4)
    elif icon == "repeat":
        s.draw.arc((cx - 18, cy - 17, cx + 18, cy + 17), 205, 335, fill=color, width=3)
        s.draw.arc((cx - 18, cy - 17, cx + 18, cy + 17), 25, 155, fill=color, width=3)
        s.draw.polygon(((cx + 15, cy + 4), (cx + 23, cy + 4), (cx + 19, cy + 12)), fill=color)
        s.draw.polygon(((cx - 15, cy - 4), (cx - 23, cy - 4), (cx - 19, cy - 12)), fill=color)
    elif icon == "bell":
        s.draw.arc((cx - 15, cy - 15, cx + 15, cy + 18), 205, 335, fill=color, width=4)
        s.line(((cx - 14, cy + 10), (cx + 14, cy + 10)), color)
        s.draw.ellipse((cx - 3, cy + 13, cx + 3, cy + 19), fill=color)
    elif icon == "map":
        s.draw.polygon(((cx + 14, cy - 17), (cx - 10, cy + 4), (cx - 1, cy + 8), (cx - 7, cy + 18)), fill=color)
    elif icon == "terminal":
        s.round_rect((cx - 18, cy - 14, cx + 18, cy + 15), (15, 28, 24), color, 3)
        s.line(((cx - 12, cy - 4), (cx - 5, cy + 2)), color)
        s.line(((cx - 12, cy + 8), (cx - 5, cy + 2)), color)
        s.line(((cx + 1, cy + 8), (cx + 11, cy + 8)), color)
    elif icon == "packets":
        for i in range(3):
            y = cy - 13 + i * 11
            s.rect((cx - 15, y, cx - 9, y + 6), color)
            s.line(((cx - 4, y + 3), (cx + 16, y + 3)), color)
    elif icon == "settings":
        s.draw.ellipse((cx - 13, cy - 13, cx + 13, cy + 13), outline=color, width=4)
        s.draw.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill=color)
        s.line(((cx, cy - 20), (cx, cy - 14)), color)
        s.line(((cx, cy + 14), (cx, cy + 20)), color)
        s.line(((cx - 20, cy), (cx - 14, cy)), color)
        s.line(((cx + 14, cy), (cx + 20, cy)), color)
    elif icon == "setup":
        s.draw.polygon(((cx - 17, cy + 2), (cx, cy - 14), (cx + 17, cy + 2)), outline=color)
        s.rect((cx - 12, cy + 2, cx + 12, cy + 17), color)
        s.rect((cx - 4, cy + 8, cx + 4, cy + 17), (15, 28, 24))
    elif icon == "signal":
        for i in range(4):
            s.rect((cx - 18 + i * 10, cy + 13 - i * 8, cx - 12 + i * 10, cy + 15), color)
    elif icon == "time":
        s.draw.ellipse((cx - 13, cy - 13, cx + 13, cy + 13), outline=color, width=3)
        s.line(((cx, cy), (cx, cy - 8)), color)
        s.line(((cx, cy), (cx + 7, cy + 4)), color)
    elif icon == "wifi":
        s.draw.arc((cx - 20, cy - 16, cx + 20, cy + 24), 220, 320, fill=color, width=3)
        s.draw.arc((cx - 13, cy - 9, cx + 13, cy + 17), 220, 320, fill=color, width=3)
        s.draw.ellipse((cx - 3, cy + 7, cx + 3, cy + 13), fill=color)
    elif icon == "ble":
        span = min(15, max(8, h // 2 - 2))
        arm = min(11, max(7, w // 5))
        upper = max(4, span // 2)
        lower = max(5, span // 2 + 1)
        s.line(((cx, cy - span), (cx, cy + span)), color)
        s.line(((cx, cy - span), (cx + arm, cy - upper)), color)
        s.line(((cx + arm, cy - upper), (cx, cy + 1)), color)
        s.line(((cx, cy + 1), (cx + arm, cy + lower)), color)
        s.line(((cx + arm, cy + lower), (cx, cy + span)), color)
        s.line(((cx - arm, cy - lower), (cx + arm, cy + lower)), color)
        s.line(((cx - arm, cy + lower), (cx + arm, cy - upper)), color)
    elif icon == "sd":
        s.draw.polygon(((cx - 10, cy - 15), (cx + 10, cy - 15), (cx + 14, cy - 10), (cx + 14, cy + 15), (cx - 14, cy + 15), (cx - 14, cy - 11)), fill=color)
        s.rect((cx - 6, cy - 9, cx - 2, cy - 3), (15, 28, 24))
        s.rect((cx + 2, cy - 9, cx + 6, cy - 3), (15, 28, 24))
    else:
        s.text(icon[:3].upper(), (x0, y0, x1, y1), min(w, h) // 3, color, True, "center")


def draw_launcher_tile(
    s: Surface,
    box: tuple[int, int, int, int],
    icon: str,
    label: str,
    detail: str,
    color: tuple[int, int, int],
    *,
    action: str | None = None,
    destination: str | None = None,
):
    x0, y0, x1, y1 = box
    s.round_rect(box, (13, 23, 18), (31, 55, 46), 4)
    draw_launcher_icon(s, (x0 + 22, y0 + 12, x1 - 22, y0 + 62), icon, color)
    s.text(label, (x0 + 5, y0 + 70, x1 - 5, y0 + 92), 13, TEXT, True, "center")
    s.text(detail, (x0 + 5, y0 + 96, x1 - 5, y1 - 7), 10, MUTED, False, "center")
    s.touch_target(label, box, kind="launcher_tile", action=action, destination=destination)


def draw_status_icon(
    s: Surface,
    box: tuple[int, int, int, int],
    icon: str,
    label: str,
    color: tuple[int, int, int],
    *,
    action: str | None = None,
    destination: str | None = None,
):
    x0, y0, x1, y1 = box
    s.round_rect(box, (13, 23, 18), color, 4)
    draw_launcher_icon(s, (x0 + 34, y0 + 3, x1 - 34, y0 + 29), icon, color)
    s.text(label, (x0 + 5, y0 + 28, x1 - 5, y1 - 3), 10, color, True, "center")
    if action or destination:
        s.touch_target(label, box, kind="status_icon", action=action, destination=destination)


def draw_settings_group(
    s: Surface,
    box: tuple[int, int, int, int],
    title: str,
    value: str,
    detail: str,
    color: tuple[int, int, int] = ACCENT,
    *,
    action: str | None = None,
    destination: str | None = None,
):
    x0, y0, x1, y1 = box
    s.round_rect(box, SURFACE, BORDER, 8)
    s.text(title, (x0 + 8, y0 + 5, x0 + 100, y0 + 24), 11, color, True)
    s.text(value, (x0 + 104, y0 + 5, x1 - 8, y0 + 24), 11, TEXT, True, "right")
    s.text(detail, (x0 + 8, y0 + 28, x1 - 8, y1 - 6), 10, MUTED)
    if action or destination:
        s.touch_target(title, box, kind="settings_group", action=action, destination=destination)


def draw_fake_qr(s: Surface, box: tuple[int, int, int, int]):
    x0, y0, x1, y1 = box
    s.rect(box, (248, 250, 252), (248, 250, 252))
    modules = 25
    cell = max(1, (x1 - x0) // modules)
    used = cell * modules
    ox = x0 + ((x1 - x0) - used) // 2
    oy = y0 + ((y1 - y0) - used) // 2

    def finder(cx: int, cy: int):
        s.rect((ox + cx * cell, oy + cy * cell, ox + (cx + 7) * cell, oy + (cy + 7) * cell), (2, 6, 10))
        s.rect((ox + (cx + 1) * cell, oy + (cy + 1) * cell, ox + (cx + 6) * cell, oy + (cy + 6) * cell), (248, 250, 252))
        s.rect((ox + (cx + 2) * cell, oy + (cy + 2) * cell, ox + (cx + 5) * cell, oy + (cy + 5) * cell), (2, 6, 10))

    finder(1, 1)
    finder(17, 1)
    finder(1, 17)
    for y in range(1, modules - 1):
        for x in range(1, modules - 1):
            in_finder = (x < 8 and y < 8) or (x >= 17 and y < 8) or (x < 8 and y >= 17)
            if in_finder:
                continue
            if ((x * 7 + y * 11 + x * y) % 5) in (0, 2):
                s.rect((ox + x * cell, oy + y * cell, ox + (x + 1) * cell, oy + (y + 1) * cell), (2, 6, 10))


def draw_row(
    s: Surface,
    box: tuple[int, int, int, int],
    title: str,
    detail: str,
    badge: str | None = None,
    *,
    badge_color: tuple[int, int, int] = GREEN,
    target_label: str | None = None,
    action: str | None = None,
    destination: str | None = None,
):
    x0, y0, x1, y1 = box
    s.round_rect(box, SURFACE_2, BORDER, 8)
    if target_label or action or destination:
        s.touch_target(target_label or title, box, kind="row", action=action, destination=destination)
    row_h = y1 - y0
    title_right = x1 - 70 if badge else x1 - 10
    if row_h >= 38:
        s.text(title, (x0 + 10, y0 + 5, title_right, y0 + 24), 13, TEXT, True)
        s.text(detail, (x0 + 10, y0 + 24, x1 - 10, y1 - 5), 11, MUTED)
    elif row_h >= 30:
        s.text(title, (x0 + 10, y0 + 3, title_right, y0 + 17), 11, TEXT, True)
        s.text(detail, (x0 + 10, y0 + 17, x1 - 10, y1 - 3), 9, MUTED)
    else:
        s.text(title, (x0 + 10, y0 + 3, title_right, y1 - 3), 10, TEXT, True)
    if badge:
        s.round_rect((x1 - 62, y0 + 9, x1 - 10, y0 + 31), (22, 39, 49), badge_color, 8)
        s.text(badge, (x1 - 58, y0 + 10, x1 - 14, y0 + 30), 11, badge_color, True, "center")


def draw_home_body(s: Surface, snap: Snapshot):
    tiles = (
        ((6, 22, 120, 148), "chat", "Chats", f"{snap.unread_public} new", AMBER if snap.unread_public else ACCENT, "open_messages_public", "messages"),
        ((124, 22, 238, 148), "dm", "DMs", f"{snap.unread_dm} new", AMBER if snap.unread_dm else GREEN, "open_messages_dm", "messages_dm"),
        ((242, 22, 356, 148), "folder", "Rooms", f"{len(snap.rooms)} seen", GREEN if snap.rooms else MUTED, "open_mesh_roles", "mesh_roles_sheet"),
        ((360, 22, 474, 148), "phone", "Contacts", f"{len(snap.contacts)} saved", GREEN if snap.contacts else MUTED, "open_nodes", "nodes"),
        ((6, 158, 120, 284), "repeat", "Repeaters", f"{len(snap.repeaters)} heard", AMBER if snap.repeaters else MUTED, "open_repeaters", "mesh_roles_sheet"),
        ((124, 158, 238, 284), "bell", "Advertise", "manual", ACCENT, "open_advert_sheet", "advert_sheet"),
        ((242, 158, 356, 284), "map", "Map", "tiles" if snap.map_tile_cache_ready else "offline", GREEN if snap.map_tile_cache_ready else ACCENT, "open_map", "map"),
        ((360, 158, 474, 284), "terminal", "Terminal", "diagnose", VIOLET, "open_diagnostics", "diagnostics_sheet"),
        ((6, 294, 120, 420), "packets", "Packets", f"{len(snap.packets)} rows", GREEN if snap.packets else MUTED, "open_packets", "packets"),
        ((124, 294, 238, 420), "settings", "Settings", "setup", ACCENT, "open_settings", "settings"),
        ((242, 294, 356, 420), "setup", "Setup", snap.storage_state, GREEN if snap.storage_backend != "NVS fallback" else AMBER, "open_storage_setup", "storage_setup_sheet"),
        ((360, 294, 474, 420), "signal", "Signal", snap.latest_signal.split()[0] if snap.latest_signal else "waiting", GREEN if snap.latest_signal else MUTED, "open_signal", "mesh_roles_sheet"),
    )
    for box, icon, label, detail, color, action, destination in tiles:
        draw_launcher_tile(s, box, icon, label, detail, color, action=action, destination=destination)

    draw_status_icon(s, (6, 434, 120, 478), "time", "Time", ACCENT)
    draw_status_icon(s, (124, 434, 238, 478), "wifi", "Wi-Fi", ACCENT, action="open_wifi_settings", destination="wifi_setup_sheet")
    draw_status_icon(s, (242, 434, 356, 478), "ble", "BLE", VIOLET, action="open_ble_settings", destination="ble_setup_sheet")
    draw_status_icon(s, (360, 434, 474, 478), "sd", "SD", GREEN if snap.storage_backend != "NVS fallback" else AMBER, action="open_storage_setup", destination="storage_setup_sheet")


def render_home(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap, compact=True)
    draw_home_body(s, snap)


def render_messages_mode(s: Surface, snap: Snapshot, *, show_dms: bool = False):
    draw_top_bar(s, snap)
    s.round_rect((16, 64, 464, 172), SURFACE, BORDER, 8)
    s.text("Messages", (28, 72, 210, 98), 22, TEXT, True)
    s.text(
        f"public {len(snap.public_messages)} new {snap.unread_public}  dm {len(snap.dm_messages)} new {snap.unread_dm}",
        (28, 102, 452, 120),
        11,
        MUTED,
        True,
    )
    draw_button(s, (28, 126, 82, 166), "Read", GREEN, action="mark_messages_read")
    draw_button(s, (90, 126, 178, 166), "Compose", ACCENT, action="open_public_compose", destination="compose_sheet")
    draw_button(s, (186, 126, 266, 166), "History", BLUE, action="open_public_history", destination="public_history_sheet")
    draw_button(s, (274, 126, 338, 166), "Test", AMBER, action="send_public_test", public_rf_tx=True)
    draw_button(s, (16, 184, 126, 224), "Public", GREEN if not show_dms else MUTED, action="open_messages_public", destination="messages")
    draw_button(s, (138, 184, 236, 224), "DMs", GREEN if show_dms else MUTED, action="open_messages_dm", destination="messages_dm")
    if show_dms:
        render_messages_dm_list(s, snap)
    else:
        render_messages_public_list(s, snap)
    draw_dock(s, "Messages")


def render_messages_public_list(s: Surface, snap: Snapshot):
    s.text("Public Channel", (28, 236, 230, 256), 14, MUTED, True)
    y = 264
    public_rendered = 0
    for msg in snap.public_messages:
        if y + 58 > DOCK_Y:
            break
        s.round_rect((28, y, 452, y + 58), SURFACE, BORDER, 8)
        s.text(msg.source, (40, y + 6, 300, y + 24), 12, AMBER if msg.unread else ACCENT, True)
        s.text("new" if msg.unread else "received", (330, y + 6, 440, y + 24), 11, MUTED if not msg.unread else AMBER, True, "right")
        s.text(msg.text, (40, y + 26, 440, y + 42), 12, TEXT, True)
        s.text(msg.meta, (40, y + 42, 440, y + 56), 10, MUTED, True)
        s.touch_target(
            f"Public row {msg.source}",
            (28, y, 452, y + 58),
            kind="row",
            action="open_message_detail",
            destination="message_detail_sheet",
        )
        y += 66
        public_rendered += 1
    s.metrics.update(
        {
            "messages_mode": "public",
            "public_source_count": len(snap.public_messages),
            "public_rendered_count": public_rendered,
            "dm_source_count": len(snap.dm_messages),
            "dm_rendered_count": 0,
        }
    )


def render_messages_dm_list(s: Surface, snap: Snapshot):
    s.text("DM Conversations", (28, 236, 250, 256), 14, MUTED, True)
    y = 264
    dm_rendered = 0
    for msg in snap.dm_messages:
        if y + 58 > DOCK_Y:
            break
        s.round_rect((28, y, 452, y + 58), SURFACE, BORDER, 8)
        s.text(msg.source, (40, y + 6, 300, y + 24), 12, AMBER if msg.unread else GREEN, True)
        s.text("new" if msg.unread else "received", (330, y + 6, 440, y + 24), 11, MUTED if not msg.unread else AMBER, True, "right")
        s.text(msg.text, (40, y + 26, 440, y + 42), 12, TEXT, True)
        s.text(msg.meta, (40, y + 42, 440, y + 56), 10, MUTED, True)
        s.touch_target(
            f"DM row {msg.source}",
            (28, y, 452, y + 58),
            kind="row",
            action="open_dm_thread",
            destination="dm_thread_sheet",
        )
        y += 66
        dm_rendered += 1
    s.metrics.update(
        {
            "messages_mode": "dms",
            "public_source_count": len(snap.public_messages),
            "public_rendered_count": 0,
            "dm_source_count": len(snap.dm_messages),
            "dm_rendered_count": dm_rendered,
        }
    )


def render_messages(s: Surface, snap: Snapshot):
    render_messages_mode(s, snap, show_dms=False)


def render_messages_dm(s: Surface, snap: Snapshot):
    render_messages_mode(s, snap, show_dms=True)


def render_nodes(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.text("Nodes", (16, 64, 150, 92), 22, TEXT, True)
    s.round_rect((16, 104, 464, 228))
    s.text("Contacts", (28, 112, 180, 134), 14, MUTED, True)
    y = 140
    contacts_rendered = 0
    for node in snap.contacts:
        if y + 34 > 220:
            break
        draw_row(
            s,
            (28, y, 374, y + 34),
            node.name,
            f"{node.fingerprint}  {node.signal}",
            role_badge_text(node.role),
            badge_color=role_badge_color(node.role),
            target_label=f"Contact row {node.name}",
            action="open_contact_detail",
            destination="contact_detail_sheet",
        )
        draw_button(s, (384, y, 452, y + 34), "DM", GREEN, action="open_dm_compose", destination="compose_sheet")
        y += 40
        contacts_rendered += 1
    s.round_rect((16, 240, 464, 416))
    s.text("Heard Nodes", (28, 248, 180, 270), 14, MUTED, True)
    s.text("All Heard", (306, 248, 452, 270), 12, MUTED, True, "right")
    y = 276
    heard_rendered = 0
    for node in snap.heard:
        if y + 44 > 416:
            break
        draw_row(
            s,
            (28, y, 452, y + 44),
            node.name,
            f"{node.meta}  {node.signal}",
            role_badge_text(node.role),
            badge_color=role_badge_color(node.role),
            target_label=f"Heard node {node.name}",
            action="open_node_detail",
            destination="node_detail_sheet",
        )
        y += 48
        heard_rendered += 1
    s.metrics.update(
        {
            "contacts_source_count": len(snap.contacts),
            "contacts_rendered_count": contacts_rendered,
            "heard_source_count": len(snap.heard),
            "heard_query_count": len(snap.heard),
            "heard_rendered_count": heard_rendered,
        }
    )
    draw_dock(s, "Nodes")


def render_map(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.text("Map", (16, 64, 150, 92), 22, TEXT, True)
    draw_button(s, (250, 62, 336, 102), "Tiles", BLUE, action="open_map_tiles", destination="map_tiles_sheet")
    draw_button(
        s,
        (344, 62, 456, 102),
        "Move Pin" if snap.map_location_set else "Set Pin",
        GREEN,
        action="open_map_location_picker",
        destination="map_location_sheet",
    )
    draw_metric(
        s,
        (16, 104, 230, 176),
        "Tile Cache",
        "SD Ready" if snap.map_tile_cache_ready else "Offline",
        snap.map_tile_backend,
        GREEN if snap.map_tile_cache_ready else AMBER,
    )
    draw_metric(
        s,
        (250, 104, 464, 176),
        "Downloads",
        "Ready" if snap.map_tile_download_supported else (
            "Setup" if snap.map_tile_render_supported else "Unavailable"
        ),
        snap.map_tile_download_state,
        BLUE,
    )
    s.round_rect((16, 190, 464, 276))
    if snap.map_tile_cache_ready:
        s.text("Offline Cache", (28, 198, 180, 220), 14, MUTED, True)
        s.text(snap.map_tile_cache_policy, (28, 224, 452, 246), 16, TEXT, True)
        s.text(snap.map_tile_cache_path_template, (28, 250, 452, 270), 11, MUTED)
    else:
        s.text("No Offline Tiles", (28, 198, 220, 220), 14, MUTED, True)
        s.text("Connect Wi-Fi and download allowed tiles for your area.", (28, 224, 452, 246), 13, TEXT, True)
        s.text("Allowed provider and visible attribution required.", (28, 250, 452, 270), 11, MUTED)
    s.round_rect((16, 290, 464, 402))
    s.text("Center", (28, 298, 160, 320), 14, MUTED, True)
    s.text("Routes", (330, 298, 452, 320), 14, MUTED, True)
    if snap.map_location_set:
        s.text("Manual", (28, 322, 122, 346), 16, TEXT, True)
        s.text(
            f"{format_e7(snap.map_lat_e7)}, {format_e7(snap.map_lon_e7)}",
            (128, 322, 452, 346),
            15,
            TEXT,
            True,
        )
    else:
        s.text("Unset", (28, 322, 452, 346), 16, AMBER, True)
    s.text(
        f"Routes {len(snap.routes)}  heard {len(snap.heard)} nodes",
        (28, 350, 452, 372),
        11,
        MUTED,
    )
    s.text("Connect Wi-Fi, choose an allowed provider, download only your area", (28, 374, 452, 394), 11, AMBER, True)
    s.metrics.update(
        {
            "map_tile_cache_ready": snap.map_tile_cache_ready,
            "map_tile_download_supported": snap.map_tile_download_supported,
            "map_tile_render_supported": snap.map_tile_render_supported,
            "map_location_set": snap.map_location_set,
            "map_center_source": snap.map_center_source,
            "map_center_lat_e7": snap.map_lat_e7,
            "map_center_lon_e7": snap.map_lon_e7,
            "map_route_count": len(snap.routes),
        }
    )
    draw_dock(s, "Map")


def render_map_location_sheet(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.rect((0, TOP_BAR_H, WIDTH, HEIGHT), (17, 25, 35))
    lat = snap.map_lat_e7 if snap.map_location_set else 436532000
    lon = snap.map_lon_e7 if snap.map_location_set else -793832000
    s.text("Set D1L Location", (28, 70, 240, 100), 22, TEXT, True)
    draw_button(s, (238, 66, 300, 106), "Save", GREEN, action="save_map_location", destination="map")
    draw_button(s, (308, 66, 378, 106), "Clear", AMBER, action="clear_d1l_pin", destination="map")
    draw_button(s, (386, 66, 456, 106), "Skip", MUTED, action="skip_map_location", destination="map")
    s.text("Map needs your D1L location", (28, 112, 452, 134), 13, GREEN, True)
    s.text("Enter decimal degrees", (28, 134, 452, 156), 13, MUTED)

    s.text("Latitude", (28, 160, 220, 182), 13, TEXT, True)
    s.round_rect((16, 184, 464, 228), SURFACE_2, BORDER, 8)
    s.touch_target("Latitude", (16, 184, 464, 228), kind="text_field", action="edit_map_latitude")
    s.text(format_e7(lat), (28, 194, 452, 222), 17, TEXT)

    s.text("Longitude", (28, 236, 220, 258), 13, TEXT, True)
    s.round_rect((16, 260, 464, 304), SURFACE_2, BORDER, 8)
    s.touch_target("Longitude", (16, 260, 464, 304), kind="text_field", action="edit_map_longitude")
    s.text(format_e7(lon), (28, 270, 452, 298), 17, TEXT)

    s.round_rect((16, 312, 464, 468), (10, 16, 24), BORDER, 8)
    s.text("Keyboard", (28, 322, 452, 344), 13, MUTED, True)
    keyboard_rows = ("1 2 3 4 5 6 7 8 9 0", "- . backspace", "ready     cancel")
    y = 352
    for row in keyboard_rows:
        s.text(row, (32, y, 448, y + 30), 17, TEXT, False, "center")
        y += 36


def render_map_tiles_sheet(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.rect((0, TOP_BAR_H, WIDTH, HEIGHT), (17, 25, 35))
    s.text("Map Tiles", (28, 70, 230, 100), 22, TEXT, True)
    draw_button(s, (392, 66, 464, 106), "Close", MUTED, action="close_map_tiles", destination="map")
    s.text(
        f"Cache {'ready' if snap.map_tile_cache_ready else 'needs SD'}  Wi-Fi needed  Provider {'saved' if snap.map_tile_provider_saved else 'needed'}",
        (28, 112, 452, 134),
        12,
        MUTED,
        True,
    )

    s.text("Allowed provider template", (28, 148, 452, 170), 13, GREEN, True)
    s.round_rect((16, 172, 464, 212), SURFACE_2, BORDER, 8)
    s.touch_target("Allowed provider template", (16, 172, 464, 212), kind="text_field", action="edit_map_tile_provider")
    s.text(snap.map_tile_url_template or "https://provider.example/{z}/{x}/{y}.png", (28, 181, 452, 206), 12, TEXT, True)

    s.text("Attribution", (28, 220, 452, 242), 13, GREEN, True)
    s.round_rect((16, 244, 464, 284), SURFACE_2, BORDER, 8)
    s.touch_target("Attribution", (16, 244, 464, 284), kind="text_field", action="edit_map_tile_attribution")
    s.text(snap.map_tile_attribution or "Provider attribution", (28, 253, 452, 278), 13, TEXT, True)

    s.text(f"Zoom {snap.map_tile_zoom}", (28, 302, 108, 326), 14, TEXT, True)
    draw_button(s, (116, 294, 170, 330), "Z-", ACCENT, action="map_tile_zoom_down")
    draw_button(s, (178, 294, 232, 330), "Z+", ACCENT, action="map_tile_zoom_up")
    draw_button(s, (244, 294, 306, 330), "Save", GREEN, action="save_map_tile_provider")
    draw_button(s, (314, 294, 380, 330), "Clear", AMBER, action="clear_map_tile_provider")
    if snap.map_tile_download_supported:
        draw_button(
            s,
            (16, 340, 128, 378),
            "Download",
            BLUE,
            action="download_center_tile",
            destination=None,
            public_rf_tx=False,
            formats_sd=False,
        )
        s.text("Downloads one center tile for your saved D1L location.", (142, 340, 464, 362), 12, TEXT, True)
    else:
        s.text("Live download unavailable", (28, 340, 214, 362), 12, AMBER, True)
        s.text("Tile render pending; use serial canaries for SD cache proof.", (218, 340, 464, 362), 11, TEXT, True)
    s.text("No public OSM bulk tile servers. Visible attribution is required.", (28, 382, 452, 408), 11, AMBER, True)

    s.round_rect((16, 416, 464, 470), (10, 16, 24), BORDER, 8)
    s.text("Keyboard", (28, 422, 452, 444), 13, MUTED, True)
    s.text("{z} {x} {y}  ready  cancel", (32, 444, 448, 468), 14, TEXT, False, "center")


def render_packets(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.text("Packets", (16, 64, 150, 92), 22, TEXT, True)
    s.round_rect((16, 100, 464, 148), (5, 12, 19), BORDER, 8)
    s.text("live tail  rssi -41  snr 30  avg -46", (28, 108, 452, 128), 12, GREEN, True)
    s.text("rooms 1  repeaters 1  samples 128", (28, 128, 452, 146), 11, MUTED)
    for i, label in enumerate(("All", "RX", "TX", "Text")):
        draw_button(
            s,
            (16 + i * 64, 160, 72 + i * 64, 194),
            label,
            GREEN if label == "All" else ACCENT,
            action=f"packet_filter_{label.lower()}",
        )
    draw_button(s, (286, 160, 366, 194), "Search", BLUE, action="open_packet_search", destination="packet_search_sheet")
    draw_button(s, (376, 160, 464, 194), "Pause", AMBER, action="pause_packet_feed")
    s.text("find raw/test", (28, 200, 464, 218), 11, AMBER)
    s.text("Packet Feed", (28, 224, 180, 244), 14, MUTED, True)
    packet_query_limit = min(len(snap.packets), 100)
    s.text(f"page 1-{packet_query_limit}/{len(snap.packets)} SD", (220, 224, 452, 244), 11, MUTED, True, "right")
    y = 236

    def packet_color(packet: Packet) -> tuple[int, int, int]:
        fields = f"{packet.kind} {packet.direction} {packet.note}".lower()
        if "fail" in fields or "error" in fields:
            return RED
        if packet.direction.upper() == "TX":
            return BLUE
        if packet.direction.upper() == "RX":
            return GREEN
        return AMBER

    visible_packets = snap.packets[: 2 if len(snap.packets) > packet_query_limit else 3]
    for packet in visible_packets:
        color = packet_color(packet)
        s.round_rect((16, y, 464, y + 40), (5, 12, 19), color, 8)
        s.rect((24, y + 8, 28, y + 32), color)
        draw_row(
            s,
            (32, y + 2, 452, y + 40),
            f"{packet.direction} {packet.kind}",
            f"{packet.meta}  {packet.note}",
            target_label=f"Packet row {packet.kind} {packet.direction}",
            action="open_packet_detail",
            destination="packet_detail_sheet",
        )
        y += 44
    if len(snap.packets) > packet_query_limit:
        draw_button(s, (16, y + 4, 146, y + 44), "Load Older", BLUE, action="load_older_packets")
        y += 50
    draw_button(s, (16, 420 - 44, 146, 420 - 8), "Mesh Roles", GREEN, action="open_mesh_roles", destination="mesh_roles_sheet")
    s.text("Routes", (166, 382, 260, 402), 14, MUTED, True)
    for route in snap.routes[:1]:
        draw_row(
            s,
            (236, 374, 464, 410),
            f"{route.kind} {route.direction}",
            f"{route.meta}  {route.note}",
            target_label=f"Route row {route.kind} {route.direction}",
            action="open_route_detail",
            destination="route_detail_sheet",
        )
    draw_dock(s, "Packets")
    s.metrics.update(
        {
            "packet_source_count": len(snap.packets),
            "packet_query_limit": packet_query_limit,
            "packet_total_matches": len(snap.packets),
            "packet_sd_history_page": True,
            "packet_rendered_count": len(visible_packets),
            "packet_load_older_available": len(snap.packets) > packet_query_limit,
        }
    )


def render_settings(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.text("Settings", (16, 64, 150, 92), 22, TEXT, True)
    s.text("Setup Dashboard", (18, 92, 260, 110), 12, MUTED)

    storage_value = "Ready" if snap.storage_state == "ready" else ("Needs FAT32" if "fat32" in snap.storage_setup_action else "NVS fallback")
    map_value = "Tiles ready" if snap.map_tile_cache_ready else "Setup"
    rows = (
        ((16, 116, 236, 170), "SD Card", storage_value, "FAT32 only, no format", AMBER, "open_storage_setup", "storage_setup_sheet"),
        ((244, 116, 464, 170), "Wi-Fi", "off", "Scan/connect, mesh offline", GREEN, "open_wifi_settings", "wifi_setup_sheet"),
        ((16, 178, 236, 232), "BLE", "off", "Companion pairing gated", MUTED, "open_ble_settings", "ble_setup_sheet"),
        ((244, 178, 464, 232), "Radio", "Mesh profile", "US/CAN profile", BLUE, "open_radio_settings", "radio_settings_sheet"),
        ((16, 240, 236, 294), "Map Tiles", map_value, "Wi-Fi and allowed provider", AMBER, "open_map_tiles", "map_tiles_sheet"),
        ((244, 240, 464, 294), "Display", "Backlight", "Brightness, night, contrast", GREEN, "open_display_settings", "display_settings_sheet"),
        ((16, 302, 236, 356), "Identity", "Ready", snap.fingerprint, GREEN, None, None),
        ((244, 302, 464, 356), "Diagnostics", "Health", "Crashlog, exports, soak", VIOLET, "open_diagnostics", "diagnostics_sheet"),
        ((16, 364, 236, 418), "About", snap.node_name, "DeskOS D1L 0.1.0-phase1", MUTED, None, None),
        ((244, 364, 464, 418), "Advanced", "Hidden tools", "Raw data and adverts", RED, "open_advert_sheet", "advert_sheet"),
    )
    for box, title, value, detail, color, action, destination in rows:
        draw_settings_group(s, box, title, value, detail, color, action=action, destination=destination)
    draw_dock(s, "Settings")


def draw_sheet_frame(s: Surface, title: str, subtitle: str | None = None):
    draw_top_bar(s, sample_snapshot())
    draw_home_body(s, sample_snapshot())
    s.touch_targets.clear()
    s.rect((0, TOP_BAR_H, WIDTH, DOCK_Y), DIM)
    s.round_rect((24, 78, 456, 392), (18, 27, 39), (72, 92, 112), 8)
    s.text(title, (44, 94, 330, 122), 22, TEXT, True)
    if subtitle:
        s.text(subtitle, (44, 124, 436, 146), 12, MUTED)


def render_compose_sheet(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.rect((0, TOP_BAR_H, WIDTH, HEIGHT), (17, 25, 35))
    s.text("Compose Public", (16, 64, 240, 96), 22, TEXT, True)
    draw_button(s, (252, 64, 314, 104), "Send", GREEN, action="send_public_text", public_rf_tx=True)
    draw_button(s, (322, 64, 384, 104), "Clear", ACCENT, action="clear_public_message")
    draw_button(s, (392, 64, 464, 104), "Close", MUTED, action="close_compose", destination="messages")
    s.round_rect((16, 114, 464, 192), SURFACE_2, BORDER, 8)
    s.touch_target("Public message", (16, 114, 464, 192), kind="text_field", action="edit_public_message")
    s.text("Public message", (28, 122, 220, 144), 13, MUTED, True)
    s.text("test from DeskOS D1L", (28, 150, 452, 188), 18, TEXT)
    s.text("20/138", (374, 196, 464, 218), 12, MUTED, True, "right")
    s.text("Keyboard", (28, 196, 180, 216), 13, MUTED, True)
    s.round_rect((16, 214, 464, 472), (10, 16, 24), BORDER, 8)
    keyboard_rows = (
        ("q", "w", "e", "r", "t", "y", "u", "i", "o", "p"),
        ("a", "s", "d", "f", "g", "h", "j", "k", "l"),
        ("z", "x", "c", "v", "b", "n", "m", ".", "?"),
        ("1#", "ABC", ",", "-", "space", "Bksp", "OK"),
    )
    y = 224
    min_key = 999
    for row in keyboard_rows:
        gap = 4
        x = 22
        fixed_space = 120 if "space" in row else 0
        available = 436 - gap * (len(row) - 1) - fixed_space
        variable_keys = len(row) - (1 if fixed_space else 0)
        key_w = available // variable_keys
        for label in row:
            width = key_w
            if label == "space":
                width = fixed_space
            s.round_rect((x, y, x + width, y + 52), SURFACE_2, BORDER, 6)
            s.text(label, (x + 2, y + 14, x + width - 2, y + 42), 15, TEXT, False, "center")
            min_key = min(min_key, width, 52)
            x += width + gap
        y += 60
    s.metrics.update({"compose_keyboard_rows": 4, "compose_keyboard_min_key_px": min_key})


def render_public_history_sheet(s: Surface, snap: Snapshot):
    public_page_limit = 5
    load_older_available = len(snap.public_messages) > public_page_limit
    draw_sheet_frame(
        s,
        "Public History",
        f"showing {min(len(snap.public_messages), public_page_limit)}/{len(snap.public_messages)} retained",
    )
    draw_button(s, (204, 94, 282, 134), "Search", BLUE, action="open_public_search", destination="public_search_sheet")
    draw_button(s, (292, 94, 356, 134), "Clear", ACCENT, action="clear_public_search")
    draw_button(s, (366, 94, 436, 134), "Close", MUTED, action="close_public_history", destination="messages")
    s.round_rect((44, 154, 436, 318), SURFACE, BORDER, 8)
    s.text("Public scrollback", (56, 162, 260, 184), 13, MUTED, True)
    visible_messages = snap.public_messages[-3:]
    y = 194
    for msg in visible_messages:
        draw_row(s, (56, y, 424, y + 34), f"{msg.source}: {msg.text}", msg.meta, "new" if msg.unread else None)
        y += 40
    if load_older_available:
        draw_button(s, (44, 332, 178, 376), "Load Older", BLUE, action="load_older_public_history")
    draw_dock(s, "Messages")
    s.metrics.update(
        {
            "public_history_source_count": len(snap.public_messages),
            "public_history_page_limit": public_page_limit,
            "public_history_rendered_count": len(visible_messages),
            "public_history_load_older_available": load_older_available,
        }
    )


def render_public_search_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Public Search", "Filter retained Public rows")
    s.round_rect((44, 158, 436, 210), SURFACE_2, BORDER, 8)
    s.touch_target("Search author or message", (44, 158, 436, 210), kind="text_field", action="edit_public_search")
    s.text("Search author or message", (56, 166, 424, 190), 13, MUTED, True)
    s.text("test", (56, 188, 424, 206), 16, TEXT)
    draw_button(s, (44, 228, 156, 278), "Apply", GREEN, action="apply_public_search", destination="public_history_sheet")
    draw_button(s, (166, 228, 278, 278), "Clear", ACCENT, action="clear_public_search")
    draw_button(s, (288, 228, 400, 278), "Close", MUTED, action="close_public_search", destination="public_history_sheet")
    s.round_rect((44, 300, 436, 370), SURFACE, BORDER, 8)
    s.text("Keyboard opens for Public history search", (56, 318, 424, 350), 13, MUTED, False, "center")
    draw_dock(s, "Messages")


def render_message_detail_sheet(s: Surface, snap: Snapshot):
    msg = snap.public_messages[1] if len(snap.public_messages) > 1 else snap.public_messages[0]
    draw_sheet_frame(s, "Message Detail", msg.source)
    draw_button(s, (214, 94, 316, 134), "Advanced", BLUE, action="toggle_message_detail_advanced")
    draw_button(s, (326, 94, 436, 134), "Close", MUTED, action="close_message_detail", destination="messages")
    s.text("Sender", (44, 154, 160, 174), 13, MUTED, True)
    s.text(f"{msg.source}  received", (44, 176, 436, 200), 17, TEXT)
    s.text("Message", (44, 212, 160, 232), 13, MUTED, True)
    s.text(msg.text, (44, 234, 436, 260), 16, TEXT)
    s.text("Signal", (44, 276, 160, 296), 13, MUTED, True)
    s.text("RSSI -41  SNR 30", (44, 298, 436, 318), 15, GREEN)
    s.text("Path", (44, 330, 160, 350), 13, MUTED, True)
    s.text("1 hop", (44, 352, 436, 372), 14, BLUE)
    draw_button(s, (44, 382, 200, 412), "Reply", GREEN, action="open_public_reply", destination="compose_sheet")
    draw_dock(s, "Messages")


def render_contact_detail_sheet(s: Surface, snap: Snapshot):
    contact = snap.contacts[0]
    draw_sheet_frame(s, "Contact Detail", contact.name)
    s.text("Fingerprint", (44, 154, 180, 174), 13, MUTED, True)
    s.text(contact.fingerprint, (44, 176, 436, 200), 17, TEXT)
    s.text("Signal", (44, 210, 180, 230), 13, MUTED, True)
    s.text(f"{contact.signal}  {contact.meta}", (44, 232, 436, 254), 14, GREEN)
    buttons = (("DM", GREEN), ("Trace", BLUE), ("Edit", ACCENT), ("Export", ACCENT), ("Fav", ACCENT), ("Mute", ACCENT))
    actions = {
        "DM": ("open_dm_compose", "compose_sheet", False),
        "Trace": ("open_route_trace", "route_trace_sheet", False),
        "Edit": ("open_contact_edit", "contact_edit_sheet", False),
        "Export": ("open_contact_export", "contact_export_sheet", False),
        "Fav": ("toggle_favorite", None, False),
        "Mute": ("toggle_mute", None, False),
    }
    for i, (label, color) in enumerate(buttons):
        action, destination, destructive = actions[label]
        draw_button(
            s,
            (44 + i * 64, 278, 100 + i * 64, 330),
            label,
            color,
            action=action,
            destination=destination,
            destructive=destructive,
        )
    draw_button(s, (44, 346, 200, 378), "Close", MUTED, action="close_contact_detail", destination="nodes")
    draw_dock(s, "Nodes")


def render_node_detail_sheet(s: Surface, snap: Snapshot):
    node = snap.heard[0]
    draw_sheet_frame(s, "Node Detail", node.name)
    s.round_rect((44, 150, 118, 176), (22, 39, 49), role_badge_color(node.role), 8)
    s.text(role_badge_text(node.role), (50, 152, 112, 174), 11, role_badge_color(node.role), True, "center")
    s.text("Role", (132, 150, 210, 170), 13, MUTED, True)
    s.text(node.role, (210, 150, 436, 174), 15, role_badge_color(node.role), True)
    s.text("Fingerprint", (44, 188, 180, 208), 13, MUTED, True)
    s.text(node.fingerprint, (44, 210, 436, 232), 16, TEXT)
    s.text("Public key", (44, 246, 180, 266), 13, MUTED, True)
    s.text("retained  reachable  normal", (166, 246, 436, 266), 14, GREEN, True)
    s.text("Signal", (44, 282, 130, 302), 13, MUTED, True)
    s.text(node.signal, (132, 282, 270, 302), 14, GREEN, True)
    s.text("Path", (280, 282, 336, 302), 13, MUTED, True)
    s.text(node.meta, (336, 282, 436, 302), 12, MUTED)
    s.text("Last heard", (44, 318, 150, 338), 13, MUTED, True)
    s.text("12s ago  heard 24", (152, 318, 436, 338), 14, TEXT)
    draw_button(s, (44, 358, 200, 392), "Close", MUTED, action="close_node_detail", destination="nodes")
    draw_dock(s, "Nodes")


def render_contact_edit_sheet(s: Surface, snap: Snapshot):
    contact = snap.contacts[0]
    draw_sheet_frame(s, "Edit Contact", contact.name)
    draw_button(s, (210, 94, 274, 134), "Save", GREEN, action="save_contact_alias", destination="contact_detail_sheet")
    draw_button(s, (284, 94, 356, 134), "Forget", RED, action="forget_contact", destination="nodes", destructive=True)
    draw_button(s, (366, 94, 436, 134), "Close", MUTED, action="close_contact_edit", destination="contact_detail_sheet")
    s.text("Alias only; retained history remains", (44, 150, 436, 172), 13, MUTED, True)
    s.round_rect((44, 184, 436, 236), SURFACE_2, BORDER, 8)
    s.touch_target("Contact alias", (44, 184, 436, 236), kind="text_field", action="edit_contact_alias")
    s.text("Contact alias", (56, 192, 220, 214), 13, MUTED, True)
    s.text(contact.name, (56, 214, 424, 232), 16, TEXT)
    s.round_rect((44, 258, 436, 370), SURFACE, BORDER, 8)
    s.text("Keyboard saves alias; Forget removes only the promoted contact", (56, 280, 424, 340), 13, MUTED, False, "center")
    draw_dock(s, "Nodes")


def render_radio_settings_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Radio Settings", "Saved profile applies after reboot")
    s.text(snap.radio_profile, (44, 154, 436, 178), 14, GREEN, True)
    s.text("Freq 910.525 MHz", (44, 190, 220, 212), 13, TEXT, True)
    draw_button(s, (244, 184, 316, 220), "-25k", ACCENT, action="radio_freq_down")
    draw_button(s, (324, 184, 396, 220), "+25k", ACCENT, action="radio_freq_up")
    s.text("BW 62.5 kHz", (44, 232, 220, 254), 13, TEXT, True)
    draw_button(s, (244, 226, 396, 262), "Cycle BW", ACCENT, action="radio_cycle_bandwidth")
    s.text("SF 7", (44, 274, 98, 296), 13, TEXT, True)
    draw_button(s, (106, 268, 166, 304), "SF-", ACCENT, action="radio_sf_down")
    draw_button(s, (174, 268, 234, 304), "SF+", ACCENT, action="radio_sf_up")
    s.text("CR 5", (250, 274, 304, 296), 13, TEXT, True)
    draw_button(s, (312, 268, 396, 304), "Cycle", ACCENT, action="radio_cycle_cr")
    s.text("TX 20 dBm", (44, 318, 136, 340), 13, TEXT, True)
    draw_button(s, (146, 312, 206, 348), "TX-", ACCENT, action="radio_tx_power_down")
    draw_button(s, (214, 312, 274, 348), "TX+", ACCENT, action="radio_tx_power_up")
    draw_button(s, (282, 312, 416, 348), "RX Boost On", GREEN, action="radio_toggle_rx_boost")
    draw_button(s, (44, 356, 136, 386), "US/CAN", BLUE, action="radio_defaults")
    draw_button(s, (146, 356, 238, 386), "Save", GREEN, action="save_radio_profile", destination="settings")
    draw_button(s, (248, 356, 340, 386), "Close", MUTED, action="close_radio_settings", destination="settings")
    draw_dock(s, "Settings")


def render_storage_setup_sheet(s: Surface, snap: Snapshot):
    if snap.storage_setup_action == "inspect_rp2040_sd_mount_error_firmware_path":
        subtitle = "Firmware mount issue"
        guidance = "Inspect RP2040 mount diagnostics; do not format on-device."
    elif snap.storage_setup_action == "inspect_rp2040_sd_cmd0_firmware_path":
        subtitle = "Firmware probe issue"
        guidance = "Inspect RP2040 CMD0/CMD8 diagnostics; do not format on-device."
    elif snap.storage_setup_action == "prepare_fat32_on_computer":
        subtitle = "FAT32 card required"
        guidance = "Prepare FAT32 on a computer; DeskOS only creates folders."
    else:
        subtitle = "FAT32 only, no format"
        guidance = "No device format; onboard NVS remains fallback."

    draw_sheet_frame(s, "SD Card", subtitle)
    draw_button(s, (356, 94, 436, 134), "Close", MUTED, action="close_storage_setup", destination="settings")
    draw_metric(s, (44, 154, 436, 214), "SD Card", snap.storage_state, snap.storage_detail, AMBER)
    draw_metric(s, (44, 226, 436, 286), "Backends", snap.storage_backend, snap.storage_stores, BLUE)
    s.text(f"setup {snap.storage_setup_action}", (44, 302, 436, 324), 14, TEXT, True)
    s.text(guidance, (44, 328, 436, 360), 12, AMBER, True)
    draw_dock(s, "Settings")


def render_display_settings_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Display", "Screen controls")
    draw_button(s, (356, 94, 436, 134), "Close", MUTED, action="close_display_settings", destination="settings")
    s.text("Screen controls", (44, 154, 436, 178), 15, GREEN, True)
    s.text("Brightness, night mode, contrast, and timeout belong here.", (44, 194, 436, 236), 13, TEXT)
    s.text("Touch display controls are staged until backlight/runtime persistence is wired.", (44, 252, 436, 294), 13, AMBER)
    draw_button(s, (44, 318, 160, 360), "Brightness", BLUE, action="display_brightness")
    draw_button(s, (172, 318, 260, 360), "Night", BLUE, action="display_night")
    draw_button(s, (272, 318, 386, 360), "Contrast", BLUE, action="display_contrast")
    s.text("Timeout", (44, 374, 160, 402), 12, MUTED, True)
    draw_dock(s, "Settings")


def render_diagnostics_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Diagnostics", "Advanced health")
    draw_button(s, (356, 94, 436, 134), "Close", MUTED, action="close_diagnostics", destination="settings")
    s.text("Health", (44, 154, 160, 174), 13, GREEN, True)
    s.text("reset poweron  uptime 122s", (44, 178, 436, 200), 13, TEXT)
    s.text("heap 184K free  min 169K  largest 80K", (44, 206, 436, 228), 12, MUTED)
    s.text("LVGL heap backed  ui stack 3052 words", (44, 234, 436, 256), 12, MUTED)
    s.text("packets rx 128 tx 34  rejected 0", (44, 262, 436, 284), 12, BLUE)
    s.text("Crashlog  Exports  Serial", (44, 300, 436, 322), 13, TEXT, True)
    s.text("Advanced details stay here so normal screens remain simple.", (44, 326, 436, 350), 12, AMBER)
    draw_button(s, (44, 358, 156, 390), "Crashlog", AMBER, action="diagnostics_crashlog")
    draw_button(s, (168, 358, 264, 390), "Export", BLUE, action="diagnostics_export")
    draw_button(s, (276, 358, 362, 390), "Soak", GREEN, action="diagnostics_soak")
    draw_dock(s, "Settings")


def render_wifi_setup_sheet(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.rect((0, TOP_BAR_H, WIDTH, HEIGHT), (17, 25, 35))
    s.text("Wi-Fi Setup", (28, 70, 230, 100), 22, TEXT, True)
    s.text("Profile and state", (28, 96, 230, 116), 12, MUTED)
    draw_button(s, (392, 66, 464, 106), "Close", MUTED, action="close_wifi_setup", destination="home")
    s.text("State off  build enabled", (28, 112, 452, 134), 14, AMBER, True)
    s.text("Last none", (28, 134, 452, 156), 13, MUTED)
    s.text("Profile not saved  password open/empty", (28, 156, 452, 178), 13, TEXT)
    s.text("Network name", (28, 186, 220, 206), 13, GREEN, True)
    s.round_rect((16, 206, 464, 242), SURFACE_2, BORDER, 8)
    s.touch_target("SSID", (16, 206, 464, 242), kind="text_field", action="edit_wifi_ssid")
    s.text("SSID", (28, 214, 452, 236), 14, MUTED)
    s.text("Password", (28, 248, 220, 268), 13, GREEN, True)
    s.round_rect((16, 268, 464, 304), SURFACE_2, BORDER, 8)
    s.touch_target("Optional", (16, 268, 464, 304), kind="text_field", action="edit_wifi_password")
    s.text("Optional", (28, 276, 452, 298), 14, MUTED)
    draw_button(s, (16, 314, 78, 352), "Save", GREEN, action="wifi_save")
    draw_button(s, (86, 314, 152, 352), "Clear", AMBER, action="wifi_clear")
    draw_button(s, (160, 314, 222, 352), "Scan", BLUE, action="wifi_scan")
    draw_button(s, (230, 314, 316, 352), "Connect", GREEN, action="wifi_connect")
    draw_button(s, (324, 314, 410, 352), "Enable", BLUE, action="wifi_enable")
    s.text("Scan to list nearby 2.4 GHz networks", (28, 360, 452, 382), 12, MUTED)
    s.round_rect((16, 386, 464, 468), (10, 16, 24), BORDER, 8)
    s.text("Keyboard", (28, 394, 452, 416), 13, MUTED, True)
    s.text("q w e r t y u i o p", (32, 426, 448, 456), 16, TEXT, False, "center")


def render_ble_setup_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "BLE Setup", "Companion state")
    draw_button(s, (356, 94, 436, 134), "Close", MUTED, action="close_ble_setup", destination="home")
    s.text("State off  build disabled", (44, 154, 436, 178), 15, AMBER, True)
    if snap.ble_transport_supported:
        s.text("Companion BLE is available for measured local setup.", (44, 194, 436, 236), 13, TEXT)
        s.text("Pairing controls require a measured BLE runtime artifact.", (44, 252, 436, 294), 13, AMBER)
        draw_button(s, (44, 318, 142, 360), "Enable", BLUE, action="ble_enable")
        draw_button(s, (154, 318, 252, 360), "Pair", GREEN, action="ble_pair")
        draw_button(s, (264, 318, 362, 360), "Forget", AMBER, action="ble_forget")
    else:
        s.text("BLE companion transport is unavailable in this release.", (44, 194, 436, 236), 13, TEXT)
        s.text("No BLE pairing or transport artifact is present for public release.", (44, 252, 436, 294), 13, AMBER)
        s.text("Enable unavailable", (44, 318, 210, 340), 13, AMBER, True)
        s.text("Pair unavailable", (44, 346, 210, 366), 12, MUTED)
        s.text("Forget unavailable", (224, 346, 436, 366), 12, MUTED)
    s.text("USB remains the reliable companion path for production validation.", (44, 386, 436, 416), 12, MUTED)
    draw_dock(s, "Home")


def render_advert_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Advert", "Share this node with nearby MeshCore clients")
    s.text("Zero-hop advert", (44, 162, 220, 184), 13, MUTED, True)
    s.text("Use when the peer is nearby and Public should stay quiet.", (44, 186, 436, 208), 12, TEXT)
    draw_button(s, (44, 222, 184, 274), "Zero Hop", GREEN, action="send_advert_zero", rf_tx=True)
    s.text("Flood advert", (44, 292, 220, 314), 13, MUTED, True)
    s.text("Intentional wider RF advert for controlled tests only.", (44, 316, 436, 338), 12, AMBER)
    draw_button(s, (44, 352, 184, 386), "Flood", AMBER, action="send_advert_flood", rf_tx=True)
    draw_button(s, (316, 94, 436, 134), "Close", MUTED, action="close_advert_sheet", destination="settings")
    draw_dock(s, "Settings")


def render_contact_export_sheet(s: Surface, snap: Snapshot):
    contact = snap.contacts[0]
    draw_sheet_frame(s, "Contact Export", "MeshCore QR for YKF Corebot")
    draw_fake_qr(s, (52, 154, 214, 316))
    s.text("MeshCore QR", (234, 154, 436, 178), 15, GREEN, True)
    s.text("Fingerprint", (234, 190, 436, 210), 13, MUTED, True)
    s.text(contact.fingerprint, (234, 212, 436, 236), 15, TEXT)
    s.text("URI", (234, 250, 436, 270), 13, MUTED, True)
    s.text("meshcore://contact/add", (234, 272, 436, 288), 10, BLUE)
    s.text("name=YKF+Corebot  type=1", (234, 288, 436, 304), 10, MUTED)
    s.text(f"key {SAMPLE_PUBLIC_KEY[:12]}...{SAMPLE_PUBLIC_KEY[-8:]}", (234, 304, 436, 320), 10, BLUE)
    draw_button(s, (44, 340, 200, 374), "Close", MUTED, action="close_contact_export", destination="contact_detail_sheet")
    draw_dock(s, "Nodes")


def render_dm_thread_sheet(s: Surface, snap: Snapshot):
    dm_thread_page_limit = 5
    load_older_available = len(snap.dm_messages) > dm_thread_page_limit
    draw_sheet_frame(s, "DM Thread", "YKF Corebot")
    s.text(f"Thread {min(len(snap.dm_messages), dm_thread_page_limit)}/{len(snap.dm_messages)} rows", (44, 132, 436, 150), 12, MUTED)
    visible_messages = snap.dm_messages[-(2 if load_older_available else 3):]
    y = 154
    for msg in visible_messages:
        draw_row(s, (44, y, 436, y + 42), f"{msg.source}: {msg.text}", msg.meta, "new" if msg.unread else None)
        y += 50
    button_y = 304
    if load_older_available:
        draw_button(s, (44, 260, 178, 304), "Load Older", BLUE, action="load_older_dm_thread")
        button_y = 318
    draw_button(s, (44, button_y, 160, button_y + 52), "Reply", GREEN, action="open_dm_reply", destination="compose_sheet")
    draw_button(s, (174, button_y, 290, button_y + 52), "Read", ACCENT, action="mark_dm_thread_read")
    draw_button(s, (304, button_y, 420, button_y + 52), "Close", MUTED, action="close_dm_thread", destination="messages")
    draw_dock(s, "Messages")
    s.metrics.update(
        {
            "dm_thread_source_count": len(snap.dm_messages),
            "dm_thread_page_limit": dm_thread_page_limit,
            "dm_thread_rendered_count": len(visible_messages),
            "dm_thread_load_older_available": load_older_available,
        }
    )


def render_route_detail_sheet(s: Surface, snap: Snapshot):
    route = snap.routes[0]
    draw_sheet_frame(s, "Route Detail", route.note)
    s.text("Target", (44, 154, 160, 174), 13, MUTED, True)
    s.text("Public room", (44, 176, 436, 200), 18, TEXT)
    s.text("Path", (44, 210, 160, 230), 13, MUTED, True)
    s.text(f"{route.direction}  {route.meta}", (44, 232, 436, 256), 16, AMBER)
    s.text("Confidence", (44, 270, 180, 290), 13, MUTED, True)
    s.text("recent live packet evidence", (44, 292, 436, 316), 16, GREEN)
    draw_button(s, (44, 340, 200, 374), "Close", MUTED, action="close_route_detail", destination="packets")
    draw_dock(s, "Packets")


def render_route_trace_sheet(s: Surface, snap: Snapshot):
    contact = snap.contacts[0]
    contact_routes = tuple(route for route in snap.routes if contact.fingerprint[:4] in route.meta or "direct" in route.note)
    draw_sheet_frame(s, "Route Trace", contact.name)
    s.text("Trace", (44, 154, 160, 174), 13, MUTED, True)
    s.text(contact.fingerprint, (44, 176, 436, 198), 16, TEXT)
    s.text("Contact Path", (44, 210, 180, 230), 13, MUTED, True)
    s.text("key retained  path known  hops 0", (44, 232, 436, 254), 15, GREEN)
    s.text("Best Evidence", (44, 266, 180, 286), 13, MUTED, True)
    s.text("direct seq 42 confidence 100", (44, 288, 436, 310), 15, ACCENT)
    y = 320
    rendered = 0
    for route in contact_routes[:2]:
        draw_row(s, (44, y, 436, y + 30), f"{route.kind} {route.direction}", f"{route.meta}  {route.note}")
        y += 34
        rendered += 1
    draw_button(s, (236, 94, 306, 134), "Ping", BLUE, action="send_trace_probe", dm_tx=True)
    draw_button(s, (316, 94, 436, 134), "Close", MUTED, action="close_route_trace", destination="contact_detail_sheet")
    s.text("DM-only trace probe", (44, 390, 300, 408), 11, MUTED)
    draw_dock(s, "Nodes")
    s.metrics.update(
        {
            "route_trace_source_count": len(contact_routes),
            "route_trace_rendered_count": rendered,
        }
    )


def render_packet_detail_sheet(s: Surface, snap: Snapshot):
    packet = snap.packets[0]
    draw_sheet_frame(s, "Packet Detail", packet.note)
    draw_button(s, (214, 94, 316, 134), "Advanced", BLUE, action="toggle_packet_detail_advanced")
    draw_button(s, (326, 94, 436, 134), "Close", MUTED, action="close_packet_detail", destination="packets")
    s.text("Kind", (44, 154, 160, 174), 13, MUTED, True)
    s.text(f"{packet.kind} {packet.direction}", (44, 176, 436, 200), 18, TEXT)
    s.text("Signal", (44, 210, 160, 230), 13, MUTED, True)
    s.text(packet.meta, (44, 232, 436, 256), 16, GREEN)
    s.text("Payload", (44, 270, 180, 290), 13, MUTED, True)
    s.text("parsed MeshCore text packet", (44, 292, 436, 316), 16, BLUE)
    s.text("Path", (44, 326, 180, 346), 13, MUTED, True)
    s.text("hash 2 byte  hops 1  uptime 12044ms", (108, 326, 436, 346), 13, TEXT)
    s.text("Raw Hex", (44, 358, 180, 378), 13, MUTED, True)
    s.text(packet.raw_hex, (116, 358, 436, 378), 14, BLUE)
    draw_dock(s, "Packets")


def render_packet_search_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Packet Search", "Filter kind, note, or raw hex")
    s.round_rect((44, 158, 436, 210), SURFACE_2, BORDER, 8)
    s.touch_target("Search kind, note, raw hex", (44, 158, 436, 210), kind="text_field", action="edit_packet_search")
    s.text("Search kind, note, raw hex", (56, 166, 424, 190), 13, MUTED, True)
    s.text("test", (56, 188, 424, 206), 16, TEXT)
    draw_button(s, (44, 228, 156, 278), "Apply", GREEN, action="apply_packet_search", destination="packets")
    draw_button(s, (166, 228, 278, 278), "Clear", ACCENT, action="clear_packet_search")
    draw_button(s, (288, 228, 400, 278), "Close", MUTED, action="close_packet_search", destination="packets")
    s.round_rect((44, 300, 436, 370), SURFACE, BORDER, 8)
    s.text("Keyboard opens for packet search", (56, 318, 424, 350), 13, MUTED, False, "center")
    draw_dock(s, "Packets")


def render_mesh_roles_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Mesh Roles", "Room servers and repeater candidates")
    s.text("Room Servers", (44, 154, 220, 176), 14, MUTED, True)
    y = 184
    for node in snap.rooms:
        draw_row(s, (44, y, 436, y + 36), f"{node.name}  {node.role}", f"{node.fingerprint}  {node.signal}")
        y += 44
    s.text("Repeater Candidates", (44, 234, 250, 256), 14, MUTED, True)
    y = 264
    for node in snap.repeaters:
        draw_row(s, (44, y, 436, y + 36), f"{node.name}  {node.role}", f"{node.meta}  {node.signal}")
        y += 44
    draw_button(s, (44, 340, 200, 374), "Close", MUTED, action="close_mesh_roles", destination="packets")
    draw_dock(s, "Packets")


def render_lock_overlay(s: Surface, snap: Snapshot):
    s.rect((0, 0, WIDTH, HEIGHT), (4, 8, 13))
    s.touch_target("Tap to unlock", (0, 0, WIDTH, HEIGHT), kind="screen", action="unlock", destination="home")
    s.text("MeshCore DeskOS", (40, 72, 440, 110), 28, TEXT, True, "center")
    s.text(snap.node_name, (40, 116, 440, 140), 16, MUTED, False, "center")
    draw_metric(s, (76, 176, 404, 252), "Mesh", snap.mesh_state, f"{snap.latest_signal} latest", GREEN)
    draw_metric(s, (76, 268, 404, 344), "Unread", f"Public {snap.unread_public}", f"DM {snap.unread_dm}, tap to unlock", AMBER)
    s.text("Tap to unlock", (40, 392, 440, 426), 20, ACCENT, True, "center")


def render_onboarding_sheet(s: Surface, snap: Snapshot):
    s.rect((0, 0, WIDTH, HEIGHT), BG)
    s.round_rect((12, 25, 468, 455), (7, 16, 24), (94, 234, 212), 8)
    s.text("MeshCore DeskOS D1L", (32, 40, 430, 70), 24, TEXT, True)
    s.text("First boot setup", (32, 74, 430, 96), 14, ACCENT, True)
    s.text("Node name", (32, 112, 200, 134), 13, MUTED, True)
    s.round_rect((32, 140, 448, 190), SURFACE_2, BORDER, 8)
    s.touch_target("Node name", (32, 140, 448, 190), kind="text_field", action="edit_node_name")
    s.text(snap.node_name, (46, 152, 430, 180), 18, TEXT)
    s.text("Canada/USA preset confirmed", (32, 204, 430, 226), 14, TEXT, True)
    s.text("910.525 BW62.5 SF7 CR5", (32, 228, 430, 250), 13, MUTED)
    s.text("Role Desk Companion", (32, 260, 430, 282), 14, TEXT, True)
    s.text("Wi-Fi off  BLE off  Observer off", (32, 284, 430, 306), 13, MUTED)
    draw_button(s, (32, 324, 148, 374), "Start", GREEN, action="complete_onboarding", destination="home")
    draw_button(s, (164, 324, 310, 374), "Use Defaults", ACCENT, action="apply_onboarding_defaults")
    s.round_rect((32, 392, 448, 432), SURFACE, BORDER, 8)
    s.text("Keyboard opens for name editing", (44, 400, 436, 424), 13, MUTED, False, "center")


RENDERERS: dict[str, Callable[[Surface, Snapshot], None]] = {
    "home": render_home,
    "messages": render_messages,
    "messages_dm": render_messages_dm,
    "nodes": render_nodes,
    "map": render_map,
    "map_location_sheet": render_map_location_sheet,
    "map_tiles_sheet": render_map_tiles_sheet,
    "packets": render_packets,
    "settings": render_settings,
    "compose_sheet": render_compose_sheet,
    "public_history_sheet": render_public_history_sheet,
    "public_search_sheet": render_public_search_sheet,
    "radio_settings_sheet": render_radio_settings_sheet,
    "storage_setup_sheet": render_storage_setup_sheet,
    "display_settings_sheet": render_display_settings_sheet,
    "diagnostics_sheet": render_diagnostics_sheet,
    "wifi_setup_sheet": render_wifi_setup_sheet,
    "ble_setup_sheet": render_ble_setup_sheet,
    "advert_sheet": render_advert_sheet,
    "contact_detail_sheet": render_contact_detail_sheet,
    "node_detail_sheet": render_node_detail_sheet,
    "contact_edit_sheet": render_contact_edit_sheet,
    "contact_export_sheet": render_contact_export_sheet,
    "message_detail_sheet": render_message_detail_sheet,
    "dm_thread_sheet": render_dm_thread_sheet,
    "route_detail_sheet": render_route_detail_sheet,
    "route_trace_sheet": render_route_trace_sheet,
    "packet_detail_sheet": render_packet_detail_sheet,
    "packet_search_sheet": render_packet_search_sheet,
    "mesh_roles_sheet": render_mesh_roles_sheet,
    "lock_overlay": render_lock_overlay,
    "onboarding_sheet": render_onboarding_sheet,
}

REQUIRED_LABELS: dict[str, tuple[str, ...]] = {
    "home": (
        "DeskOS",
        "Chats",
        "DMs",
        "Rooms",
        "Contacts",
        "Repeaters",
        "Advertise",
        "Map",
        "Terminal",
        "Packets",
        "Settings",
        "Setup",
        "Signal",
        "Time",
        "Wi-Fi",
        "BLE",
        "SD",
    ),
    "messages": ("Messages", "Read", "Compose", "History", "Test", "Public", "DMs", "Public Channel"),
    "messages_dm": ("Messages", "Read", "Compose", "History", "Test", "Public", "DMs", "DM Conversations"),
    "nodes": ("Nodes", "Contacts", "Heard Nodes", "All Heard", "DM", "CMP", "ROOM", "RPT"),
    "map": (
        "Map",
        "Tiles",
        "Tile Cache",
        "Downloads",
        "Center",
        "Routes",
        "Connect Wi-Fi, choose an allowed provider, download only your area",
    ),
    "map_location_sheet": (
        "Set D1L Location",
        "Map needs your D1L location",
        "Enter decimal degrees",
        "Latitude",
        "Longitude",
        "Keyboard",
        "Save",
        "Clear",
        "Skip",
    ),
    "map_tiles_sheet": (
        "Map Tiles",
        "Allowed provider template",
        "Attribution",
        "Zoom 12",
        "Save",
        "Clear",
        "Live download unavailable",
        "Tile render pending; use serial canaries for SD cache proof.",
        "No public OSM bulk tile servers. Visible attribution is required.",
        "Keyboard",
        "Close",
    ),
    "packets": ("Packets", "live tail  rssi -41  snr 30  avg -46", "Mesh Roles", "All", "RX", "TX", "Text", "Search", "Pause", "Packet Feed", "Routes"),
    "settings": (
        "Settings",
        "Setup Dashboard",
        "SD Card",
        "Wi-Fi",
        "BLE",
        "Radio",
        "Map Tiles",
        "Display",
        "Identity",
        "Diagnostics",
        "About",
        "Advanced",
    ),
    "compose_sheet": ("Compose Public", "Public message", "20/138", "Keyboard", "Send", "Clear", "Close"),
    "public_history_sheet": ("Public History", "Search", "Clear", "Close", "Public scrollback"),
    "public_search_sheet": ("Public Search", "Search author or message", "Apply", "Clear", "Close"),
    "radio_settings_sheet": (
        "Radio Settings",
        "Freq 910.525 MHz",
        "-25k",
        "+25k",
        "Cycle BW",
        "SF-",
        "SF+",
        "Cycle",
        "TX-",
        "TX+",
        "RX Boost On",
        "US/CAN",
        "Save",
        "Close",
    ),
    "storage_setup_sheet": (
        "SD Card",
        "SD Card",
        "Backends",
        "Close",
    ),
    "display_settings_sheet": (
        "Display",
        "Screen controls",
        "Brightness",
        "Night",
        "Contrast",
        "Timeout",
        "Close",
    ),
    "diagnostics_sheet": (
        "Diagnostics",
        "Advanced health",
        "Health",
        "Crashlog",
        "Export",
        "Crashlog  Exports  Serial",
        "Close",
    ),
    "wifi_setup_sheet": (
        "Wi-Fi Setup",
        "Profile and state",
        "State off  build enabled",
        "Last none",
        "Network name",
        "Password",
        "SSID",
        "Optional",
        "Scan",
        "Connect",
        "Scan to list nearby 2.4 GHz networks",
        "Keyboard",
        "Save",
        "Clear",
        "Enable",
        "Close",
    ),
    "ble_setup_sheet": (
        "BLE Setup",
        "Companion state",
        "State off  build disabled",
        "BLE companion transport is unavailable in this release.",
        "No BLE pairing or transport artifact is present for public release.",
        "Enable unavailable",
        "Pair unavailable",
        "Forget unavailable",
        "Close",
    ),
    "advert_sheet": ("Advert", "Zero-hop advert", "Zero Hop", "Flood advert", "Flood", "Close"),
    "contact_detail_sheet": ("Contact Detail", "Fingerprint", "Signal", "DM", "Trace", "Edit", "Export", "Fav", "Mute", "Close"),
    "node_detail_sheet": ("Node Detail", "Role", "Fingerprint", "Public key", "Signal", "Path", "Last heard", "Close"),
    "contact_edit_sheet": ("Edit Contact", "Contact alias", "Save", "Forget", "Close"),
    "contact_export_sheet": ("Contact Export", "MeshCore QR", "Fingerprint", "URI", "Close"),
    "message_detail_sheet": ("Message Detail", "Sender", "Message", "Signal", "Path", "Advanced", "Reply", "Close"),
    "dm_thread_sheet": ("DM Thread", "Reply", "Read", "Close"),
    "route_detail_sheet": ("Route Detail", "Target", "Path", "Confidence", "Close"),
    "route_trace_sheet": ("Route Trace", "Trace", "Contact Path", "Best Evidence", "Ping", "Close"),
    "packet_detail_sheet": ("Packet Detail", "Kind", "Signal", "Payload", "Advanced", "Raw Hex", "Close"),
    "packet_search_sheet": ("Packet Search", "Search kind, note, raw hex", "Apply", "Clear", "Close"),
    "mesh_roles_sheet": ("Mesh Roles", "Room Servers", "Repeater Candidates", "Close"),
    "lock_overlay": ("MeshCore DeskOS", "Mesh", "Unread", "Tap to unlock"),
    "onboarding_sheet": (
        "MeshCore DeskOS D1L",
        "First boot setup",
        "Node name",
        "Canada/USA preset confirmed",
        "Role Desk Companion",
        "Wi-Fi off  BLE off  Observer off",
        "Start",
        "Use Defaults",
    ),
}

EXPECTED_FLOWS: tuple[dict[str, object], ...] = (
    {
        "name": "first_boot_onboarding",
        "steps": (
            {"view": "onboarding_sheet", "action": "edit_node_name"},
            {"view": "onboarding_sheet", "action": "complete_onboarding", "destination": "home"},
            {"view": "onboarding_sheet", "action": "apply_onboarding_defaults"},
        ),
    },
    {
        "name": "lock_overlay_unlock",
        "steps": ({"view": "lock_overlay", "action": "unlock", "destination": "home"},),
    },
    {
        "name": "home_launcher_navigation",
        "steps": (
            {"view": "home", "action": "open_messages_public", "destination": "messages"},
            {"view": "home", "action": "open_messages_dm", "destination": "messages_dm"},
            {"view": "home", "action": "open_nodes", "destination": "nodes"},
            {"view": "home", "action": "open_repeaters", "destination": "mesh_roles_sheet"},
            {"view": "home", "action": "open_map", "destination": "map"},
            {"view": "home", "action": "open_packets", "destination": "packets"},
            {"view": "home", "action": "open_settings", "destination": "settings"},
            {"view": "home", "action": "open_advert_sheet", "destination": "advert_sheet"},
            {"view": "home", "action": "open_diagnostics", "destination": "diagnostics_sheet"},
            {"view": "home", "action": "open_mesh_roles", "destination": "mesh_roles_sheet"},
            {"view": "home", "action": "open_signal", "destination": "mesh_roles_sheet"},
            {"view": "home", "action": "open_wifi_settings", "destination": "wifi_setup_sheet"},
            {"view": "wifi_setup_sheet", "action": "wifi_scan"},
            {"view": "wifi_setup_sheet", "action": "wifi_connect"},
            {"view": "wifi_setup_sheet", "action": "close_wifi_setup", "destination": "home"},
            {"view": "home", "action": "open_ble_settings", "destination": "ble_setup_sheet"},
            {"view": "ble_setup_sheet", "action": "close_ble_setup", "destination": "home"},
            {"view": "home", "action": "open_storage_setup", "destination": "storage_setup_sheet"},
        ),
    },
    {
        "name": "public_compose_and_send",
        "steps": (
            {"view": "messages", "action": "open_public_compose", "destination": "compose_sheet"},
            {"view": "compose_sheet", "action": "edit_public_message"},
            {"view": "compose_sheet", "action": "send_public_text", "public_rf_tx": True},
            {"view": "compose_sheet", "action": "close_compose", "destination": "messages"},
        ),
    },
    {
        "name": "public_history_search",
        "steps": (
            {"view": "messages", "action": "mark_messages_read"},
            {"view": "messages", "action": "open_messages_public", "destination": "messages"},
            {"view": "messages", "action": "open_public_history", "destination": "public_history_sheet"},
            {"view": "public_history_sheet", "action": "open_public_search", "destination": "public_search_sheet"},
            {"view": "public_search_sheet", "action": "edit_public_search"},
            {"view": "public_search_sheet", "action": "apply_public_search", "destination": "public_history_sheet"},
        ),
    },
    {
        "name": "public_message_detail",
        "steps": (
            {"view": "messages", "action": "open_message_detail", "destination": "message_detail_sheet"},
            {"view": "message_detail_sheet", "action": "close_message_detail", "destination": "messages"},
        ),
    },
    {
        "name": "public_message_reply",
        "steps": (
            {"view": "messages", "action": "open_message_detail", "destination": "message_detail_sheet"},
            {"view": "message_detail_sheet", "action": "open_public_reply", "destination": "compose_sheet"},
        ),
    },
    {
        "name": "dm_thread_read_and_reply",
        "steps": (
            {"view": "messages", "action": "open_messages_dm", "destination": "messages_dm"},
            {"view": "messages_dm", "action": "open_dm_thread", "destination": "dm_thread_sheet"},
            {"view": "dm_thread_sheet", "action": "open_dm_reply", "destination": "compose_sheet"},
            {"view": "dm_thread_sheet", "action": "mark_dm_thread_read"},
            {"view": "dm_thread_sheet", "action": "close_dm_thread", "destination": "messages"},
        ),
    },
    {
        "name": "node_detail_inspection",
        "steps": (
            {"view": "nodes", "action": "open_node_detail", "destination": "node_detail_sheet"},
            {"view": "node_detail_sheet", "action": "close_node_detail", "destination": "nodes"},
        ),
    },
    {
        "name": "contact_detail_management",
        "steps": (
            {"view": "nodes", "action": "open_contact_detail", "destination": "contact_detail_sheet"},
            {"view": "contact_detail_sheet", "action": "open_dm_compose", "destination": "compose_sheet"},
            {"view": "contact_detail_sheet", "action": "open_route_trace", "destination": "route_trace_sheet"},
            {"view": "route_trace_sheet", "action": "send_trace_probe", "dm_tx": True},
            {"view": "contact_detail_sheet", "action": "open_contact_edit", "destination": "contact_edit_sheet"},
            {"view": "contact_detail_sheet", "action": "open_contact_export", "destination": "contact_export_sheet"},
            {"view": "contact_detail_sheet", "action": "toggle_favorite"},
            {"view": "contact_detail_sheet", "action": "toggle_mute"},
        ),
    },
    {
        "name": "contact_edit_alias_and_forget",
        "steps": (
            {"view": "contact_edit_sheet", "action": "edit_contact_alias"},
            {"view": "contact_edit_sheet", "action": "save_contact_alias", "destination": "contact_detail_sheet"},
            {"view": "contact_edit_sheet", "action": "forget_contact", "destination": "nodes", "destructive": True},
            {"view": "contact_edit_sheet", "action": "close_contact_edit", "destination": "contact_detail_sheet"},
        ),
    },
    {
        "name": "map_page_policy",
        "steps": (
            {"view": "home", "action": "open_map", "destination": "map"},
            {"view": "map", "action": "open_map_tiles", "destination": "map_tiles_sheet"},
            {"view": "map_tiles_sheet", "action": "edit_map_tile_provider"},
            {"view": "map_tiles_sheet", "action": "edit_map_tile_attribution"},
            {"view": "map_tiles_sheet", "action": "map_tile_zoom_down"},
            {"view": "map_tiles_sheet", "action": "map_tile_zoom_up"},
            {"view": "map_tiles_sheet", "action": "save_map_tile_provider"},
            {"view": "map_tiles_sheet", "action": "close_map_tiles", "destination": "map"},
            {"view": "map", "action": "open_map_location_picker", "destination": "map_location_sheet"},
            {"view": "map_location_sheet", "action": "edit_map_latitude"},
            {"view": "map_location_sheet", "action": "edit_map_longitude"},
            {"view": "map_location_sheet", "action": "save_map_location", "destination": "map"},
            {"view": "map_location_sheet", "action": "skip_map_location", "destination": "map"},
        ),
    },
    {
        "name": "packet_filters_search_and_details",
        "steps": (
            {"view": "packets", "action": "packet_filter_all"},
            {"view": "packets", "action": "packet_filter_rx"},
            {"view": "packets", "action": "packet_filter_tx"},
            {"view": "packets", "action": "packet_filter_text"},
            {"view": "packets", "action": "pause_packet_feed"},
            {"view": "packets", "action": "open_packet_search", "destination": "packet_search_sheet"},
            {"view": "packet_search_sheet", "action": "edit_packet_search"},
            {"view": "packet_search_sheet", "action": "apply_packet_search", "destination": "packets"},
            {"view": "packets", "action": "open_packet_detail", "destination": "packet_detail_sheet"},
            {"view": "packet_detail_sheet", "action": "toggle_packet_detail_advanced"},
            {"view": "packets", "action": "open_route_detail", "destination": "route_detail_sheet"},
        ),
    },
    {
        "name": "mesh_roles_browser",
        "steps": (
            {"view": "packets", "action": "open_mesh_roles", "destination": "mesh_roles_sheet"},
            {"view": "mesh_roles_sheet", "action": "close_mesh_roles", "destination": "packets"},
        ),
    },
    {
        "name": "settings_radio_storage_and_advert",
        "steps": (
            {"view": "settings", "action": "open_radio_settings", "destination": "radio_settings_sheet"},
            {"view": "radio_settings_sheet", "action": "radio_freq_down"},
            {"view": "radio_settings_sheet", "action": "radio_freq_up"},
            {"view": "radio_settings_sheet", "action": "radio_cycle_bandwidth"},
            {"view": "radio_settings_sheet", "action": "radio_sf_down"},
            {"view": "radio_settings_sheet", "action": "radio_sf_up"},
            {"view": "radio_settings_sheet", "action": "save_radio_profile", "destination": "settings"},
            {"view": "settings", "action": "open_storage_setup", "destination": "storage_setup_sheet"},
            {"view": "storage_setup_sheet", "action": "close_storage_setup", "destination": "settings"},
            {"view": "settings", "action": "open_advert_sheet", "destination": "advert_sheet"},
            {"view": "advert_sheet", "action": "send_advert_zero", "rf_tx": True},
            {"view": "advert_sheet", "action": "send_advert_flood", "rf_tx": True},
            {"view": "advert_sheet", "action": "close_advert_sheet", "destination": "settings"},
        ),
    },
    {
        "name": "settings_display_and_diagnostics",
        "steps": (
            {"view": "settings", "action": "open_display_settings", "destination": "display_settings_sheet"},
            {"view": "display_settings_sheet", "action": "close_display_settings", "destination": "settings"},
            {"view": "settings", "action": "open_diagnostics", "destination": "diagnostics_sheet"},
            {"view": "diagnostics_sheet", "action": "close_diagnostics", "destination": "settings"},
        ),
    },
)


def snapshot_counts(snap: Snapshot) -> dict[str, int]:
    return {
        "contacts": len(snap.contacts),
        "heard": len(snap.heard),
        "public_messages": len(snap.public_messages),
        "dm_messages": len(snap.dm_messages),
        "packets": len(snap.packets),
        "routes": len(snap.routes),
        "rooms": len(snap.rooms),
        "repeaters": len(snap.repeaters),
    }


def find_target(views_by_name: dict[str, dict[str, object]], step: dict[str, object]) -> dict[str, object] | None:
    view = views_by_name.get(str(step["view"]))
    if not view:
        return None
    for target in view["touch_targets"]:
        if target["action"] != step["action"]:
            continue
        for key in ("destination", "rf_tx", "public_rf_tx", "dm_tx", "destructive", "formats_sd"):
            if key in step and target.get(key) != step[key]:
                break
        else:
            return target
    return None


def touch_target_overlaps(targets: list[dict[str, object]]) -> list[dict[str, object]]:
    overlaps: list[dict[str, object]] = []
    for i, left in enumerate(targets):
        if left["kind"] == "screen":
            continue
        lx0, ly0, lx1, ly1 = left["visual_box"]
        for right in targets[i + 1 :]:
            if right["kind"] == "screen":
                continue
            rx0, ry0, rx1, ry1 = right["visual_box"]
            width = min(lx1, rx1) - max(lx0, rx0)
            height = min(ly1, ry1) - max(ly0, ry0)
            if width > 0 and height > 0:
                overlaps.append(
                    {
                        "left": left["label"],
                        "right": right["label"],
                        "box": [max(lx0, rx0), max(ly0, ry0), min(lx1, rx1), min(ly1, ry1)],
                    }
                )
    return overlaps


def build_flow_report(report_views: list[dict[str, object]]) -> dict[str, object]:
    views_by_name = {str(view["name"]): view for view in report_views}
    flows = []
    missing_steps: list[dict[str, object]] = []
    public_rf_actions: list[dict[str, object]] = []
    rf_actions: list[dict[str, object]] = []
    destructive_actions: list[dict[str, object]] = []
    format_actions: list[dict[str, object]] = []
    target_issues: list[dict[str, object]] = []
    target_overlaps: list[dict[str, object]] = []
    skipped_flows: list[str] = []

    for view in report_views:
        view_name = str(view["name"])
        target_issues.extend({"view": view_name, **target} for target in view["touch_target_issues"])
        target_overlaps.extend({"view": view_name, **overlap} for overlap in touch_target_overlaps(view["touch_targets"]))
        for target in view["touch_targets"]:
            action_summary = {
                "view": view_name,
                "action": target["action"],
                "label": target["label"],
                "destination": target["destination"],
            }
            if target["rf_tx"]:
                rf_actions.append(action_summary)
            if target["public_rf_tx"]:
                public_rf_actions.append(action_summary)
            if target["destructive"]:
                destructive_actions.append(action_summary)
            if target["formats_sd"]:
                format_actions.append(action_summary)

    for flow in EXPECTED_FLOWS:
        flow_views = {str(step["view"]) for step in flow["steps"]}
        if not flow_views <= set(views_by_name):
            skipped_flows.append(str(flow["name"]))
            continue
        checked_steps = []
        flow_missing = []
        for step in flow["steps"]:
            target = find_target(views_by_name, step)
            checked_step = dict(step)
            checked_step["present"] = target is not None
            if target:
                checked_step["label"] = target["label"]
                checked_step["box"] = target["box"]
            else:
                flow_missing.append(dict(step))
                missing_steps.append({"flow": flow["name"], **step})
            checked_steps.append(checked_step)
        flows.append({"name": flow["name"], "ok": not flow_missing, "steps": checked_steps, "missing_steps": flow_missing})

    return {
        "schema": 1,
        "min_touch_target": MIN_TOUCH_TARGET,
        "flows": flows,
        "skipped_flows": skipped_flows,
        "missing_steps": missing_steps,
        "target_issues": target_issues,
        "target_overlaps": target_overlaps,
        "rf_actions": rf_actions,
        "public_rf_actions": public_rf_actions,
        "destructive_actions": destructive_actions,
        "format_actions": format_actions,
        "ok": not missing_steps and not target_issues and not target_overlaps and not format_actions,
    }


def generate(out_dir: Path, views: tuple[str, ...] | None = None, scenario: str = "default") -> dict[str, object]:
    out_dir.mkdir(parents=True, exist_ok=True)
    if scenario not in SCENARIOS:
        raise ValueError(f"unknown scenario: {scenario}")
    snap = SCENARIOS[scenario]()
    selected = views or tuple(RENDERERS)
    report_views = []
    overflow_count = 0
    truncated_count = 0
    required_missing: list[dict[str, str]] = []
    touch_target_issue_count = 0
    for view in selected:
        if view not in RENDERERS:
            raise ValueError(f"unknown view: {view}")
        surface = Surface(view)
        RENDERERS[view](surface, snap)
        screenshot = out_dir / f"{view}.png"
        surface.save(screenshot)
        summary = surface.summary(screenshot, REQUIRED_LABELS.get(view, ()))
        overflow_count += len(summary["overflow"])
        truncated_count += len(summary["truncated_labels"])
        touch_target_issue_count += len(summary["touch_target_issues"])
        for label in summary["missing_required_labels"]:
            required_missing.append({"view": view, "label": label})
        report_views.append(summary)

    flow_report = build_flow_report(report_views)
    report = {
        "schema": 2,
        "ok": overflow_count == 0 and not required_missing and flow_report["ok"],
        "display": {"width": WIDTH, "height": HEIGHT},
        "source": "tools/ui_simulator.py",
        "scenario": scenario,
        "snapshot_counts": snapshot_counts(snap),
        "views": report_views,
        "touch_target_issue_count": touch_target_issue_count,
        "flow_report": flow_report,
        "overflow_count": overflow_count,
        "truncated_count": truncated_count,
        "required_labels_missing": required_missing,
    }
    report_path = out_dir / "ui-sim-report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate D1L UI simulator screenshots and layout report.")
    parser.add_argument("--out", type=Path, default=Path("artifacts/ui-sim"), help="output directory")
    parser.add_argument("--view", action="append", choices=tuple(RENDERERS), help="view to render; repeatable")
    parser.add_argument("--scenario", choices=tuple(SCENARIOS), default="default", help="snapshot scenario")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = generate(args.out, tuple(args.view) if args.view else None, scenario=args.scenario)
    print(
        json.dumps(
            {"ok": report["ok"], "views": len(report["views"]), "out": args.out.as_posix(), "scenario": args.scenario},
            sort_keys=True,
        )
    )
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
