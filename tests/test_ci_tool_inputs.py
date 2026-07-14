import copy
import hashlib
import json
from pathlib import Path

import pytest

from scripts import verify_ci_tool_inputs as verifier


ROOT = Path(__file__).resolve().parents[1]
METADATA = ROOT / ".github" / "d1l-build-inputs.json"
COMMIT = "a" * 40


def metadata() -> dict:
    return json.loads(METADATA.read_text(encoding="utf-8"))


def test_committed_clang_package_and_executable_bytes_are_exact():
    contract = verifier.clang_contract(metadata())

    assert contract["runner"] == "ubuntu-24.04"
    assert contract["architecture"] == "x86_64"
    assert contract["package_version"] == "1:18.1.3-1ubuntu1"
    assert contract["archive"] == {
        "url": (
            "https://archive.ubuntu.com/ubuntu/pool/universe/l/llvm-toolchain-18/"
            "clang-18_18.1.3-1ubuntu1_amd64.deb"
        ),
        "filename": "clang-18_18.1.3-1ubuntu1_amd64.deb",
        "sha256": "628b16701014ef7ad648380b20ea74b90dd543857f933b3e34d2fc042783de25",
        "size": 80040,
    }
    assert contract["executable"]["sha256"] == (
        "8ef402d453d1ba4902e4ee0f0f847f6cfa01400c95aa43c24e97818b9c0e3f45"
    )
    assert contract["executable"]["size"] == 140592


def test_clang_receipt_verifies_archive_and_both_compiler_commands(tmp_path):
    archive = tmp_path / "clang.deb"
    executable = tmp_path / "clang"
    archive.write_bytes(b"locked-package")
    executable.write_bytes(b"locked-compiler")
    data = metadata()
    clang = data["ci_tools"]["meshcore_conformance"]["clang"]
    clang["archive"].update(
        {
            "url": f"https://example.invalid/{archive.name}",
            "filename": archive.name,
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "size": archive.stat().st_size,
        }
    )
    clang["executable"].update(
        {
            "resolved_path": executable.resolve().as_posix(),
            "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "size": executable.stat().st_size,
        }
    )
    metadata_path = tmp_path / ".github" / "d1l-build-inputs.json"
    metadata_path.parent.mkdir()
    metadata_path.write_text(json.dumps(data), encoding="ascii")

    receipt = verifier.build_clang_receipt(
        metadata_path,
        tmp_path,
        archive_path=archive,
        cc_path=executable,
        cxx_path=executable,
        source_commit=COMMIT,
    )

    assert receipt["ok"] is True
    assert receipt["clang"]["bytes_verified"] is True
    assert receipt["clang"]["archive"]["verified"] is True
    assert receipt["clang"]["executables"]["cc"]["verified"] is True
    verifier.validate_clang_receipt(
        receipt,
        data,
        COMMIT,
        verifier.sha256_file(metadata_path),
    )


def test_clang_byte_verification_and_receipt_validation_fail_closed(tmp_path):
    archive = tmp_path / "clang.deb"
    executable = tmp_path / "clang"
    archive.write_bytes(b"locked-package")
    executable.write_bytes(b"locked-compiler")
    data = metadata()
    clang = data["ci_tools"]["meshcore_conformance"]["clang"]
    clang["archive"].update(
        {
            "url": f"https://example.invalid/{archive.name}",
            "filename": archive.name,
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "size": archive.stat().st_size,
        }
    )
    clang["executable"].update(
        {
            "resolved_path": executable.resolve().as_posix(),
            "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "size": executable.stat().st_size,
        }
    )
    metadata_path = tmp_path / ".github" / "d1l-build-inputs.json"
    metadata_path.parent.mkdir()
    metadata_path.write_text(json.dumps(data), encoding="ascii")
    receipt = verifier.build_clang_receipt(
        metadata_path,
        tmp_path,
        archive_path=archive,
        cc_path=executable,
        cxx_path=executable,
        source_commit=COMMIT,
    )

    tampered = copy.deepcopy(receipt)
    tampered["clang"]["bytes_verified"] = False
    with pytest.raises(ValueError, match="clang_bytes_verified"):
        verifier.validate_clang_receipt(
            tampered,
            data,
            COMMIT,
            verifier.sha256_file(metadata_path),
        )

    archive.write_bytes(b"changed-package")
    with pytest.raises(ValueError, match="archive byte identity mismatch"):
        verifier.build_clang_receipt(
            metadata_path,
            tmp_path,
            archive_path=archive,
            cc_path=executable,
            cxx_path=executable,
            source_commit=COMMIT,
        )
