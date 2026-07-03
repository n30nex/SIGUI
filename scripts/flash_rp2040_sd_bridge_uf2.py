#!/usr/bin/env python3
"""Guarded UF2 copier for D1L RP2040 UF2 artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import string
from datetime import datetime, timezone
from pathlib import Path


UF2_NAME = "deskos_sd_bridge.ino.uf2"
SHA256SUMS_NAME = "SHA256SUMS.txt"
UF2_METADATA_FILES = ("INFO_UF2.TXT", "INDEX.HTM", "CURRENT.UF2")


class FlashGuardError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sha256sums(path: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for raw_line in path.read_text(encoding="ascii").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            raise FlashGuardError(f"invalid checksum line in {path}: {raw_line!r}")
        rel = parts[1].strip().replace("\\", "/")
        if rel.startswith("./"):
            rel = rel[2:]
        entries[rel] = parts[0].lower()
    return entries


def verify_artifact(
    artifact_dir: Path,
    expected_sha256: str | None = None,
    *,
    uf2_name: str = UF2_NAME,
) -> dict:
    artifact_dir = artifact_dir.resolve()
    uf2_path = artifact_dir / uf2_name
    sums_path = artifact_dir / SHA256SUMS_NAME
    if not uf2_path.is_file():
        raise FlashGuardError(f"missing UF2 artifact: {uf2_path}")
    if not sums_path.is_file():
        raise FlashGuardError(f"missing checksum manifest: {sums_path}")

    checksums = parse_sha256sums(sums_path)
    expected = checksums.get(uf2_name) or checksums.get(f"./{uf2_name}")
    if not expected:
        raise FlashGuardError(f"{SHA256SUMS_NAME} does not list {uf2_name}")
    actual = sha256_file(uf2_path)
    if actual.lower() != expected.lower():
        raise FlashGuardError(
            f"checksum mismatch for {uf2_name}: expected {expected}, actual {actual}"
        )
    if expected_sha256 and actual.lower() != expected_sha256.lower():
        raise FlashGuardError(
            f"checksum mismatch for --expected-sha256: expected {expected_sha256}, actual {actual}"
        )
    return {
        "path": str(uf2_path),
        "name": uf2_name,
        "size": uf2_path.stat().st_size,
        "sha256": actual.upper(),
        "manifest": str(sums_path),
    }


def normalize_volume_path(value: str | Path) -> Path:
    text = str(value).strip().strip('"')
    if len(text) == 2 and text[1] == ":":
        text += "\\"
    return Path(text).resolve()


def metadata_for_volume(volume: Path) -> dict:
    volume = normalize_volume_path(volume)
    metadata: dict[str, object] = {
        "path": str(volume),
        "exists": volume.exists(),
        "is_dir": volume.is_dir(),
        "metadata_files": [],
        "info_uf2_preview": "",
    }
    if not volume.exists() or not volume.is_dir():
        return metadata

    found: list[str] = []
    for name in UF2_METADATA_FILES:
        path = volume / name
        if path.exists():
            found.append(name)
    metadata["metadata_files"] = found

    info_path = volume / "INFO_UF2.TXT"
    if info_path.exists():
        try:
            metadata["info_uf2_preview"] = info_path.read_text(
                encoding="ascii", errors="ignore"
            )[:240]
        except OSError:
            metadata["info_uf2_preview"] = ""
    return metadata


def volume_is_uf2_bootloader(volume: Path) -> bool:
    metadata = metadata_for_volume(volume)
    if not metadata["exists"] or not metadata["is_dir"]:
        return False
    files = set(metadata["metadata_files"])
    if "CURRENT.UF2" in files:
        return True
    preview = str(metadata.get("info_uf2_preview", "")).upper()
    if "INFO_UF2.TXT" in files and ("UF2" in preview or "RP2040" in preview):
        return True
    return "INDEX.HTM" in files and "INFO_UF2.TXT" in files


def windows_drive_roots() -> list[Path]:
    roots: list[Path] = []
    for letter in string.ascii_uppercase:
        root = Path(f"{letter}:\\")
        if root.exists():
            roots.append(root)
    return roots


def candidate_volumes(extra_roots: list[Path] | None = None) -> list[dict]:
    roots = extra_roots if extra_roots is not None else windows_drive_roots()
    candidates = []
    for root in roots:
        metadata = metadata_for_volume(root)
        if volume_is_uf2_bootloader(root):
            candidates.append(metadata)
    return candidates


def choose_volume(volume: Path | None, extra_roots: list[Path] | None = None) -> tuple[Path, list[dict]]:
    candidates = candidate_volumes(extra_roots)
    if volume:
        target = normalize_volume_path(volume)
        if not volume_is_uf2_bootloader(target):
            raise FlashGuardError(
                f"target is not a UF2 bootloader volume or lacks UF2 metadata: {target}"
            )
        return target, candidates
    if len(candidates) != 1:
        raise FlashGuardError(
            f"expected exactly one UF2 bootloader volume, found {len(candidates)}; pass --volume"
        )
    return normalize_volume_path(candidates[0]["path"]), candidates


def copy_uf2(
    artifact_dir: Path,
    volume: Path | None = None,
    *,
    do_copy: bool = False,
    expected_sha256: str | None = None,
    uf2_name: str = UF2_NAME,
    extra_roots: list[Path] | None = None,
) -> dict:
    artifact = verify_artifact(artifact_dir, expected_sha256, uf2_name=uf2_name)
    target, candidates = choose_volume(volume, extra_roots)
    destination = target / uf2_name
    report = {
        "schema": 1,
        "ok": True,
        "mode": "copy" if do_copy else "dry-run",
        "public_rf_tx": False,
        "formats_sd": False,
        "artifact": artifact,
        "target_volume": metadata_for_volume(target),
        "candidate_volumes": candidates,
        "destination": str(destination),
        "copied": False,
        "note": "RP2040 UF2 copy helper only; use the selected D1L port for post-flash validation after reboot",
    }
    if do_copy:
        shutil.copyfile(artifact["path"], destination)
        report["copied"] = True
    return report


def write_report(report: dict, out: str | None) -> None:
    if not out:
        return
    out_path = Path(out)
    if not out_path.is_absolute():
        out_path = Path.cwd() / out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--artifact-dir",
        required=True,
        help="Directory containing deskos_sd_bridge.ino.uf2 and SHA256SUMS.txt",
    )
    parser.add_argument("--volume", help="RP2040 UF2 bootloader volume root, e.g. E:")
    parser.add_argument("--expected-sha256")
    parser.add_argument("--uf2-name", default=UF2_NAME)
    parser.add_argument("--copy", action="store_true", help="Actually copy the UF2; default is dry-run")
    parser.add_argument("--list-volumes", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    if args.list_volumes:
        report = {
            "schema": 1,
            "ok": True,
            "mode": "list-volumes",
            "public_rf_tx": False,
            "formats_sd": False,
            "candidate_volumes": candidate_volumes(),
            "created_at": datetime.now(timezone.utc).isoformat(),
        }
    else:
        report = copy_uf2(
            Path(args.artifact_dir),
            Path(args.volume) if args.volume else None,
            do_copy=args.copy,
            expected_sha256=args.expected_sha256,
            uf2_name=args.uf2_name,
        )
        report["created_at"] = datetime.now(timezone.utc).isoformat()

    write_report(report, args.out)
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except FlashGuardError as exc:
        print(
            json.dumps(
                {
                    "schema": 1,
                    "ok": False,
                    "error": str(exc),
                    "public_rf_tx": False,
                    "formats_sd": False,
                }
            )
        )
        raise SystemExit(1)
