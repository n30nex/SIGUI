#!/usr/bin/env python3
"""Compare a D1L hardware UI capture PNG with a simulator/reference PNG."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

try:
    from artifact_metadata import stamp_report
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report


WIDTH = 480
HEIGHT = 480
DEFAULT_PIXEL_TOLERANCE = 36
DEFAULT_MAX_DIFFERENT_RATIO = 0.60
DEFAULT_MAX_MEAN_ABS_DELTA = 50.0
DEFAULT_TILE_SIZE = 24
DEFAULT_TILE_DELTA_THRESHOLD = 72.0
DEFAULT_MAX_TILE_MISMATCH_RATIO = 0.38


def load_rgb_image(path: Path):
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise RuntimeError("Pillow is required for UI capture diff: python -m pip install pillow") from exc

    with Image.open(path) as image:
        return image.convert("RGB")


def tile_mean_delta(
    capture: bytes,
    reference: bytes,
    width: int,
    x0: int,
    y0: int,
    tile_w: int,
    tile_h: int,
) -> float:
    delta = 0
    count = 0
    for y in range(y0, y0 + tile_h):
        row_offset = y * width * 3
        for x in range(x0, x0 + tile_w):
            offset = row_offset + x * 3
            delta += abs(capture[offset] - reference[offset])
            delta += abs(capture[offset + 1] - reference[offset + 1])
            delta += abs(capture[offset + 2] - reference[offset + 2])
            count += 3
    return delta / count if count else 0.0


def compare_pngs(
    capture_png: Path,
    reference_png: Path,
    *,
    reference_view: str | None = None,
    pixel_tolerance: int = DEFAULT_PIXEL_TOLERANCE,
    max_different_ratio: float = DEFAULT_MAX_DIFFERENT_RATIO,
    max_mean_abs_delta: float = DEFAULT_MAX_MEAN_ABS_DELTA,
    tile_size: int = DEFAULT_TILE_SIZE,
    tile_delta_threshold: float = DEFAULT_TILE_DELTA_THRESHOLD,
    max_tile_mismatch_ratio: float = DEFAULT_MAX_TILE_MISMATCH_RATIO,
) -> dict[str, Any]:
    capture_png = capture_png.resolve()
    reference_png = reference_png.resolve()
    capture = load_rgb_image(capture_png)
    reference = load_rgb_image(reference_png)
    width, height = capture.size
    ref_width, ref_height = reference.size
    size_match = (width, height) == (ref_width, ref_height)

    report: dict[str, Any] = {
        "schema": 1,
        "kind": "ui_capture_simulator_diff",
        "ok": False,
        "capture_png_path": str(capture_png),
        "reference_png_path": str(reference_png),
        "reference_view": reference_view,
        "width": width,
        "height": height,
        "reference_width": ref_width,
        "reference_height": ref_height,
        "size_match": size_match,
        "public_rf_tx": False,
        "formats_sd": False,
        "thresholds": {
            "pixel_tolerance": pixel_tolerance,
            "max_different_ratio": max_different_ratio,
            "max_mean_abs_delta": max_mean_abs_delta,
            "tile_size": tile_size,
            "tile_delta_threshold": tile_delta_threshold,
            "max_tile_mismatch_ratio": max_tile_mismatch_ratio,
        },
    }
    if not size_match:
        report["error"] = "image_size_mismatch"
        return report

    capture_bytes = capture.tobytes()
    reference_bytes = reference.tobytes()
    total_pixels = width * height
    different_pixels = 0
    total_channel_delta = 0
    max_channel_delta = 0

    for offset in range(0, len(capture_bytes), 3):
        dr = abs(capture_bytes[offset] - reference_bytes[offset])
        dg = abs(capture_bytes[offset + 1] - reference_bytes[offset + 1])
        db = abs(capture_bytes[offset + 2] - reference_bytes[offset + 2])
        pixel_delta = max(dr, dg, db)
        if pixel_delta > pixel_tolerance:
            different_pixels += 1
        total_channel_delta += dr + dg + db
        max_channel_delta = max(max_channel_delta, pixel_delta)

    tile_size = max(1, tile_size)
    tile_count = 0
    tile_mismatch_count = 0
    max_tile_mean_delta = 0.0
    for y in range(0, height, tile_size):
        tile_h = min(tile_size, height - y)
        for x in range(0, width, tile_size):
            tile_w = min(tile_size, width - x)
            mean_delta = tile_mean_delta(capture_bytes, reference_bytes, width, x, y, tile_w, tile_h)
            tile_count += 1
            max_tile_mean_delta = max(max_tile_mean_delta, mean_delta)
            if mean_delta > tile_delta_threshold:
                tile_mismatch_count += 1

    different_ratio = different_pixels / total_pixels if total_pixels else 1.0
    mean_abs_delta = total_channel_delta / (total_pixels * 3) if total_pixels else 255.0
    tile_mismatch_ratio = tile_mismatch_count / tile_count if tile_count else 1.0
    report.update(
        {
            "different_pixels": different_pixels,
            "total_pixels": total_pixels,
            "different_pixel_ratio": round(different_ratio, 6),
            "mean_abs_delta": round(mean_abs_delta, 3),
            "max_channel_delta": max_channel_delta,
            "tile_count": tile_count,
            "tile_mismatch_count": tile_mismatch_count,
            "tile_mismatch_ratio": round(tile_mismatch_ratio, 6),
            "max_tile_mean_delta": round(max_tile_mean_delta, 3),
        }
    )
    report["ok"] = (
        different_ratio <= max_different_ratio
        and mean_abs_delta <= max_mean_abs_delta
        and tile_mismatch_ratio <= max_tile_mismatch_ratio
    )
    if not report["ok"]:
        report["error"] = "material_pixel_difference"
    return report


def write_diff_png(capture_png: Path, reference_png: Path, out: Path) -> None:
    try:
        from PIL import ImageChops
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise RuntimeError("Pillow is required for UI capture diff: python -m pip install pillow") from exc

    capture = load_rgb_image(capture_png)
    reference = load_rgb_image(reference_png)
    if capture.size != reference.size:
        raise ValueError("image_size_mismatch")
    out.parent.mkdir(parents=True, exist_ok=True)
    ImageChops.difference(capture, reference).save(out)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare D1L hardware capture PNG to a simulator/reference PNG.")
    parser.add_argument("--capture-png", required=True, type=Path)
    parser.add_argument("--reference-png", required=True, type=Path)
    parser.add_argument("--reference-view")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--diff-png-out", type=Path)
    parser.add_argument("--pixel-tolerance", type=int, default=DEFAULT_PIXEL_TOLERANCE)
    parser.add_argument("--max-different-ratio", type=float, default=DEFAULT_MAX_DIFFERENT_RATIO)
    parser.add_argument("--max-mean-abs-delta", type=float, default=DEFAULT_MAX_MEAN_ABS_DELTA)
    parser.add_argument("--tile-size", type=int, default=DEFAULT_TILE_SIZE)
    parser.add_argument("--tile-delta-threshold", type=float, default=DEFAULT_TILE_DELTA_THRESHOLD)
    parser.add_argument("--max-tile-mismatch-ratio", type=float, default=DEFAULT_MAX_TILE_MISMATCH_RATIO)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    report = compare_pngs(
        args.capture_png,
        args.reference_png,
        reference_view=args.reference_view,
        pixel_tolerance=args.pixel_tolerance,
        max_different_ratio=args.max_different_ratio,
        max_mean_abs_delta=args.max_mean_abs_delta,
        tile_size=args.tile_size,
        tile_delta_threshold=args.tile_delta_threshold,
        max_tile_mismatch_ratio=args.max_tile_mismatch_ratio,
    )
    if args.diff_png_out and report.get("size_match") is True:
        write_diff_png(args.capture_png, args.reference_png, args.diff_png_out)
        report["diff_png_path"] = str(args.diff_png_out.resolve())
    stamp_report(report, root)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="ascii")
    print(json.dumps({"ok": report.get("ok"), "different_pixel_ratio": report.get("different_pixel_ratio")}))
    return 0 if report.get("ok") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
