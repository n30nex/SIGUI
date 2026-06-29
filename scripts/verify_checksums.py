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


def verify_sha256_manifest(path: Path) -> bool:
    for line in path.read_text(encoding="ascii").splitlines():
        line = line.strip()
        if not line:
            continue
        expected, filename = line.split(maxsplit=1)
        filename = filename.removeprefix("./")
        target = path.parent / filename
        if not target.exists() or sha256_file(target).lower() != expected.lower():
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact_dir", nargs="?", default="artifacts")
    args = parser.parse_args()

    root = Path(args.artifact_dir)
    sha_files = list(root.rglob("*.sha256"))
    manifests = list(root.rglob("SHA256SUMS.txt"))
    if not sha_files and not manifests:
        print("No checksum files found.")
        return 0
    failed = [path for path in sha_files if not verify_sha256_file(path)]
    failed.extend(path for path in manifests if not verify_sha256_manifest(path))
    for path in sha_files:
        print(f"{'FAIL' if path in failed else 'OK'} {path}")
    for path in manifests:
        print(f"{'FAIL' if path in failed else 'OK'} {path}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
