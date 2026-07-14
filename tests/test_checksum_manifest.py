import hashlib
import os
import sys
from pathlib import Path

import pytest

from scripts.verify_checksums import (
    main as verify_checksums_main,
    verify_checksum_tree,
    verify_sha256_file,
    verify_sha256_manifest,
)


def symlink_or_skip(target: Path, link: Path, *, target_is_directory: bool = False) -> None:
    try:
        os.symlink(target, link, target_is_directory=target_is_directory)
    except (NotImplementedError, OSError) as exc:
        pytest.skip(f"symlinks unavailable: {exc}")


def test_verify_checksum_file(tmp_path: Path):
    artifact = tmp_path / "firmware.bin"
    artifact.write_bytes(b"d1l")
    digest = hashlib.sha256(b"d1l").hexdigest()
    checksum = tmp_path / "firmware.bin.sha256"
    checksum.write_text(f"{digest}  firmware.bin\n", encoding="ascii")
    assert verify_sha256_file(checksum)


def test_verify_sha256sums_manifest(tmp_path: Path):
    firmware = tmp_path / "firmware"
    firmware.mkdir()
    app = firmware / "meshcore_deskos_d1l.bin"
    app.write_bytes(b"app")
    full = tmp_path / "full-flash.bin"
    full.write_bytes(b"full")
    manifest = tmp_path / "SHA256SUMS.txt"
    manifest.write_text(
        "\n".join(
            [
                f"{hashlib.sha256(b'app').hexdigest()}  ./firmware/meshcore_deskos_d1l.bin",
                f"{hashlib.sha256(b'full').hexdigest()}  ./full-flash.bin",
            ]
        )
        + "\n",
        encoding="ascii",
    )

    assert verify_sha256_manifest(manifest)


def test_verify_sha256_manifest_rejects_missing_file(tmp_path: Path):
    manifest = tmp_path / "SHA256SUMS.txt"
    manifest.write_text(f"{hashlib.sha256(b'missing').hexdigest()}  ./missing.bin\n", encoding="ascii")

    assert not verify_sha256_manifest(manifest)


def test_verify_sha256_manifest_rejects_bad_digest(tmp_path: Path):
    artifact = tmp_path / "firmware.bin"
    artifact.write_bytes(b"actual")
    manifest = tmp_path / "SHA256SUMS.txt"
    manifest.write_text(f"{hashlib.sha256(b'other').hexdigest()}  ./firmware.bin\n", encoding="ascii")

    assert not verify_sha256_manifest(manifest)


def test_verify_sha256_manifest_rejects_empty_incomplete_and_duplicate_rows(tmp_path: Path):
    manifest = tmp_path / "SHA256SUMS.txt"
    manifest.write_text("", encoding="ascii")
    assert not verify_sha256_manifest(manifest)

    artifact = tmp_path / "firmware.bin"
    extra = tmp_path / "unlisted.map"
    artifact.write_bytes(b"firmware")
    extra.write_bytes(b"extra")
    row = f"{hashlib.sha256(b'firmware').hexdigest()}  ./firmware.bin\n"
    manifest.write_text(row, encoding="ascii")
    assert not verify_sha256_manifest(manifest)

    extra.unlink()
    manifest.write_text(row + row, encoding="ascii")
    assert not verify_sha256_manifest(manifest)


def test_verify_sha256_manifest_rejects_path_escape_and_malformed_digest(tmp_path: Path):
    artifact = tmp_path / "firmware.bin"
    artifact.write_bytes(b"firmware")
    outside = tmp_path.parent / "outside-release.bin"
    outside.write_bytes(b"outside")
    manifest = tmp_path / "SHA256SUMS.txt"
    manifest.write_text(
        f"{hashlib.sha256(b'outside').hexdigest()}  ../outside-release.bin\n",
        encoding="ascii",
    )
    assert not verify_sha256_manifest(manifest)

    manifest.write_text("not-a-sha256  ./firmware.bin\n", encoding="ascii")
    assert not verify_sha256_manifest(manifest)


def test_verify_sha256_manifest_requires_nested_manifests_in_root_coverage(tmp_path: Path):
    nested = tmp_path / "nested"
    nested.mkdir()
    artifact = nested / "firmware.bin"
    artifact.write_bytes(b"firmware")
    nested_manifest = nested / "SHA256SUMS.txt"
    nested_manifest.write_text(
        f"{hashlib.sha256(b'firmware').hexdigest()}  ./firmware.bin\n",
        encoding="ascii",
    )
    root_manifest = tmp_path / "SHA256SUMS.txt"
    root_manifest.write_text(
        f"{hashlib.sha256(b'firmware').hexdigest()}  ./nested/firmware.bin\n",
        encoding="ascii",
    )
    assert not verify_sha256_manifest(root_manifest)

    root_manifest.write_text(
        "\n".join(
            [
                f"{hashlib.sha256(b'firmware').hexdigest()}  ./nested/firmware.bin",
                f"{hashlib.sha256(nested_manifest.read_bytes()).hexdigest()}  ./nested/SHA256SUMS.txt",
            ]
        )
        + "\n",
        encoding="ascii",
    )
    assert verify_sha256_manifest(root_manifest)
    assert verify_sha256_manifest(nested_manifest)
    assert verify_checksum_tree(tmp_path)


def test_verify_checksum_tree_requires_valid_root_manifest(
    tmp_path: Path, monkeypatch
):
    empty = tmp_path / "empty"
    empty.mkdir()
    assert not verify_checksum_tree(empty)
    assert not verify_checksum_tree(tmp_path / "missing")

    nested_only = tmp_path / "nested-only"
    nested = nested_only / "nested"
    nested.mkdir(parents=True)
    artifact = nested / "firmware.bin"
    artifact.write_bytes(b"firmware")
    (nested / "SHA256SUMS.txt").write_text(
        f"{hashlib.sha256(b'firmware').hexdigest()}  ./firmware.bin\n",
        encoding="ascii",
    )
    (nested_only / "uncovered.bin").write_bytes(b"uncovered")
    assert not verify_checksum_tree(nested_only)
    monkeypatch.setattr(sys, "argv", ["verify_checksums.py", str(nested_only)])
    assert verify_checksums_main() == 1


def test_verify_sha256_manifest_rejects_internal_symlink_alias_and_control(tmp_path: Path):
    alias_root = tmp_path / "alias"
    alias_root.mkdir()
    real = alias_root / "real.bin"
    real.write_bytes(b"payload")
    alias = alias_root / "alias.bin"
    symlink_or_skip(Path("real.bin"), alias)
    manifest = alias_root / "SHA256SUMS.txt"
    manifest.write_text(
        f"{hashlib.sha256(b'payload').hexdigest()}  ./alias.bin\n",
        encoding="ascii",
    )
    assert not verify_sha256_manifest(manifest)

    control_root = tmp_path / "control"
    control_root.mkdir()
    artifact = control_root / "firmware.bin"
    artifact.write_bytes(b"firmware")
    real_control = tmp_path / "real-SHA256SUMS.txt"
    real_control.write_text(
        f"{hashlib.sha256(b'firmware').hexdigest()}  ./firmware.bin\n",
        encoding="ascii",
    )
    symlink_or_skip(real_control, control_root / "SHA256SUMS.txt")
    assert not verify_sha256_manifest(control_root / "SHA256SUMS.txt")
    assert not verify_checksum_tree(control_root)


def test_verify_sha256_manifest_rejects_external_symlink_components(tmp_path: Path):
    outside = tmp_path / "outside"
    outside.mkdir()
    (outside / "firmware.bin").write_bytes(b"outside")

    file_root = tmp_path / "file-link"
    file_root.mkdir()
    symlink_or_skip(outside / "firmware.bin", file_root / "firmware.bin")
    file_manifest = file_root / "SHA256SUMS.txt"
    file_manifest.write_text(
        f"{hashlib.sha256(b'outside').hexdigest()}  ./firmware.bin\n",
        encoding="ascii",
    )
    assert not verify_sha256_manifest(file_manifest)

    directory_root = tmp_path / "directory-link"
    directory_root.mkdir()
    symlink_or_skip(outside, directory_root / "linked", target_is_directory=True)
    directory_manifest = directory_root / "SHA256SUMS.txt"
    directory_manifest.write_text(
        f"{hashlib.sha256(b'outside').hexdigest()}  ./linked/firmware.bin\n",
        encoding="ascii",
    )
    assert not verify_sha256_manifest(directory_manifest)


def test_checksum_verifier_stays_python38_compatible():
    source = Path("scripts/verify_checksums.py").read_text(encoding="utf-8")

    assert ".removeprefix(" not in source
    assert ".removesuffix(" not in source
