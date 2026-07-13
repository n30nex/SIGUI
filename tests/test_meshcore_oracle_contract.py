import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from scripts import meshcore_conformance_d1l as conformance


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "meshcore_conformance_d1l.py"
ORACLE_ROOT = ROOT / "tests" / "meshcore_oracle"
MANIFEST = ORACLE_ROOT / "manifest.json"
UPSTREAM_COMMIT = "e8d3c53ba1ea863937081cd0caad759b832f3028"
BOUNDARY = "upstream_packet_envelope_adapter_only"


def canonical_lf_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def git_head() -> str:
    return subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def test_oracle_manifest_is_exactly_pinned_and_fail_closed():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    assert manifest["schema_version"] == 1
    assert manifest["corpus_version"] == 1
    assert manifest["abi_version"] == 1
    assert manifest["coverage_boundary"] == BOUNDARY
    assert manifest["wp04_closure_eligible"] is False
    assert manifest["upstream"]["commit"] == UPSTREAM_COMMIT
    assert manifest["interface"] == {
        "language": "c_abi",
        "header": "tests/meshcore_oracle/meshcore_oracle.h",
        "implementation": "tests/meshcore_oracle/meshcore_oracle.cpp",
        "upstream_type": "mesh::Packet",
        "reject_preserves_output": True,
        "crypto_available": False,
    }
    assert manifest["vectors"] == {
        "roundtrip": 4,
        "invalid": 5,
        "semantic": 0,
        "total": 9,
    }
    assert manifest["capabilities"][0] == {
        "id": "packet_envelope",
        "status": "implemented",
        "owner": "pinned_upstream",
        "semantic": False,
    }
    assert all(
        capability["status"] == "pending"
        for capability in manifest["capabilities"][1:]
    )
    assert [
        capability["id"] for capability in manifest["capabilities"][1:]
    ] == [
        "identity_signed_advert",
        "public_group_packets",
        "dm_encrypt_decrypt",
        "ack_multiack_ack_path",
        "direct_flood_routing",
        "path_return_route_codes",
        "trace_path_discovery",
        "login_request_response_admin",
    ]

    for relative, expected in {
        **manifest["upstream"]["sources"],
        **manifest["oracle_sources"],
    }.items():
        assert canonical_lf_sha256(ROOT / relative) == expected


def test_oracle_c_abi_wraps_only_pinned_upstream_packet():
    header = (ORACLE_ROOT / "meshcore_oracle.h").read_text(encoding="utf-8")
    adapter = (ORACLE_ROOT / "meshcore_oracle.cpp").read_text(encoding="utf-8")

    assert '"e8d3c53ba1ea863937081cd0caad759b832f3028"' in header
    assert 'extern "C"' in header
    assert "mesh::Packet upstream" in adapter
    assert "upstream.readFrom" in adapter
    assert "upstream.writeTo" in adapter
    assert "structurally_safe_to_read" in adapter
    assert "structurally_safe_to_write" in adapter
    assert "main/mesh" not in adapter
    assert "Identity" not in adapter
    assert "Dispatcher" not in adapter


def test_conformance_plan_builds_the_oracle_as_a_separate_target():
    commands = conformance.command_plan("clang", "clang++")
    flattened = [" ".join(command) for command in commands]

    assert any("meshcore_oracle.cpp" in command for command in flattened)
    assert any("meshcore_oracle_vectors.cpp" in command for command in flattened)
    assert any(command.endswith("meshcore_oracle_vectors") for command in flattened)
    assert all("main/mesh/meshcore_service" not in command for command in flattened)


def test_dry_run_writes_a_versioned_fail_closed_oracle_artifact(tmp_path):
    output = tmp_path / "meshcore_conformance.json"
    oracle_output = tmp_path / f"meshcore_oracle_manifest_{git_head()}.json"
    env = os.environ.copy()
    env.pop("D1L_MESHCORE_CONFORMANCE_CI", None)
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--commit",
            git_head(),
            "--out",
            str(output),
            "--oracle-out",
            str(oracle_output),
            "--dry-run",
        ],
        cwd=ROOT,
        env=env,
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr
    report = json.loads(output.read_text(encoding="utf-8"))
    artifact = json.loads(oracle_output.read_text(encoding="utf-8"))
    assert report["oracle"]["artifact_path"] == str(oracle_output)
    assert report["oracle"]["source_verification"]["verified"] is True
    assert report["oracle"]["result"] is None
    assert artifact["artifact_type"] == "meshcore_oracle_manifest"
    assert artifact["status"] == "dry_run"
    assert artifact["passed"] is False
    assert artifact["execution_complete"] is False
    assert artifact["coverage_boundary"] == BOUNDARY
    assert artifact["wp04_closure_eligible"] is False
    assert artifact["closure_ready"] is False
    assert artifact["repository_commit"] == git_head()
    assert artifact["upstream_commit"] == UPSTREAM_COMMIT
    assert artifact["oracle_result"] is None
    assert len(artifact["pending_capabilities"]) == 8
    assert "packet_envelope" not in artifact["pending_capabilities"]


def test_oracle_vectors_compile_and_run_deterministically(tmp_path):
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        pytest.skip("no host C++ compiler available")
    executable = tmp_path / (
        "meshcore_oracle_vectors.exe" if os.name == "nt" else "meshcore_oracle_vectors"
    )
    command = [
        compiler,
        "-std=c++17",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ORACLE_ROOT),
        "-I",
        str(ROOT / "tests" / "meshcore_conformance" / "stubs"),
        "-isystem",
        str(ROOT / "third_party" / "MeshCore" / "src"),
        str(ROOT / "third_party" / "MeshCore" / "src" / "Packet.cpp"),
        str(ORACLE_ROOT / "meshcore_oracle.cpp"),
        str(ORACLE_ROOT / "meshcore_oracle_vectors.cpp"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)

    first = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    second = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert first.stdout == second.stdout
    assert first.stderr == second.stderr == ""
    assert json.loads(first.stdout) == {
        "passed": True,
        "coverage_boundary": BOUNDARY,
        "wp04_closure_eligible": False,
        "abi_version": 1,
        "upstream_commit": UPSTREAM_COMMIT,
        "vectors": {"roundtrip": 4, "invalid": 5, "semantic": 0, "total": 9},
        "failures": 0,
    }
