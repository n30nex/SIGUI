#!/usr/bin/env python3
"""Verify .sha256 files under an artifact directory."""

from __future__ import annotations

import argparse
import hashlib
import re
import stat
from pathlib import Path


def is_link_or_reparse(path: Path) -> bool:
    """Return true for symlinks and Windows reparse-point aliases."""
    try:
        if path.is_symlink():
            return True
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
    except OSError:
        return True
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return bool(attributes & reparse_flag)


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
    parts = filename.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        return None
    relative = Path(*parts)
    if relative.is_absolute() or relative.drive:
        return None
    try:
        parent_resolved = parent.resolve(strict=True)
        target = parent
        expected = parent_resolved
        for part in relative.parts:
            target /= part
            expected /= part
            if is_link_or_reparse(target):
                return None
            if target.exists() and target.resolve(strict=True) != expected:
                return None
        target.resolve(strict=False).relative_to(parent_resolved)
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
        if is_link_or_reparse(path) or not path.is_file():
            return False
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
        if is_link_or_reparse(path) or not path.is_file():
            return False
        manifest = path
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
        try:
            relative = target.relative_to(path.parent)
        except ValueError:
            return False
        if relative in listed:
            return False
        listed.add(relative)
        try:
            if not target.is_file() or sha256_file(target).lower() != expected:
                return False
        except OSError:
            return False

    try:
        actual: set[Path] = set()
        for candidate in path.parent.rglob("*"):
            relative = candidate.relative_to(path.parent)
            if is_link_or_reparse(candidate) or safe_checksum_target(
                path.parent, relative.as_posix()
            ) is None:
                return False
            if candidate == manifest:
                continue
            if candidate.is_file():
                actual.add(relative)
            elif not candidate.is_dir():
                return False
    except (OSError, RuntimeError, ValueError):
        return False
    return listed == actual


def verify_checksum_tree(root: Path) -> bool:
    try:
        if is_link_or_reparse(root) or not root.is_dir():
            return False
        root_manifest = root / "SHA256SUMS.txt"
        if is_link_or_reparse(root_manifest) or not root_manifest.is_file():
            return False
        sha_files = list(root.rglob("*.sha256"))
        manifests = list(root.rglob("SHA256SUMS.txt"))
    except (OSError, RuntimeError):
        return False
    return root_manifest in manifests and all(
        verify_sha256_file(path) for path in sha_files
    ) and all(
        verify_sha256_manifest(path) for path in manifests
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact_dir", nargs="?", default="artifacts")
    args = parser.parse_args()

    root = Path(args.artifact_dir)
    sha_files = list(root.rglob("*.sha256"))
    manifests = list(root.rglob("SHA256SUMS.txt"))
    failed = [path for path in sha_files if not verify_sha256_file(path)]
    failed.extend(path for path in manifests if not verify_sha256_manifest(path))
    for path in sha_files:
        print(f"{'FAIL' if path in failed else 'OK'} {path}")
    for path in manifests:
        print(f"{'FAIL' if path in failed else 'OK'} {path}")
    if not verify_checksum_tree(root):
        print(f"FAIL {root / 'SHA256SUMS.txt'} (required root coverage)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
