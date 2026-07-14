import copy
import hashlib
import json
from pathlib import Path

import pytest

from scripts import verify_arduino_build_inputs as verifier


ROOT = Path(__file__).resolve().parents[1]
METADATA = ROOT / ".github" / "d1l-build-inputs.json"


def metadata() -> dict:
    return json.loads(METADATA.read_text(encoding="utf-8"))


def test_committed_arduino_inputs_are_exact_and_match_source_gitlinks():
    data = metadata()
    validated = verifier.validate_metadata(data, ROOT)
    rp2040 = data["arduino"]["rp2040"]
    tools = rp2040["tools"]

    assert validated["cli_version"] == "1.5.0"
    assert validated["cli_archive"]["sha256"] == (
        "f4e49fb6f5d6a043f7df792ef057143c542140508081aaa52d214a9ab33141c8"
    )
    assert validated["cli_executable"]["sha256"] == (
        "33eb851315471d5d4573454e03c29b66ee2a9f2a94ca30b08ce72e13e15d2118"
    )
    assert validated["core_version"] == "5.6.1"
    assert rp2040["production_fqbn"] == "rp2040:rp2040:seeed_indicator_rp2040:usbstack=nousb"
    assert rp2040["smoke_fqbn"] == "rp2040:rp2040:seeed_indicator_rp2040"
    assert rp2040["tool_host"] == "x86_64-pc-linux-gnu"
    assert rp2040["compiler_tool"] == "pqt-gcc"
    assert len(tools) == 7
    assert len({tool["name"] for tool in tools}) == len(tools)
    assert len(validated["archive_inventory"]) == 8
    assert validated["submodules"] == data["submodules"]


def test_metadata_only_receipt_is_exact_commit_bound_and_truthful():
    receipt = verifier.build_receipt(
        METADATA,
        ROOT,
        arduino_data_dir=None,
        arduino_cli_version="1.5.0",
    )
    assert receipt["ok"] is True
    assert len(receipt["source_commit"]) == 40
    assert receipt["metadata"]["path"] == ".github/d1l-build-inputs.json"
    assert receipt["metadata"]["sha256"] == verifier.sha256_file(METADATA)
    assert receipt["arduino_cli_version"] == "1.5.0"
    assert receipt["arduino_cli_bytes_verified"] is False
    assert receipt["arduino_cli"]["executable"] is None
    assert receipt["rp2040_core_version"] == "5.6.1"
    assert receipt["archives_verified"] is False
    assert receipt["archives"] == []


def test_archive_verification_requires_exact_inventory_bytes(tmp_path: Path):
    payloads = {"core.zip": b"core", "tool.tar.gz": b"toolchain"}
    inventory = []
    for index, (filename, payload) in enumerate(payloads.items()):
        directory = tmp_path / ("packages" if index == 0 else "tools")
        directory.mkdir(parents=True)
        (directory / filename).write_bytes(payload)
        inventory.append(
            {
                "filename": filename,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }
        )

    verified = verifier.verify_archives(tmp_path, inventory)
    assert [item["filename"] for item in verified] == ["core.zip", "tool.tar.gz"]
    assert all(item["relative_path"] for item in verified)

    (tmp_path / "tools" / "tool.tar.gz").write_bytes(b"tampered")
    with pytest.raises(ValueError, match="archive mismatch"):
        verifier.verify_archives(tmp_path, inventory)


def test_archive_verification_rejects_missing_or_ambiguous_download(tmp_path: Path):
    payload = b"archive"
    inventory = [
        {
            "filename": "core.zip",
            "sha256": hashlib.sha256(payload).hexdigest(),
            "size": len(payload),
        }
    ]
    with pytest.raises(ValueError, match="found 0"):
        verifier.verify_archives(tmp_path, inventory)

    for directory in (tmp_path / "one", tmp_path / "two"):
        directory.mkdir()
        (directory / "core.zip").write_bytes(payload)
    with pytest.raises(ValueError, match="found 2"):
        verifier.verify_archives(tmp_path, inventory)


def test_arduino_cli_executable_requires_exact_locked_bytes(tmp_path, monkeypatch):
    executable = tmp_path / "arduino-cli"
    executable.write_bytes(b"locked-cli")
    validated = verifier.validate_metadata(metadata(), ROOT)
    validated["cli_executable"] = {
        "filename": "arduino-cli",
        "sha256": hashlib.sha256(b"locked-cli").hexdigest(),
        "size": len(b"locked-cli"),
    }
    monkeypatch.setattr(verifier, "validate_metadata", lambda _data, _root: validated)
    monkeypatch.setattr(verifier, "_git", lambda _root, *_args: "a" * 40)

    receipt = verifier.build_receipt(
        METADATA,
        ROOT,
        arduino_data_dir=None,
        arduino_cli_version="1.5.0",
        arduino_cli_path=executable,
    )
    assert receipt["arduino_cli_bytes_verified"] is True
    assert receipt["arduino_cli"]["executable"]["verified"] is True

    executable.write_bytes(b"tampered-cli")
    with pytest.raises(ValueError, match="executable byte identity mismatch"):
        verifier.build_receipt(
            METADATA,
            ROOT,
            arduino_data_dir=None,
            arduino_cli_version="1.5.0",
            arduino_cli_path=executable,
        )

def test_metadata_rejects_moving_versions_duplicate_tools_and_stale_gitlinks():
    data = metadata()

    moving = copy.deepcopy(data)
    moving["arduino"]["cli"]["version"] = "1.x"
    with pytest.raises(ValueError, match="must be exact"):
        verifier.validate_metadata(moving, ROOT)

    duplicate = copy.deepcopy(data)
    duplicate["arduino"]["rp2040"]["tools"].append(
        copy.deepcopy(duplicate["arduino"]["rp2040"]["tools"][0])
    )
    with pytest.raises(ValueError, match="duplicate Arduino archive filename"):
        verifier.validate_metadata(duplicate, ROOT)

    bad_url = copy.deepcopy(data)
    bad_url["arduino"]["rp2040"]["platform_archive"]["url"] = "http://example.invalid/core.zip"
    with pytest.raises(ValueError, match="archive URL"):
        verifier.validate_metadata(bad_url, ROOT)

    stale = copy.deepcopy(data)
    stale["submodules"]["third_party/MeshCore"] = "0" * 40
    with pytest.raises(ValueError, match="submodule gitlink mismatch"):
        verifier.validate_metadata(stale, ROOT)


def test_workflow_downloads_and_verifies_archives_before_installing_core():
    workflow = (ROOT / ".github" / "workflows" / "d1l-ci.yml").read_text(encoding="utf-8")
    job = workflow.split("  rp2040-sd-bridge-build:", 1)[1].split("  firmware-build:", 1)[0]
    action = "arduino/setup-arduino-cli@81d310742121c928ea9c8bbd407b4217b432ae02"
    download = "arduino-cli core download rp2040:rp2040@5.6.1"
    verify = "python scripts/verify_arduino_build_inputs.py"
    install = "arduino-cli core install rp2040:rp2040@5.6.1"

    assert action in job
    assert 'version: "1.5.0"' in job
    assert job.index(download) < job.index(verify) < job.index(install)
    assert '--arduino-data-dir "$HOME/.arduino15"' in job
    assert '--arduino-cli-path "$(command -v arduino-cli)"' in job
    assert "artifacts/rp2040-sd-bridge/build-inputs.json" in job
    assert "version: 1.x" not in job
