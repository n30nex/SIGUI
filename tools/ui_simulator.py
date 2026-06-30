#!/usr/bin/env python3
"""Generate deterministic 480x480 UI check screenshots for DeskOS D1L."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from PIL import Image, ImageDraw, ImageFont


WIDTH = 480
HEIGHT = 480
TOP_BAR_H = 54
DOCK_Y = 420
DOCK_H = 60

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
    storage_setup_action: str
    storage_format_action: str


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
        ),
        routes=(
            Packet("Public route", "RX", "target Public hop 1", "via Krabs Lagoon", ""),
            Packet("DM route", "TX", "target 0BF0A direct", "direct path retained", ""),
        ),
        storage_state="pending bridge",
        storage_backend="NVS fallback",
        storage_detail="RP2040 SD bridge pending",
        storage_setup_action="bridge_protocol_pending",
        storage_format_action="not_available",
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
            "Room" if i % 5 == 0 else "Chat",
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
        for i in range(40)
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
        storage_setup_action="bridge_protocol_pending",
        storage_format_action="not_available",
    )


SCENARIOS: dict[str, Callable[[], Snapshot]] = {
    "default": sample_snapshot,
    "large-mesh": large_mesh_snapshot,
}


class Surface:
    def __init__(self, view: str):
        self.view = view
        self.image = Image.new("RGB", (WIDTH, HEIGHT), BG)
        self.draw = ImageDraw.Draw(self.image)
        self._fonts: dict[tuple[int, bool], ImageFont.ImageFont] = {}
        self.text_records: list[dict[str, object]] = []
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
        return {
            "name": self.view,
            "screenshot": screenshot.as_posix(),
            "labels": self.labels,
            "missing_required_labels": missing,
            "truncated_labels": [r for r in self.text_records if r["truncated"]],
            "overflow": [r for r in self.text_records if r["overflow"]],
            "text_count": len(self.text_records),
            "metrics": self.metrics,
        }


def draw_top_bar(s: Surface, snap: Snapshot):
    s.rect((0, 0, WIDTH, TOP_BAR_H), (11, 18, 28))
    s.text("MeshCore DeskOS", (16, 8, 190, 30), 18, TEXT, True)
    s.text(snap.node_name, (16, 30, 150, 49), 12, MUTED)
    s.text("RF ready", (202, 10, 280, 28), 12, GREEN, True, "center")
    s.text("Mesh ready", (286, 10, 380, 28), 12, ACCENT, True, "center")
    s.text(f"RX {snap.rx_total} TX {snap.tx_total}", (386, 10, 464, 28), 12, BLUE, True, "right")
    s.text("USB ok  Wi-Fi off  BLE off", (202, 31, 464, 49), 11, MUTED, align="right")
    s.line(((0, TOP_BAR_H), (WIDTH, TOP_BAR_H)))


def draw_dock(s: Surface, active: str):
    s.rect((0, DOCK_Y, WIDTH, HEIGHT), (10, 16, 25))
    tabs = [("Home", "Home"), ("Messages", "Msg"), ("Nodes", "Nodes"), ("Packets", "Packets"), ("Settings", "Set")]
    w = WIDTH // len(tabs)
    for i, (name, label) in enumerate(tabs):
        x0 = i * w
        x1 = WIDTH if i == len(tabs) - 1 else (i + 1) * w
        active_tab = name == active
        if active_tab:
            s.round_rect((x0 + 8, DOCK_Y + 8, x1 - 8, HEIGHT - 8), (29, 48, 62), (58, 88, 104), 8)
        s.text(label, (x0 + 6, DOCK_Y + 17, x1 - 6, HEIGHT - 15), 13, TEXT if active_tab else MUTED, active_tab, "center")


def draw_metric(s: Surface, box: tuple[int, int, int, int], title: str, value: str, detail: str, color: tuple[int, int, int] = ACCENT):
    x0, y0, x1, y1 = box
    s.round_rect(box)
    s.text(title, (x0 + 12, y0 + 8, x1 - 12, y0 + 28), 12, MUTED, True)
    s.text(value, (x0 + 12, y0 + 30, x1 - 12, y0 + 57), 20, color, True)
    s.text(detail, (x0 + 12, y1 - 27, x1 - 12, y1 - 8), 11, MUTED)


def draw_button(s: Surface, box: tuple[int, int, int, int], label: str, color: tuple[int, int, int] = ACCENT):
    s.round_rect(box, (24, 43, 54), (52, 92, 105), 8)
    s.text(label, (box[0] + 8, box[1] + 8, box[2] - 8, box[3] - 8), 14, color, True, "center")


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


def draw_row(s: Surface, box: tuple[int, int, int, int], title: str, detail: str, badge: str | None = None):
    x0, y0, x1, y1 = box
    s.round_rect(box, SURFACE_2, BORDER, 8)
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
        s.round_rect((x1 - 62, y0 + 9, x1 - 10, y0 + 31), (43, 58, 42), (80, 107, 77), 8)
        s.text(badge, (x1 - 58, y0 + 10, x1 - 14, y0 + 30), 11, GREEN, True, "center")


def draw_home_body(s: Surface, snap: Snapshot):
    draw_metric(s, (16, 68, 230, 144), "Radio", "US/CAN", snap.radio_profile, ACCENT)
    draw_metric(s, (250, 68, 464, 144), "Mesh", snap.mesh_state, f"{snap.latest_signal} latest", GREEN)
    draw_metric(s, (16, 156, 230, 232), "Identity", snap.node_name, snap.fingerprint, BLUE)
    draw_metric(s, (250, 156, 464, 232), "Unread", f"Public {snap.unread_public}", f"DM {snap.unread_dm}, tap Messages", AMBER)
    s.round_rect((16, 244, 464, 318), SURFACE, BORDER, 8)
    s.text("Latest Public", (28, 252, 180, 272), 13, MUTED, True)
    s.text("YKF Corebot: Public test reply received", (28, 276, 452, 300), 16, TEXT, True)
    s.text("RX new  RSSI -41  route direct", (28, 300, 452, 316), 11, MUTED)
    draw_button(s, (16, 332, 120, 388), "Send")
    draw_button(s, (128, 332, 232, 388), "Advert")
    draw_button(s, (248, 332, 352, 388), "Nodes", BLUE)
    draw_button(s, (360, 332, 464, 388), "Packets", AMBER)


def render_home(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    draw_home_body(s, snap)
    draw_dock(s, "Home")


def render_messages(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.text("Messages", (16, 64, 150, 92), 22, TEXT, True)
    draw_button(s, (172, 64, 226, 100), "Read", GREEN)
    draw_button(s, (234, 64, 316, 100), "Compose", ACCENT)
    draw_button(s, (324, 64, 400, 100), "History", BLUE)
    draw_button(s, (408, 64, 464, 100), "Test", AMBER)
    s.round_rect((16, 112, 464, 258))
    s.text("Public", (28, 120, 150, 142), 14, MUTED, True)
    y = 148
    public_rendered = 0
    for msg in snap.public_messages:
        if y + 30 > 252:
            break
        draw_row(s, (28, y, 452, y + 30), f"{msg.source}: {msg.text}", msg.meta, "new" if msg.unread else None)
        y += 34
        public_rendered += 1
    s.round_rect((16, 270, 464, 402))
    s.text("Direct", (28, 278, 150, 300), 14, MUTED, True)
    y = 306
    dm_rendered = 0
    for msg in snap.dm_messages:
        if y + 34 > 396:
            break
        draw_row(s, (28, y, 452, y + 34), f"{msg.source}: {msg.text}", msg.meta, "new" if msg.unread else None)
        y += 38
        dm_rendered += 1
    s.metrics.update(
        {
            "public_source_count": len(snap.public_messages),
            "public_rendered_count": public_rendered,
            "dm_source_count": len(snap.dm_messages),
            "dm_rendered_count": dm_rendered,
        }
    )
    draw_dock(s, "Messages")


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
        draw_row(s, (28, y, 374, y + 34), f"{node.name}  {node.role}", f"{node.fingerprint}  {node.signal}", None)
        draw_button(s, (384, y, 452, y + 34), "DM", GREEN)
        y += 40
        contacts_rendered += 1
    s.round_rect((16, 240, 464, 402))
    s.text("Heard Nodes", (28, 248, 180, 270), 14, MUTED, True)
    y = 276
    heard_rendered = 0
    for node in snap.heard:
        if y + 32 > 396:
            break
        draw_row(s, (28, y, 452, y + 32), f"{node.name}  {node.role}", f"{node.meta}  {node.signal}", None)
        y += 36
        heard_rendered += 1
    s.metrics.update(
        {
            "contacts_source_count": len(snap.contacts),
            "contacts_rendered_count": contacts_rendered,
            "heard_source_count": len(snap.heard),
            "heard_rendered_count": heard_rendered,
        }
    )
    draw_dock(s, "Nodes")


def render_packets(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.text("Packets", (16, 64, 150, 92), 22, TEXT, True)
    draw_metric(s, (16, 104, 230, 176), "Signal", snap.latest_signal, "3 recent packets", GREEN)
    draw_metric(s, (250, 104, 464, 176), "Mesh Roles", "1 room / 1 repeater", "tap for role browser", ACCENT)
    for i, label in enumerate(("All", "RX", "TX", "Text")):
        draw_button(s, (16 + i * 64, 188, 72 + i * 64, 222), label, GREEN if label == "All" else ACCENT)
    draw_button(s, (286, 188, 366, 222), "Search", BLUE)
    s.text("find raw/test", (374, 194, 464, 216), 11, AMBER)
    s.round_rect((16, 232, 464, 326))
    s.text("Packet Feed", (28, 240, 180, 262), 14, MUTED, True)
    y = 268
    for packet in snap.packets[:2]:
        draw_row(s, (28, y, 452, y + 25), f"{packet.kind} {packet.direction}", f"{packet.meta}  {packet.note}")
        y += 29
    s.round_rect((16, 336, 464, 402))
    s.text("Routes", (28, 344, 180, 366), 14, MUTED, True)
    y = 372
    for route in snap.routes[:1]:
        draw_row(s, (28, y, 452, y + 25), f"{route.kind} {route.direction}", f"{route.meta}  {route.note}")
    draw_dock(s, "Packets")


def render_settings(s: Surface, snap: Snapshot):
    draw_top_bar(s, snap)
    s.text("Settings", (16, 64, 150, 92), 22, TEXT, True)
    draw_metric(s, (16, 104, 464, 166), "Radio", snap.radio_profile, "TX 20 dBm, TCXO NONE", ACCENT)
    draw_metric(s, (16, 176, 464, 238), "Identity", snap.node_name, snap.fingerprint, BLUE)
    draw_metric(s, (16, 248, 464, 310), "Companion", "USB ready", "Wi-Fi off, BLE off, offline-first", GREEN)
    draw_metric(s, (16, 320, 230, 394), "Storage", snap.storage_backend, snap.storage_detail, AMBER)
    draw_button(s, (44, 356, 202, 386), "Storage", AMBER)
    draw_button(s, (250, 320, 354, 394), "Radio", ACCENT)
    draw_button(s, (364, 320, 464, 394), "Advert", ACCENT)
    draw_dock(s, "Settings")


def draw_sheet_frame(s: Surface, title: str, subtitle: str | None = None):
    draw_top_bar(s, sample_snapshot())
    draw_home_body(s, sample_snapshot())
    s.rect((0, TOP_BAR_H, WIDTH, DOCK_Y), DIM)
    s.round_rect((24, 78, 456, 392), (18, 27, 39), (72, 92, 112), 8)
    s.text(title, (44, 94, 330, 122), 22, TEXT, True)
    if subtitle:
        s.text(subtitle, (44, 124, 436, 146), 12, MUTED)


def render_compose_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Compose Public", "On-screen keyboard entry surface")
    s.round_rect((44, 158, 436, 228), SURFACE_2, BORDER, 8)
    s.text("Public message", (56, 166, 220, 188), 13, MUTED, True)
    s.text("test from DeskOS D1L", (56, 194, 424, 222), 18, TEXT)
    for i, label in enumerate(("Quick", "Clear", "Send")):
        draw_button(s, (44 + i * 132, 248, 164 + i * 132, 300), label, GREEN if label == "Send" else ACCENT)
    draw_button(s, (44, 320, 200, 370), "Close", MUTED)
    draw_dock(s, "Messages")


def render_public_history_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Public History", f"retained {len(snap.public_messages)} rows")
    draw_button(s, (204, 94, 282, 134), "Search", BLUE)
    draw_button(s, (292, 94, 356, 134), "Clear", ACCENT)
    draw_button(s, (366, 94, 436, 134), "Close", MUTED)
    s.round_rect((44, 154, 436, 318), SURFACE, BORDER, 8)
    s.text("Public scrollback", (56, 162, 260, 184), 13, MUTED, True)
    visible_messages = snap.public_messages[-3:]
    y = 194
    for msg in visible_messages:
        draw_row(s, (56, y, 424, y + 34), f"{msg.source}: {msg.text}", msg.meta, "new" if msg.unread else None)
        y += 40
    s.text("Search filters retained author, direction, and text rows.", (44, 332, 436, 354), 12, MUTED)
    draw_dock(s, "Messages")
    s.metrics.update(
        {
            "public_history_source_count": len(snap.public_messages),
            "public_history_rendered_count": len(visible_messages),
        }
    )


def render_public_search_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Public Search", "Filter retained Public rows")
    s.round_rect((44, 158, 436, 210), SURFACE_2, BORDER, 8)
    s.text("Search author or message", (56, 166, 424, 190), 13, MUTED, True)
    s.text("test", (56, 188, 424, 206), 16, TEXT)
    draw_button(s, (44, 228, 156, 278), "Apply", GREEN)
    draw_button(s, (166, 228, 278, 278), "Clear", ACCENT)
    draw_button(s, (288, 228, 400, 278), "Close", MUTED)
    s.round_rect((44, 300, 436, 370), SURFACE, BORDER, 8)
    s.text("Keyboard opens for Public history search", (56, 318, 424, 350), 13, MUTED, False, "center")
    draw_dock(s, "Messages")


def render_contact_detail_sheet(s: Surface, snap: Snapshot):
    contact = snap.contacts[0]
    draw_sheet_frame(s, "Contact Detail", contact.name)
    s.text("Fingerprint", (44, 154, 180, 174), 13, MUTED, True)
    s.text(contact.fingerprint, (44, 176, 436, 200), 17, TEXT)
    s.text("Signal", (44, 210, 180, 230), 13, MUTED, True)
    s.text(f"{contact.signal}  {contact.meta}", (44, 232, 436, 254), 14, GREEN)
    buttons = (("DM", GREEN), ("Trace", BLUE), ("Export", ACCENT), ("Fav", ACCENT), ("Mute", ACCENT))
    for i, (label, color) in enumerate(buttons):
        draw_button(s, (44 + i * 76, 278, 112 + i * 76, 330), label, color)
    draw_button(s, (44, 346, 200, 378), "Close", MUTED)
    draw_dock(s, "Nodes")


def render_radio_settings_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Radio Settings", "Saved profile applies after reboot")
    s.text(snap.radio_profile, (44, 154, 436, 178), 14, GREEN, True)
    s.text("Freq 910.525 MHz", (44, 190, 220, 212), 13, TEXT, True)
    draw_button(s, (244, 184, 316, 220), "-25k", ACCENT)
    draw_button(s, (324, 184, 396, 220), "+25k", ACCENT)
    s.text("BW 62.5 kHz", (44, 232, 220, 254), 13, TEXT, True)
    draw_button(s, (244, 226, 396, 262), "Cycle BW", ACCENT)
    s.text("SF 7", (44, 274, 98, 296), 13, TEXT, True)
    draw_button(s, (106, 268, 166, 304), "SF-", ACCENT)
    draw_button(s, (174, 268, 234, 304), "SF+", ACCENT)
    s.text("CR 5", (250, 274, 304, 296), 13, TEXT, True)
    draw_button(s, (312, 268, 396, 304), "Cycle", ACCENT)
    s.text("TX 20 dBm", (44, 318, 136, 340), 13, TEXT, True)
    draw_button(s, (146, 312, 206, 348), "TX-", ACCENT)
    draw_button(s, (214, 312, 274, 348), "TX+", ACCENT)
    draw_button(s, (282, 312, 416, 348), "RX Boost On", GREEN)
    draw_button(s, (44, 356, 136, 386), "US/CAN", BLUE)
    draw_button(s, (146, 356, 238, 386), "Save", GREEN)
    draw_button(s, (248, 356, 340, 386), "Close", MUTED)
    draw_dock(s, "Settings")


def render_storage_setup_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Storage Setup", "Optional SD data storage")
    draw_button(s, (356, 94, 436, 134), "Close", MUTED)
    draw_metric(s, (44, 154, 436, 214), "SD Card", snap.storage_state, snap.storage_detail, AMBER)
    draw_metric(s, (44, 226, 436, 286), "Backends", snap.storage_backend, "messages NVS / packets NVS / routes NVS", BLUE)
    s.text(f"setup {snap.storage_setup_action}", (44, 302, 436, 324), 14, TEXT, True)
    s.text(f"format {snap.storage_format_action}", (44, 328, 436, 350), 13, MUTED)
    s.text("No automatic format. Confirmation required before SD setup.", (44, 358, 436, 380), 12, AMBER)
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
    draw_button(s, (44, 340, 200, 374), "Close", MUTED)
    draw_dock(s, "Nodes")


def render_dm_thread_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "DM Thread", "YKF Corebot")
    s.text(f"Thread {len(snap.dm_messages)} rows", (44, 132, 436, 150), 12, MUTED)
    visible_messages = snap.dm_messages[-3:]
    y = 154
    for msg in visible_messages:
        draw_row(s, (44, y, 436, y + 42), f"{msg.source}: {msg.text}", msg.meta, "new" if msg.unread else None)
        y += 50
    draw_button(s, (44, 304, 160, 356), "Reply", GREEN)
    draw_button(s, (174, 304, 290, 356), "Read", ACCENT)
    draw_button(s, (304, 304, 420, 356), "Close", MUTED)
    draw_dock(s, "Messages")
    s.metrics.update(
        {
            "dm_thread_source_count": len(snap.dm_messages),
            "dm_thread_rendered_count": len(visible_messages),
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
    draw_button(s, (44, 340, 200, 374), "Close", MUTED)
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
    draw_button(s, (316, 94, 436, 134), "Close", MUTED)
    s.text("Local evidence only", (44, 390, 300, 408), 11, MUTED)
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
    s.text("Kind", (44, 154, 160, 174), 13, MUTED, True)
    s.text(f"{packet.kind} {packet.direction}", (44, 176, 436, 200), 18, TEXT)
    s.text("Signal", (44, 210, 160, 230), 13, MUTED, True)
    s.text(packet.meta, (44, 232, 436, 256), 16, GREEN)
    s.text("Payload", (44, 270, 180, 290), 13, MUTED, True)
    s.text("parsed MeshCore text packet", (44, 292, 436, 316), 16, BLUE)
    s.text("Raw Hex", (44, 324, 180, 344), 13, MUTED, True)
    s.text(packet.raw_hex, (44, 346, 436, 368), 14, BLUE)
    draw_button(s, (44, 374, 200, 402), "Close", MUTED)
    draw_dock(s, "Packets")


def render_packet_search_sheet(s: Surface, snap: Snapshot):
    draw_sheet_frame(s, "Packet Search", "Filter kind, note, or raw hex")
    s.round_rect((44, 158, 436, 210), SURFACE_2, BORDER, 8)
    s.text("Search kind, note, raw hex", (56, 166, 424, 190), 13, MUTED, True)
    s.text("test", (56, 188, 424, 206), 16, TEXT)
    draw_button(s, (44, 228, 156, 278), "Apply", GREEN)
    draw_button(s, (166, 228, 278, 278), "Clear", ACCENT)
    draw_button(s, (288, 228, 400, 278), "Close", MUTED)
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
    draw_button(s, (44, 340, 200, 374), "Close", MUTED)
    draw_dock(s, "Packets")


def render_lock_overlay(s: Surface, snap: Snapshot):
    s.rect((0, 0, WIDTH, HEIGHT), (4, 8, 13))
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
    s.text(snap.node_name, (46, 152, 430, 180), 18, TEXT)
    s.text("Canada/USA preset confirmed", (32, 204, 430, 226), 14, TEXT, True)
    s.text("910.525 BW62.5 SF7 CR5", (32, 228, 430, 250), 13, MUTED)
    s.text("Role Desk Companion", (32, 260, 430, 282), 14, TEXT, True)
    s.text("Wi-Fi off  BLE off  Observer off", (32, 284, 430, 306), 13, MUTED)
    draw_button(s, (32, 324, 148, 374), "Start", GREEN)
    draw_button(s, (164, 324, 310, 374), "Use Defaults", ACCENT)
    s.round_rect((32, 392, 448, 432), SURFACE, BORDER, 8)
    s.text("Keyboard opens for name editing", (44, 400, 436, 424), 13, MUTED, False, "center")


RENDERERS: dict[str, Callable[[Surface, Snapshot], None]] = {
    "home": render_home,
    "messages": render_messages,
    "nodes": render_nodes,
    "packets": render_packets,
    "settings": render_settings,
    "compose_sheet": render_compose_sheet,
    "public_history_sheet": render_public_history_sheet,
    "public_search_sheet": render_public_search_sheet,
    "radio_settings_sheet": render_radio_settings_sheet,
    "storage_setup_sheet": render_storage_setup_sheet,
    "contact_detail_sheet": render_contact_detail_sheet,
    "contact_export_sheet": render_contact_export_sheet,
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
    "home": ("MeshCore DeskOS", "Home", "Radio", "US/CAN", "Mesh", "Identity", "Unread", "Send", "Advert"),
    "messages": ("Messages", "Read", "Compose", "History", "Test", "Public", "Direct"),
    "nodes": ("Nodes", "Contacts", "Heard Nodes", "DM"),
    "packets": ("Packets", "Signal", "Mesh Roles", "All", "RX", "TX", "Text", "Search", "Packet Feed", "Routes"),
    "settings": ("Settings", "Radio", "Identity", "Companion", "Storage", "Advert"),
    "compose_sheet": ("Compose Public", "Public message", "Send", "Close"),
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
        "Storage Setup",
        "Optional SD data storage",
        "SD Card",
        "Backends",
        "setup bridge_protocol_pending",
        "format not_available",
        "No automatic format. Confirmation required before SD setup.",
        "Close",
    ),
    "contact_detail_sheet": ("Contact Detail", "Fingerprint", "Signal", "DM", "Trace", "Export", "Fav", "Mute", "Close"),
    "contact_export_sheet": ("Contact Export", "MeshCore QR", "Fingerprint", "URI", "Close"),
    "dm_thread_sheet": ("DM Thread", "Reply", "Read", "Close"),
    "route_detail_sheet": ("Route Detail", "Target", "Path", "Confidence", "Close"),
    "route_trace_sheet": ("Route Trace", "Trace", "Contact Path", "Best Evidence", "Close"),
    "packet_detail_sheet": ("Packet Detail", "Kind", "Signal", "Payload", "Raw Hex", "Close"),
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
        for label in summary["missing_required_labels"]:
            required_missing.append({"view": view, "label": label})
        report_views.append(summary)

    report = {
        "schema": 1,
        "ok": overflow_count == 0 and not required_missing,
        "display": {"width": WIDTH, "height": HEIGHT},
        "source": "tools/ui_simulator.py",
        "scenario": scenario,
        "snapshot_counts": snapshot_counts(snap),
        "views": report_views,
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
