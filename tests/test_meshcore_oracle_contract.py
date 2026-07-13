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
COVERAGE_MANIFEST = ORACLE_ROOT / "coverage_manifest.json"
UPSTREAM_COMMIT = "e8d3c53ba1ea863937081cd0caad759b832f3028"
BOUNDARY = (
    "pinned_upstream_packet_advert_route_ack_trace_and_signed_advert_verification"
)


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
    assert manifest["corpus_version"] == 7
    assert manifest["abi_version"] == 2
    assert manifest["coverage_boundary"] == BOUNDARY
    assert manifest["wp04_closure_eligible"] is False
    assert manifest["upstream"]["commit"] == UPSTREAM_COMMIT
    assert manifest["interface"] == {
        "language": "c_abi",
        "header": "tests/meshcore_oracle/meshcore_oracle.h",
        "implementation": "tests/meshcore_oracle/meshcore_oracle.cpp",
        "upstream_types": [
            "mesh::Packet",
            "AdvertDataBuilder",
            "AdvertDataParser",
        ],
        "reject_preserves_output": True,
        "crypto_available": False,
        "signed_advert_ed25519_available": True,
        "signed_advert_scope": (
            "d1l_production_message_layout_and_ed25519_verification_only_no_mesh_dispatch"
        ),
        "canonical_advert_data": True,
        "route_header_scope": "non_trace_direct_flood_and_zero_hop_headers",
        "ack_frame_scope": "simple_and_multipart_payload_framing_only",
        "trace_frame_scope": (
            "initial_outbound_direct_flags_zero_trace_framing_only"
        ),
        "golden_vector_sources": {
            "direct_flood_headers": "third_party/MeshCore/src/Mesh.cpp",
            "ack_frames": "third_party/MeshCore/src/Mesh.cpp",
            "trace_source_frames": "third_party/MeshCore/src/Mesh.cpp",
            "signed_advert_message_layout": "third_party/MeshCore/src/Mesh.cpp",
            "signed_advert_fixed_seed_keypair": (
                "third_party/MeshCore/lib/ed25519/keypair.c"
            ),
            "signed_advert_fixed_seed_signer": (
                "third_party/MeshCore/lib/ed25519/sign.c"
            ),
            "signed_advert_verifier": (
                "third_party/MeshCore/lib/ed25519/verify.c"
            ),
            "signed_advert_independent_kat": "RFC 8032 section 7.1 TEST 1",
        },
    }
    assert manifest["determinism"]["signed_advert_seed_hex"] == (
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f"
    )
    assert manifest["determinism"]["signed_advert_generation_recipe"] == (
        "ed25519_create_keypair_seed_then_ed25519_sign_"
        "public_key_timestamp_wire_bytes_app_data"
    )
    assert manifest["determinism"]["independent_verifier_kat"] == (
        "RFC 8032 section 7.1 TEST 1 empty message"
    )
    assert manifest["determinism"]["canonical_s_regression"] == (
        "RFC 8032 TEST 1 signature scalar plus Ed25519 group order L"
    )
    assert manifest["vectors"] == {
        "roundtrip": 26,
        "valid": 4,
        "invalid": 75,
        "semantic": 92,
        "total": 105,
        "packet_envelope": {
            "roundtrip": 4,
            "invalid": 5,
            "semantic": 0,
            "total": 9,
        },
        "advert_data_fields": {
            "roundtrip": 4,
            "invalid": 11,
            "semantic": 15,
            "total": 15,
        },
        "signed_advert_verification": {
            "valid": 3,
            "invalid": 10,
            "semantic": 13,
            "total": 13,
        },
        "ed25519_verifier_kat": {
            "valid": 1,
            "invalid": 3,
            "semantic": 0,
            "total": 4,
        },
        "direct_flood_headers": {
            "roundtrip": 7,
            "invalid": 10,
            "semantic": 17,
            "total": 17,
        },
        "ack_frames": {
            "roundtrip": 5,
            "invalid": 17,
            "semantic": 22,
            "total": 22,
        },
        "trace_source_frames": {
            "roundtrip": 6,
            "invalid": 19,
            "semantic": 25,
            "total": 25,
        },
    }
    assert manifest["capabilities"][0] == {
        "id": "packet_envelope",
        "status": "implemented",
        "owner": "pinned_upstream",
        "semantic": False,
    }
    assert manifest["capabilities"][1] == {
        "id": "advert_data_fields",
        "status": "implemented",
        "owner": "pinned_upstream",
        "semantic": True,
    }
    assert manifest["capabilities"][2] == {
        "id": "signed_advert_verification",
        "status": "implemented",
        "owner": "pinned_d1l_production_ed25519",
        "semantic": True,
        "scope": "message_layout_and_signature_verification_only_no_mesh_dispatch",
    }
    assert manifest["capabilities"][3] == {
        "id": "direct_flood_headers",
        "status": "implemented",
        "owner": "pinned_source_golden_vectors",
        "semantic": True,
        "scope": "non_trace_direct_flood_and_zero_hop_headers",
    }
    assert manifest["capabilities"][4] == {
        "id": "ack_frames",
        "status": "implemented",
        "owner": "pinned_source_golden_vectors",
        "semantic": True,
        "scope": "simple_and_multipart_payload_framing_only",
    }
    assert manifest["capabilities"][5] == {
        "id": "trace_source_frames",
        "status": "implemented",
        "owner": "pinned_source_golden_vectors",
        "semantic": True,
        "scope": "initial_outbound_direct_flags_zero_trace_framing_only",
    }
    assert manifest["capabilities"][6] == {
        "id": "identity_signed_advert",
        "status": "pending",
        "owner": "unassigned",
        "blocked_by": "external_unpinned_ed25519_verifier_and_mesh_dispatch_fixture",
        "blocked_scope": (
            "upstream_identity_verifier_parity_and_mesh_dispatch_replay_contact_semantics"
        ),
    }
    assert all(
        capability["status"] == "pending"
        for capability in manifest["capabilities"][6:]
    )
    assert [
        capability["id"] for capability in manifest["capabilities"][6:]
    ] == [
        "identity_signed_advert",
        "public_group_packets",
        "dm_encrypt_decrypt",
        "expected_ack_hash_and_ack_path",
        "ack_dispatch_correlation_and_delivery",
        "route_selection_and_forwarding",
        "path_return_route_codes",
        "trace_forwarding_and_path_discovery",
        "login_request_response_admin",
    ]
    pending = {
        capability["id"]: capability
        for capability in manifest["capabilities"]
        if capability["status"] == "pending"
    }
    assert pending["expected_ack_hash_and_ack_path"]["blocked_by"] == (
        "external_unpinned_sha_aes_and_mesh_session_fixtures"
    )
    assert pending["dm_encrypt_decrypt"]["blocked_by"] == (
        "external_unpinned_aes_sha_ed25519_identity_and_mesh_session_fixtures"
    )
    assert pending["ack_dispatch_correlation_and_delivery"]["blocked_by"] == (
        "deterministic_mesh_dispatch_packet_manager_tables_and_clock_fixtures"
    )
    assert pending["path_return_route_codes"]["blocked_by"] == (
        "external_unpinned_aes_sha_rng_identity_and_mesh_session_fixtures"
    )
    assert pending["trace_forwarding_and_path_discovery"]["blocked_by"] == (
        "deterministic_identity_mesh_tables_radio_snr_and_clock_fixtures"
    )
    assert pending["route_selection_and_forwarding"]["blocked_by"] == (
        "deterministic_mesh_dispatch_packet_manager_tables_radio_rng_and_clock_fixtures"
    )
    assert pending["login_request_response_admin"]["blocked_by"] == (
        "external_unpinned_aes_sha_ed25519_identity_and_deterministic_admin_session_fixtures"
    )

    for relative, expected in {
        **manifest["upstream"]["sources"],
        **manifest["production_binding_sources"],
        **manifest["oracle_sources"],
    }.items():
        assert canonical_lf_sha256(ROOT / relative) == expected


def test_oracle_coverage_manifest_accounts_for_every_required_surface():
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    coverage = conformance.load_oracle_coverage_manifest(
        oracle_manifest, wire_manifest
    )
    summary = conformance.summarize_oracle_coverage(coverage, oracle_manifest)

    assert coverage == json.loads(COVERAGE_MANIFEST.read_text(encoding="utf-8"))
    assert summary["validated"] is True
    assert summary["wp04_closure_eligible"] is False
    assert summary["closure_ready"] is False
    assert summary["unsupported_closure_rejected"] is True
    assert summary["required_surface_count"] == 9
    assert summary["implemented_surface_count"] == 1
    assert summary["partial_surface_count"] == 3
    assert summary["blocked_surface_count"] == 5
    assert summary["local_packet_type_count"] == 6
    assert summary["wire_vector_covered_packet_type_count"] == 6
    assert summary["unknown_packet_type_policy"] == "fail_closed"
    assert len(summary["blocker_receipts"]) == 9
    assert len(summary["unresolved_capabilities"]) == 9
    assert len(summary["local_packet_types"]) == 6


@pytest.mark.parametrize(
    ("declaration", "error"),
    [
        (
            "#define D1L_MESHCORE_PAYLOAD_UNREVIEWED 0x0BU",
            "local packet type changed without a coverage vector",
        ),
        (
            "#define D1L_MESHCORE_PAYLOAD_UNREVIEWED 0x0BU // unreviewed",
            "local MeshCore packet type value syntax is unsupported",
        ),
        (
            "#define D1L_MESHCORE_PAYLOAD_UNREVIEWED (0x0BU)",
            "local MeshCore packet type value syntax is unsupported",
        ),
        (
            "#define D1L_MESHCORE_PAYLOAD_VER_UNREVIEWED 0x0BU",
            "local packet type changed without a coverage vector",
        ),
    ],
)
def test_local_packet_type_addition_without_vector_fails_closed(
    tmp_path, declaration, error
):
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    header = ROOT / "main" / "mesh" / "meshcore_wire.h"
    changed_header = tmp_path / "meshcore_wire.h"
    changed_header.write_text(
        header.read_text(encoding="utf-8")
        + f"\n{declaration}\n",
        encoding="utf-8",
    )

    with pytest.raises(
        conformance.GateFailure,
        match=error,
    ):
        conformance.load_oracle_coverage_manifest(
            oracle_manifest,
            wire_manifest,
            packet_type_source=changed_header,
        )


def test_duplicate_local_packet_type_code_fails_closed(tmp_path):
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    header = ROOT / "main" / "mesh" / "meshcore_wire.h"
    changed_header = tmp_path / "meshcore_wire.h"
    changed_header.write_text(
        header.read_text(encoding="utf-8")
        + "\n#define D1L_MESHCORE_PAYLOAD_UNREVIEWED 0x0AU\n",
        encoding="utf-8",
    )

    with pytest.raises(
        conformance.GateFailure,
        match="local MeshCore packet type codes must be unique",
    ):
        conformance.load_oracle_coverage_manifest(
            oracle_manifest,
            wire_manifest,
            packet_type_source=changed_header,
        )


@pytest.mark.parametrize(
    ("mutation", "error"),
    [
        ("missing_surface", "oracle required-surface mapping drifted"),
        ("unknown_capability", "oracle required-surface mapping drifted"),
        ("valid_surface_swap", "oracle required-surface mapping drifted"),
        (
            "valid_packet_swap",
            "local packet type semantic coverage mapping drifted",
        ),
    ],
)
def test_oracle_coverage_registry_mutations_fail_closed(tmp_path, mutation, error):
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    coverage = json.loads(COVERAGE_MANIFEST.read_text(encoding="utf-8"))
    if mutation == "missing_surface":
        coverage["required_surfaces"].pop()
    elif mutation == "unknown_capability":
        coverage["required_surfaces"][0]["capabilities"].append(
            "unreviewed_capability"
        )
    elif mutation == "valid_surface_swap":
        public_capabilities = coverage["required_surfaces"][1]["capabilities"]
        dm_capabilities = coverage["required_surfaces"][2]["capabilities"]
        coverage["required_surfaces"][1]["capabilities"] = dm_capabilities
        coverage["required_surfaces"][2]["capabilities"] = public_capabilities
    else:
        packet_types = coverage["local_packet_type_policy"]["packet_types"]
        text_capabilities = packet_types[0]["semantic_capabilities"]
        group_capabilities = packet_types[3]["semantic_capabilities"]
        packet_types[0]["semantic_capabilities"] = group_capabilities
        packet_types[3]["semantic_capabilities"] = text_capabilities
    changed_coverage = tmp_path / "coverage_manifest.json"
    changed_coverage.write_text(
        json.dumps(coverage, indent=2) + "\n", encoding="utf-8"
    )

    with pytest.raises(conformance.GateFailure, match=error):
        conformance.load_oracle_coverage_manifest(
            oracle_manifest,
            wire_manifest,
            coverage_manifest_path=changed_coverage,
        )


def test_oracle_c_abi_wraps_pinned_protocol_helpers_and_production_s_guard():
    header = (ORACLE_ROOT / "meshcore_oracle.h").read_text(encoding="utf-8")
    adapter = (ORACLE_ROOT / "meshcore_oracle.cpp").read_text(encoding="utf-8")
    service = (ROOT / "main" / "mesh" / "meshcore_service.c").read_text(
        encoding="utf-8"
    )
    canonical_guard = (
        ROOT / "main" / "mesh" / "ed25519_canonical.h"
    ).read_text(encoding="utf-8")

    assert '"e8d3c53ba1ea863937081cd0caad759b832f3028"' in header
    assert 'extern "C"' in header
    assert "mesh::Packet upstream" in adapter
    assert "upstream.readFrom" in adapter
    assert "upstream.writeTo" in adapter
    assert "structurally_safe_to_read" in adapter
    assert "structurally_safe_to_write" in adapter
    assert "AdvertDataBuilder upstream" in adapter
    assert "AdvertDataParser upstream" in adapter
    assert "advert_layout_is_canonical" in adapter
    assert "d1l_meshcore_oracle_advert_data_decode" in header
    assert "d1l_meshcore_oracle_advert_data_encode" in header
    assert "d1l_meshcore_oracle_verify_signed_advert" in header
    assert "ed25519_verify" in adapter
    assert '#include "mesh/ed25519_canonical.h"' in adapter
    assert '#include "mesh/ed25519_canonical.h"' in service
    assert "d1l_ed25519_signature_s_is_canonical(signature)" in adapter
    assert "d1l_ed25519_signature_s_is_canonical(signature)" in service
    assert adapter.index("d1l_ed25519_signature_s_is_canonical(signature)") < (
        adapter.index("ed25519_verify(signature")
    )
    assert service.index("d1l_ed25519_signature_s_is_canonical(signature)") < (
        service.index("ed25519_verify(signature")
    )
    assert "D1L_ADVERT_DATA_MAX_LEN" in adapter
    assert "S >= L" in canonical_guard
    assert "d1l_meshcore_oracle_prepare_flood" in header
    assert "d1l_meshcore_oracle_prepare_direct" in header
    assert "d1l_meshcore_oracle_prepare_zero_hop" in header
    assert "d1l_meshcore_oracle_create_ack" in header
    assert "d1l_meshcore_oracle_create_multi_ack" in header
    assert "d1l_meshcore_oracle_parse_ack" in header
    assert "d1l_meshcore_oracle_create_trace" in header
    assert "d1l_meshcore_oracle_prepare_trace_direct" in header
    assert "d1l_meshcore_oracle_parse_trace_source" in header
    assert "mesh::Packet::copyPath" in adapter
    assert "PAYLOAD_TYPE_TRACE" in adapter
    assert "Identity" not in adapter
    assert "Dispatcher" not in adapter


def test_conformance_plan_builds_the_oracle_as_a_separate_target():
    commands = conformance.command_plan("clang", "clang++")
    flattened = [" ".join(command) for command in commands]

    assert any("meshcore_oracle.cpp" in command for command in flattened)
    assert any("meshcore_oracle_vectors.cpp" in command for command in flattened)
    assert any("AdvertDataHelpers.cpp" in command for command in flattened)
    assert any("meshcore_advert_data.o" in command for command in flattened)
    for source in conformance.ED25519_ORACLE_SOURCES:
        source_path = str(source)
        source_commands = [
            command for command in flattened if source_path in command
        ]
        assert len(source_commands) == 1
        assert "-fsanitize=address,undefined" in source_commands[0]
        assert f"meshcore_ed25519_{source.stem}.o" in source_commands[0]
    oracle_link = next(
        command
        for command in flattened
        if command.endswith("meshcore_oracle_vectors")
    )
    for source in conformance.ED25519_ORACLE_SOURCES:
        assert f"meshcore_ed25519_{source.stem}.o" in oracle_link
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
    assert artifact["wp04_acceptance_ready"] is False
    assert artifact["corpus_version"] == 7
    assert artifact["coverage_policy"]["validated"] is True
    assert artifact["coverage_policy"]["unsupported_closure_rejected"] is True
    assert artifact["coverage_policy"]["local_packet_type_count"] == 6
    assert artifact["repository_commit"] == git_head()
    assert artifact["upstream_commit"] == UPSTREAM_COMMIT
    assert artifact["oracle_result"] is None
    assert len(artifact["pending_capabilities"]) == 9
    assert "packet_envelope" not in artifact["pending_capabilities"]
    assert "advert_data_fields" not in artifact["pending_capabilities"]
    assert "signed_advert_verification" not in artifact["pending_capabilities"]
    assert "direct_flood_headers" not in artifact["pending_capabilities"]
    assert "ack_frames" not in artifact["pending_capabilities"]
    assert "trace_source_frames" not in artifact["pending_capabilities"]


def test_oracle_vectors_compile_and_run_deterministically(tmp_path):
    compiler_pair = next(
        (
            (cxx, cc)
            for cxx, cc in (
                (shutil.which("g++"), shutil.which("gcc")),
                (shutil.which("clang++"), shutil.which("clang")),
            )
            if cxx is not None and cc is not None
        ),
        None,
    )
    if compiler_pair is None:
        pytest.skip("no matching host C/C++ compiler pair available")
    compiler, c_compiler = compiler_pair
    executable = tmp_path / (
        "meshcore_oracle_vectors.exe" if os.name == "nt" else "meshcore_oracle_vectors"
    )
    ed25519_objects = []
    subprocess.run(
        [
            c_compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            "-x",
            "c",
            "-fsyntax-only",
            "-",
        ],
        cwd=ROOT,
        input=(
            '#include "mesh/ed25519_canonical.h"\n'
            "int main(void) { unsigned char signature[64] = {0}; "
            "return d1l_ed25519_signature_s_is_canonical(signature) ? 0 : 1; }\n"
        ),
        check=True,
        capture_output=True,
        text=True,
    )
    for source in conformance.ED25519_ORACLE_SOURCES:
        object_path = tmp_path / f"meshcore_ed25519_{source.stem}.o"
        subprocess.run(
            [
                c_compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(conformance.ED25519_ROOT),
                "-c",
                str(source),
                "-o",
                str(object_path),
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        ed25519_objects.append(str(object_path))
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
        str(ORACLE_ROOT / "stubs"),
        "-I",
        str(ROOT / "tests" / "meshcore_conformance" / "stubs"),
        "-I",
        str(ROOT / "main"),
        "-I",
        str(conformance.ED25519_ROOT),
        "-isystem",
        str(ROOT / "third_party" / "MeshCore" / "src"),
        str(ROOT / "third_party" / "MeshCore" / "src" / "Packet.cpp"),
        str(
            ROOT
            / "third_party"
            / "MeshCore"
            / "src"
            / "helpers"
            / "AdvertDataHelpers.cpp"
        ),
        str(ORACLE_ROOT / "meshcore_oracle.cpp"),
        str(ORACLE_ROOT / "meshcore_oracle_vectors.cpp"),
        *ed25519_objects,
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
        "abi_version": 2,
        "upstream_commit": UPSTREAM_COMMIT,
        "vectors": {
            "roundtrip": 26,
            "valid": 4,
            "invalid": 75,
            "semantic": 92,
            "total": 105,
            "packet_envelope": {
                "roundtrip": 4,
                "invalid": 5,
                "semantic": 0,
                "total": 9,
            },
            "advert_data_fields": {
                "roundtrip": 4,
                "invalid": 11,
                "semantic": 15,
                "total": 15,
            },
            "signed_advert_verification": {
                "valid": 3,
                "invalid": 10,
                "semantic": 13,
                "total": 13,
            },
            "ed25519_verifier_kat": {
                "valid": 1,
                "invalid": 3,
                "semantic": 0,
                "total": 4,
            },
            "direct_flood_headers": {
                "roundtrip": 7,
                "invalid": 10,
                "semantic": 17,
                "total": 17,
            },
            "ack_frames": {
                "roundtrip": 5,
                "invalid": 17,
                "semantic": 22,
                "total": 22,
            },
            "trace_source_frames": {
                "roundtrip": 6,
                "invalid": 19,
                "semantic": 25,
                "total": 25,
            },
        },
        "capabilities": {
            "packet_envelope": True,
            "advert_data_fields": True,
            "signed_advert_verification": True,
            "direct_flood_headers": True,
            "ack_frames": True,
            "trace_source_frames": True,
        },
        "failures": 0,
    }
