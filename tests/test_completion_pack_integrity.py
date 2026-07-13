import hashlib
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACK_DIR = ROOT / "docs" / "completion"
MANIFEST = PACK_DIR / "SHA256SUMS.txt"
ROW_RE = re.compile(r"^([0-9a-f]{64})  ([^/\\]+)$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def test_completion_pack_manifest_is_complete_and_current() -> None:
    rows = MANIFEST.read_text(encoding="ascii").splitlines()
    parsed: dict[str, str] = {}
    for row in rows:
        match = ROW_RE.fullmatch(row)
        assert match is not None, f"malformed completion-pack checksum row: {row!r}"
        expected, name = match.groups()
        assert name not in parsed, f"duplicate completion-pack checksum row: {name}"
        parsed[name] = expected

    expected_files = {
        path.name for path in PACK_DIR.iterdir() if path.is_file() and path != MANIFEST
    }
    assert set(parsed) == expected_files
    assert {
        name: sha256(PACK_DIR / name) for name in sorted(parsed)
    } == parsed
