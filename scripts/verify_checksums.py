#!/usr/bin/env python3
"""Verify .sha256 files under an artifact directory."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256_file(path: Path) -> bool:
    text = path.read_text(encoding="ascii").strip()
    expected, filename = text.split(maxsplit=1)
    target = path.parent / filename
    return target.exists() and sha256_file(target).lower() == expected.lower()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact_dir", nargs="?", default="artifacts")
    args = parser.parse_args()

    root = Path(args.artifact_dir)
    sha_files = list(root.rglob("*.sha256"))
    if not sha_files:
        print("No checksum files found.")
        return 0
    failed = [path for path in sha_files if not verify_sha256_file(path)]
    for path in sha_files:
        print(f"{'FAIL' if path in failed else 'OK'} {path}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
