#!/usr/bin/env python3
"""Capture the D1L hardware UI shadow framebuffer over the JSONL console."""

from __future__ import annotations

import argparse
import binascii
import json
import os
import struct
import sys
import time
import zlib
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from artifact_metadata import stamp_report
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import send_console_command


WIDTH = 480
HEIGHT = 480
BYTES_PER_PIXEL = 2
TOTAL_BYTES = WIDTH * HEIGHT * BYTES_PER_PIXEL
MAX_CHUNK_BYTES = 1024
CAPTURE_COMMANDS = [
    "ui capture status",
    "ui capture begin",
    "ui capture chunk 0 1024",
    "ui capture end",
]


def crc32_hex(data: bytes | bytearray) -> str:
    return f"{binascii.crc32(data) & 0xFFFFFFFF:08X}"


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)
    )


def rgb565_le_to_rgb_rows(raw: bytes, width: int, height: int) -> bytes:
    rows = bytearray()
    stride = width * BYTES_PER_PIXEL
    for y in range(height):
        rows.append(0)
        row = raw[y * stride : (y + 1) * stride]
        for i in range(0, len(row), 2):
            pixel = row[i] | (row[i + 1] << 8)
            r5 = (pixel >> 11) & 0x1F
            g6 = (pixel >> 5) & 0x3F
            b5 = pixel & 0x1F
            rows.append((r5 << 3) | (r5 >> 2))
            rows.append((g6 << 2) | (g6 >> 4))
            rows.append((b5 << 3) | (b5 >> 2))
    return bytes(rows)


def write_png_from_rgb565_le(raw: bytes, path: Path, width: int, height: int) -> None:
    if len(raw) != width * height * BYTES_PER_PIXEL:
        raise ValueError(f"raw length {len(raw)} does not match {width}x{height} RGB565")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    payload = rgb565_le_to_rgb_rows(raw, width, height)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(payload, level=6))
        + png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def default_json_path(root: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return root / "artifacts" / "ui-capture" / f"d1l-ui-capture-{stamp}.json"


def resolve_output_paths(root: Path, out: str | None, png_out: str | None, raw_out: str | None) -> tuple[Path, Path, Path]:
    if out:
        out_path = Path(out)
        if not out_path.is_absolute():
            out_path = root / out_path
        if out_path.suffix.lower() == ".png":
            json_path = out_path.with_suffix(".json")
            png_path = out_path
        else:
            json_path = out_path
            png_path = out_path.with_suffix(".png")
    else:
        json_path = default_json_path(root)
        png_path = json_path.with_suffix(".png")
    if png_out:
        png_path = Path(png_out)
        if not png_path.is_absolute():
            png_path = root / png_path
    raw_path = Path(raw_out) if raw_out else json_path.with_suffix(".rgb565")
    if not raw_path.is_absolute():
        raw_path = root / raw_path
    return json_path, png_path, raw_path


def dry_run_report(chunk_size: int) -> dict[str, Any]:
    return {
        "schema": 1,
        "kind": "ui_pixel_capture",
        "mode": "dry-run",
        "ok": True,
        "hardware_required": False,
        "explicit_port_required": True,
        "commands": CAPTURE_COMMANDS,
        "width": WIDTH,
        "height": HEIGHT,
        "bytes_per_pixel": BYTES_PER_PIXEL,
        "pixel_format": "rgb565-le",
        "total_bytes": TOTAL_BYTES,
        "chunk_size": min(chunk_size, MAX_CHUNK_BYTES),
        "public_rf_tx": False,
        "formats_sd": False,
    }


def require_capture_geometry(begin: dict[str, Any]) -> tuple[int, int, int]:
    width = int(begin.get("width") or 0)
    height = int(begin.get("height") or 0)
    total = int(begin.get("total_bytes") or 0)
    if width != WIDTH or height != HEIGHT or total != TOTAL_BYTES:
        raise ValueError(f"unexpected capture geometry {width}x{height} total={total}")
    if begin.get("pixel_format") != "rgb565-le":
        raise ValueError(f"unexpected pixel format {begin.get('pixel_format')}")
    return width, height, total


def capture_frame(port: str, baud: int, timeout: float, chunk_size: int) -> dict[str, Any]:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware UI capture: python -m pip install pyserial") from exc

    chunk_size = max(1, min(chunk_size, MAX_CHUNK_BYTES))
    events: list[dict[str, Any]] = []
    raw = bytearray()
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        status = send_console_command(ser, "ui capture status", timeout)
        events.append(status)
        begin = send_console_command(ser, "ui capture begin", timeout)
        events.append(begin)
        if begin.get("ok") is not True:
            return {
                "schema": 1,
                "kind": "ui_pixel_capture",
                "mode": "hardware",
                "ok": False,
                "port": port,
                "baud": baud,
                "events": events,
                "error": "ui_capture_begin_failed",
                "public_rf_tx": False,
                "formats_sd": False,
            }
        width, height, total = require_capture_geometry(begin)
        while len(raw) < total:
            requested = min(chunk_size, total - len(raw))
            command = f"ui capture chunk {len(raw)} {requested}"
            chunk = send_console_command(ser, command, timeout)
            events.append({key: chunk.get(key) for key in ("ok", "cmd", "offset", "len", "chunk_crc32")})
            if chunk.get("ok") is not True:
                send_console_command(ser, "ui capture end", timeout)
                raise RuntimeError(f"capture chunk failed at offset {len(raw)}: {chunk}")
            data_hex = str(chunk.get("data_hex") or "")
            data = bytes.fromhex(data_hex)
            if len(data) != int(chunk.get("len") or -1):
                raise RuntimeError(f"chunk length mismatch at offset {len(raw)}")
            if crc32_hex(data) != str(chunk.get("chunk_crc32") or "").upper():
                raise RuntimeError(f"chunk crc mismatch at offset {len(raw)}")
            raw.extend(data)
        end = send_console_command(ser, "ui capture end", timeout)
        events.append(end)

    capture_crc32 = crc32_hex(raw)
    expected_crc32 = str(begin.get("crc32") or "").upper()
    ok = len(raw) == total and (not expected_crc32 or capture_crc32 == expected_crc32)
    return {
        "schema": 1,
        "kind": "ui_pixel_capture",
        "mode": "hardware",
        "ok": ok,
        "port": port,
        "baud": baud,
        "width": width,
        "height": height,
        "bytes_per_pixel": BYTES_PER_PIXEL,
        "pixel_format": "rgb565-le",
        "total_bytes": total,
        "captured_bytes": len(raw),
        "chunk_size": chunk_size,
        "chunk_count": (len(raw) + chunk_size - 1) // chunk_size,
        "crc32": capture_crc32,
        "firmware_crc32": expected_crc32,
        "frame_seq": begin.get("frame_seq"),
        "flush_count": begin.get("flush_count"),
        "active_tab": begin.get("active_tab"),
        "pending_tab": begin.get("pending_tab"),
        "onboarding_visible": begin.get("onboarding_visible"),
        "lock_visible": begin.get("lock_visible"),
        "events": events,
        "raw_bytes": raw,
        "public_rf_tx": False,
        "formats_sd": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--chunk-size", type=int, default=MAX_CHUNK_BYTES)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None, help="JSON report path, or .png path to derive the JSON beside it")
    parser.add_argument("--png-out", default=None)
    parser.add_argument("--raw-out", default=None)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    json_path, png_path, raw_path = resolve_output_paths(root, args.out, args.png_out, args.raw_out)
    if args.dry_run:
        report = dry_run_report(args.chunk_size)
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = capture_frame(args.port, args.baud, args.timeout, args.chunk_size)
        raw = bytes(report.pop("raw_bytes", b""))
        if report.get("ok") is True:
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_bytes(raw)
            write_png_from_rgb565_le(raw, png_path, int(report["width"]), int(report["height"]))
            report["raw_path"] = str(raw_path)
            report["png_path"] = str(png_path)
    stamp_report(report, root)
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps({"ok": report.get("ok"), "path": str(json_path), "png_path": report.get("png_path")}))
    return 0 if report.get("ok") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
