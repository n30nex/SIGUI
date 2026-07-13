import hashlib
from pathlib import Path

from scripts.verify_checksums import verify_sha256_file, verify_sha256_manifest


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


def test_checksum_verifier_stays_python38_compatible():
    source = Path("scripts/verify_checksums.py").read_text(encoding="utf-8")

    assert ".removeprefix(" not in source
    assert ".removesuffix(" not in source
