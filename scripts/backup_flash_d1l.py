#!/usr/bin/env python3
"""Read and checksum a D1L flash backup with an explicit serial port."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path


SIZE_ALIASES = {
    "4MB": 4 * 1024 * 1024,
    "8MB": 8 * 1024 * 1024,
    "16MB": 16 * 1024 * 1024,
}


def parse_size(value: str) -> int:
    value = value.strip()
    upper = value.upper()
    if upper in SIZE_ALIASES:
        return SIZE_ALIASES[upper]
    return int(value, 0)


def build_read_flash_command(port: str, size: int, output: Path) -> list[str]:
    return [
        "python",
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "--port",
        port,
        "read_flash",
        "0x0",
        str(size),
        str(output),
    ]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--size", default="8MB", help="Flash size to read, e.g. 8MB or 0x800000")
    parser.add_argument("--out-dir", default="artifacts/backups")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not args.port:
        parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")

    root = Path(__file__).resolve().parents[1]
    out_dir = root / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = out_dir / f"d1l-flash-{stamp}.bin"
    size = parse_size(args.size)
    command = build_read_flash_command(args.port, size, output)

    metadata = {
        "schema": 1,
        "created_at": stamp,
        "port": args.port,
        "chip": "esp32s3",
        "size": size,
        "output": str(output),
        "command": command,
        "dry_run": args.dry_run,
    }

    if not args.dry_run:
        subprocess.run(command, check=True)
        digest = sha256_file(output)
        metadata["sha256"] = digest
        output.with_suffix(output.suffix + ".sha256").write_text(f"{digest}  {output.name}\n", encoding="ascii")

    meta_path = output.with_suffix(output.suffix + ".json")
    meta_path.write_text(json.dumps(metadata, indent=2), encoding="ascii")
    print(json.dumps(metadata))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
