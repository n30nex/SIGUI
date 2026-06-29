import hashlib
from pathlib import Path

from scripts.verify_checksums import verify_sha256_file


def test_verify_checksum_file(tmp_path: Path):
    artifact = tmp_path / "firmware.bin"
    artifact.write_bytes(b"d1l")
    digest = hashlib.sha256(b"d1l").hexdigest()
    checksum = tmp_path / "firmware.bin.sha256"
    checksum.write_text(f"{digest}  firmware.bin\n", encoding="ascii")
    assert verify_sha256_file(checksum)
