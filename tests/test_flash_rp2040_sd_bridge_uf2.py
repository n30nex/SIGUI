import hashlib
import json
from pathlib import Path

import pytest

from scripts import flash_rp2040_sd_bridge_uf2 as uf2_flash


def make_artifact(tmp_path: Path, payload: bytes = b"uf2-payload") -> tuple[Path, str]:
    artifact = tmp_path / "artifact"
    artifact.mkdir()
    uf2 = artifact / uf2_flash.UF2_NAME
    uf2.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    (artifact / uf2_flash.SHA256SUMS_NAME).write_text(
        f"{digest}  ./{uf2_flash.UF2_NAME}\n",
        encoding="ascii",
    )
    return artifact, digest


def make_uf2_volume(tmp_path: Path, name: str = "RPI-RP2") -> Path:
    volume = tmp_path / name
    volume.mkdir()
    (volume / "INFO_UF2.TXT").write_text(
        "UF2 Bootloader v1.0\nModel: RP2040\n",
        encoding="ascii",
    )
    (volume / "INDEX.HTM").write_text("UF2", encoding="ascii")
    return volume


def test_verify_artifact_requires_manifest_hash_match(tmp_path):
    artifact, digest = make_artifact(tmp_path)

    report = uf2_flash.verify_artifact(artifact, expected_sha256=digest)

    assert report["sha256"] == digest.upper()
    assert report["size"] == len(b"uf2-payload")


def test_verify_artifact_rejects_bad_expected_hash(tmp_path):
    artifact, _digest = make_artifact(tmp_path)

    with pytest.raises(uf2_flash.FlashGuardError, match="expected-sha256"):
        uf2_flash.verify_artifact(artifact, expected_sha256="0" * 64)


def test_copy_uf2_dry_run_detects_single_uf2_volume_without_copying(tmp_path):
    artifact, digest = make_artifact(tmp_path)
    volume = make_uf2_volume(tmp_path)

    report = uf2_flash.copy_uf2(
        artifact,
        do_copy=False,
        expected_sha256=digest,
        extra_roots=[volume],
    )

    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["copied"] is False
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["target_volume"]["path"] == str(volume.resolve())
    assert not (volume / uf2_flash.UF2_NAME).exists()


def test_copy_uf2_requires_explicit_copy_flag(tmp_path):
    artifact, digest = make_artifact(tmp_path)
    volume = make_uf2_volume(tmp_path)

    report = uf2_flash.copy_uf2(
        artifact,
        volume,
        do_copy=True,
        expected_sha256=digest,
    )

    assert report["copied"] is True
    assert (volume / uf2_flash.UF2_NAME).read_bytes() == b"uf2-payload"


def test_copy_uf2_refuses_ambiguous_or_missing_uf2_volume(tmp_path):
    artifact, _digest = make_artifact(tmp_path)
    first = make_uf2_volume(tmp_path, "FIRST")
    second = make_uf2_volume(tmp_path, "SECOND")

    with pytest.raises(uf2_flash.FlashGuardError, match="expected exactly one"):
        uf2_flash.copy_uf2(artifact, extra_roots=[first, second])

    with pytest.raises(uf2_flash.FlashGuardError, match="target is not a UF2"):
        uf2_flash.copy_uf2(artifact, tmp_path / "not-uf2")


def test_write_report_uses_ascii_json(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    out = Path("artifacts") / "rp2040-uf2-copy.json"

    uf2_flash.write_report({"schema": 1, "ok": True}, str(out))

    assert json.loads(out.read_text(encoding="ascii")) == {"schema": 1, "ok": True}
