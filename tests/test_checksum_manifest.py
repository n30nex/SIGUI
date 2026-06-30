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
