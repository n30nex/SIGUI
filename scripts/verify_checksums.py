#!/usr/bin/env python3
"""Verify .sha256 files under an artifact directory."""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def strip_leading_current_dir(filename: str) -> str:
    return filename[2:] if filename.startswith("./") else filename


def safe_checksum_target(parent: Path, filename: str) -> Path | None:
    filename = strip_leading_current_dir(filename)
    if not filename or "\\" in filename:
        return None
    relative = Path(filename)
    if relative.is_absolute() or any(part in {"", ".", ".."} for part in relative.parts):
        return None
    try:
        target = (parent / relative).resolve()
        target.relative_to(parent.resolve())
    except (OSError, RuntimeError, ValueError):
        return None
    return target


def parse_checksum_row(line: str, parent: Path) -> tuple[str, Path] | None:
    try:
        expected, filename = line.split(maxsplit=1)
    except ValueError:
        return None
    if re.fullmatch(r"[0-9a-fA-F]{64}", expected) is None:
        return None
    target = safe_checksum_target(parent, filename)
    if target is None:
        return None
    return expected.lower(), target


def verify_sha256_file(path: Path) -> bool:
    try:
        lines = [line.strip() for line in path.read_text(encoding="ascii").splitlines() if line.strip()]
    except (OSError, UnicodeError):
        return False
    if len(lines) != 1:
        return False
    row = parse_checksum_row(lines[0], path.parent)
    if row is None:
        return False
    expected, target = row
    try:
        return target.is_file() and sha256_file(target).lower() == expected
    except OSError:
        return False


def verify_sha256_manifest(path: Path) -> bool:
    try:
        manifest = path.resolve()
        lines = [line.strip() for line in path.read_text(encoding="ascii").splitlines() if line.strip()]
    except (OSError, UnicodeError):
        return False
    if not lines:
        return False

    listed: set[Path] = set()
    for line in lines:
        row = parse_checksum_row(line, path.parent)
        if row is None:
            return False
        expected, target = row
        if target in listed:
            return False
        listed.add(target)
        try:
            if not target.is_file() or sha256_file(target).lower() != expected:
                return False
        except OSError:
            return False

    try:
        actual = {
            candidate.resolve()
            for candidate in path.parent.rglob("*")
            if candidate.is_file() and candidate.resolve() != manifest
        }
    except OSError:
        return False
    return listed == actual


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
