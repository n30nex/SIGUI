#!/usr/bin/env python3
"""Generate or verify the flat SIGUI completion-pack SHA-256 manifest."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


DEFAULT_PACK_DIR = Path("docs/completion")
MANIFEST_NAME = "SHA256SUMS.txt"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def completion_pack_files(pack_dir: Path) -> list[Path]:
    if not pack_dir.is_dir():
        raise ValueError(f"completion pack directory does not exist: {pack_dir}")
    paths = sorted(
        (
            path
            for path in pack_dir.iterdir()
            if path.is_file() and path.name != MANIFEST_NAME
        ),
        key=lambda path: path.name,
    )
    if not paths:
        raise ValueError("completion pack contains no manifest inputs")
    for path in paths:
        if path.is_symlink():
            raise ValueError(f"completion pack input must not be a symlink: {path.name}")
        if any(character in path.name for character in "\r\n/\\"):
            raise ValueError(f"invalid completion pack filename: {path.name!r}")
    return paths


def render_manifest(pack_dir: Path) -> str:
    return "".join(
        f"{sha256(path)}  {path.name}\n" for path in completion_pack_files(pack_dir)
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("generate", "check"))
    parser.add_argument("--pack-dir", type=Path, default=DEFAULT_PACK_DIR)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    pack_dir = args.pack_dir.resolve()
    manifest = pack_dir / MANIFEST_NAME
    try:
        expected = render_manifest(pack_dir)
    except (OSError, ValueError) as exc:
        print(f"ERROR {exc}")
        return 1
    if args.command == "generate":
        manifest.write_text(expected, encoding="ascii", newline="\n")
        print(f"Wrote {manifest}")
        return 0
    try:
        actual = manifest.read_text(encoding="ascii")
    except (OSError, UnicodeError) as exc:
        print(f"ERROR cannot read {manifest}: {exc}")
        return 1
    if actual != expected:
        print(f"ERROR stale completion pack manifest: {manifest}")
        return 1
    print(f"PASS {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
