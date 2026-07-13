#!/usr/bin/env python3
"""Run the bounded D1L/Pinned-MeshCore wire-envelope conformance gate.

This intentionally covers only packet framing. It is not a crypto, retained-state,
delivery, RF, or complete MeshCore conformance claim.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "tests" / "meshcore_conformance" / "manifest.json"
CORPUS_PATH = ROOT / "tests" / "meshcore_conformance" / "corpus.json"
HARNESS_PATH = ROOT / "tests" / "meshcore_conformance" / "meshcore_wire_conformance.cpp"
FUZZ_PATH = ROOT / "tests" / "meshcore_conformance" / "meshcore_wire_fuzz.cpp"
WIRE_SOURCE = ROOT / "main" / "mesh" / "meshcore_wire.c"
PACKET_SOURCE = ROOT / "third_party" / "MeshCore" / "src" / "Packet.cpp"
ORACLE_MANIFEST_PATH = ROOT / "tests" / "meshcore_oracle" / "manifest.json"
ORACLE_ADAPTER_PATH = ROOT / "tests" / "meshcore_oracle" / "meshcore_oracle.cpp"
ORACLE_VECTORS_PATH = (
    ROOT / "tests" / "meshcore_oracle" / "meshcore_oracle_vectors.cpp"
)
DEFAULT_SEED = 0xD1C065
DEFAULT_RUNS = 100_000
ORACLE_ABI_VERSION = 1
ORACLE_CORPUS_VERSION = 1
ORACLE_COVERAGE_BOUNDARY = "upstream_packet_envelope_adapter_only"
EXPECTED_UPSTREAM = {
    "name": "MeshCore",
    "path": "third_party/MeshCore",
    "commit": "e8d3c53ba1ea863937081cd0caad759b832f3028",
    "sources": {
        "src/MeshCore.h": "51d93fe4c18608efa827625639a68721cedd9c15a83d55e3b86629cd5710ba8a",
        "src/Packet.cpp": "6f401a4abe57b67aae6ce54172a8cd5cfa64f801c2186894d4bf28aaf30b52e0",
        "src/Packet.h": "dae1e63021df0b85ccd83bd409993e302daf768509a7a28e8485d02344528d9f",
    },
}
EXPECTED_PRODUCTION_WIRE_SOURCES = [
    "main/mesh/meshcore_wire.c",
    "main/mesh/meshcore_wire.h",
]
EXPECTED_FUZZ_CORPUS = {
    "manifest": "tests/meshcore_conformance/corpus.json",
    "sha256": "ad0d3d03f1699a86c61812b5307b0bd47ffd50cd9fdbbbd69f73f1df893eef25",
    "seed_count": 5,
}

EXPECTED_VECTORS = {
    "payload_types": 6,
    "route_types": 4,
    "path_cases": 9,
    "payload_lengths": 2,
    "local_to_upstream": 432,
    "upstream_to_local": 432,
    "total": 864,
}
EXPECTED_PAYLOAD_VERSION_GATE = {
    "permissive_envelope_versions": [0, 1, 2, 3],
    "accepted_v1_versions": [0],
    "rejected_future_versions": [1, 2, 3],
    "preserve_output_on_reject": True,
}
EXPECTED_VECTOR_MATRIX = {
    "payload_types": [
        {"name": "TXT", "code": 2},
        {"name": "ACK", "code": 3},
        {"name": "ADVERT", "code": 4},
        {"name": "GRP_TXT", "code": 5},
        {"name": "PATH", "code": 8},
        {"name": "MULTIPART", "code": 10},
    ],
    "route_types": [
        {"name": "TRANSPORT_FLOOD", "code": 0},
        {"name": "FLOOD", "code": 1},
        {"name": "DIRECT", "code": 2},
        {"name": "TRANSPORT_DIRECT", "code": 3},
    ],
    "path_cases": [
        {"hash_bytes": 1, "hop_count": 0},
        {"hash_bytes": 1, "hop_count": 1},
        {"hash_bytes": 1, "hop_count": 63},
        {"hash_bytes": 2, "hop_count": 0},
        {"hash_bytes": 2, "hop_count": 1},
        {"hash_bytes": 2, "hop_count": 32},
        {"hash_bytes": 3, "hop_count": 0},
        {"hash_bytes": 3, "hop_count": 1},
        {"hash_bytes": 3, "hop_count": 21},
    ],
    "payload_lengths": [1, 184],
}
EXPECTED_FUZZ = {
    "engine": "libFuzzer",
    "runs": DEFAULT_RUNS,
    "seed": DEFAULT_SEED,
    "max_input_bytes": 255,
    "sanitizers": ["address", "undefined"],
}
EXPECTED_ORACLE_CAPABILITIES = [
    {
        "id": "packet_envelope",
        "status": "implemented",
        "owner": "pinned_upstream",
        "semantic": False,
    },
    {"id": "identity_signed_advert", "status": "pending", "owner": "unassigned"},
    {"id": "public_group_packets", "status": "pending", "owner": "unassigned"},
    {"id": "dm_encrypt_decrypt", "status": "pending", "owner": "unassigned"},
    {
        "id": "ack_multiack_ack_path",
        "status": "pending",
        "owner": "unassigned",
    },
    {"id": "direct_flood_routing", "status": "pending", "owner": "unassigned"},
    {
        "id": "path_return_route_codes",
        "status": "pending",
        "owner": "unassigned",
    },
    {"id": "trace_path_discovery", "status": "pending", "owner": "unassigned"},
    {
        "id": "login_request_response_admin",
        "status": "pending",
        "owner": "unassigned",
    },
]


class GateFailure(RuntimeError):
    """A fail-closed gate condition with a concise artifact-safe message."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_lf_text_file(path: Path) -> str:
    """Hash tracked text using the repository's canonical LF representation."""
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def run_process(
    command: Sequence[str],
    *,
    cwd: Path = ROOT,
    timeout: int = 300,
    env: dict[str, str] | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            list(command),
            cwd=cwd,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            env=env,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired) as exc:
        raise GateFailure(f"command could not complete: {command[0]}: {exc}") from exc
    if check and result.returncode != 0:
        stderr = result.stderr[-12_000:].strip()
        stdout = result.stdout[-4_000:].strip()
        detail = stderr or stdout or "no process output"
        raise GateFailure(
            f"command failed with exit {result.returncode}: {command[0]}: {detail}"
        )
    return result


def git_output(*args: str) -> str:
    return run_process(["git", *args], timeout=30).stdout.strip()


def load_manifest() -> dict[str, Any]:
    try:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateFailure(f"cannot read conformance manifest: {exc}") from exc
    if manifest.get("schema_version") != 1:
        raise GateFailure("manifest schema version drifted")
    if manifest.get("coverage_boundary") != "wire_envelope_only":
        raise GateFailure("manifest coverage boundary is not wire_envelope_only")
    if manifest.get("issue_65_closure_eligible") is not False:
        raise GateFailure("manifest must keep Issue #65 closure eligibility false")
    if manifest.get("upstream") != EXPECTED_UPSTREAM:
        raise GateFailure("manifest upstream pin or source allowlist drifted")
    if manifest.get("production_wire_sources") != EXPECTED_PRODUCTION_WIRE_SOURCES:
        raise GateFailure("manifest production wire source list drifted")
    if manifest.get("fuzz_corpus") != EXPECTED_FUZZ_CORPUS:
        raise GateFailure("manifest fuzz corpus pin drifted")
    if manifest.get("constants") != {
        "max_raw_packet": 255,
        "max_path_bytes": 64,
        "max_packet_payload": 184,
    }:
        raise GateFailure("manifest wire constants drifted")
    if manifest.get("vectors") != EXPECTED_VECTORS:
        raise GateFailure("manifest vector counts drifted")
    if manifest.get("payload_version_gate") != EXPECTED_PAYLOAD_VERSION_GATE:
        raise GateFailure("manifest payload version gate drifted")
    if manifest.get("vector_matrix") != EXPECTED_VECTOR_MATRIX:
        raise GateFailure("manifest vector matrix drifted")
    if manifest.get("fuzz") != EXPECTED_FUZZ:
        raise GateFailure("manifest fuzz contract drifted")
    return manifest


def load_oracle_manifest() -> dict[str, Any]:
    try:
        manifest = json.loads(ORACLE_MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateFailure(f"cannot read MeshCore oracle manifest: {exc}") from exc
    if manifest.get("schema_version") != 1:
        raise GateFailure("oracle manifest schema version drifted")
    if manifest.get("corpus_version") != ORACLE_CORPUS_VERSION:
        raise GateFailure("oracle corpus version drifted")
    if manifest.get("abi_version") != ORACLE_ABI_VERSION:
        raise GateFailure("oracle ABI version drifted")
    if manifest.get("coverage_boundary") != ORACLE_COVERAGE_BOUNDARY:
        raise GateFailure("oracle coverage boundary drifted")
    if manifest.get("wp04_closure_eligible") is not False:
        raise GateFailure("first oracle slice must not claim WP-04 closure")
    upstream = manifest.get("upstream")
    if not isinstance(upstream, dict) or upstream.get("commit") != EXPECTED_UPSTREAM["commit"]:
        raise GateFailure("oracle upstream commit does not match the pinned MeshCore pin")
    if upstream.get("path") != EXPECTED_UPSTREAM["path"]:
        raise GateFailure("oracle upstream path drifted")
    expected_upstream_sources = {
        f"{EXPECTED_UPSTREAM['path']}/{relative}": digest
        for relative, digest in EXPECTED_UPSTREAM["sources"].items()
    }
    if upstream.get("sources") != expected_upstream_sources:
        raise GateFailure("oracle upstream source allowlist drifted")
    if manifest.get("capabilities") != EXPECTED_ORACLE_CAPABILITIES:
        raise GateFailure("oracle capability registry drifted")
    if manifest.get("vectors") != {
        "roundtrip": 4,
        "invalid": 5,
        "semantic": 0,
        "total": 9,
    }:
        raise GateFailure("oracle vector contract drifted")
    interface = manifest.get("interface", {})
    if (
        interface.get("language") != "c_abi"
        or interface.get("upstream_type") != "mesh::Packet"
        or interface.get("reject_preserves_output") is not True
        or interface.get("crypto_available") is not False
    ):
        raise GateFailure("oracle interface boundary drifted")
    required_fixtures = manifest.get("determinism", {}).get(
        "future_fixtures_required"
    )
    if required_fixtures != [
        "mock_radio",
        "deterministic_rng",
        "deterministic_rtc",
        "deterministic_millisecond_clock",
        "packet_manager",
        "mesh_tables",
        "contacts",
        "channels",
    ]:
        raise GateFailure("oracle deterministic fixture roadmap drifted")
    oracle_sources = manifest.get("oracle_sources")
    if not isinstance(oracle_sources, dict) or set(oracle_sources) != {
        "tests/meshcore_conformance/stubs/SHA256.h",
        "tests/meshcore_oracle/meshcore_oracle.cpp",
        "tests/meshcore_oracle/meshcore_oracle.h",
        "tests/meshcore_oracle/meshcore_oracle_vectors.cpp",
    }:
        raise GateFailure("oracle source allowlist drifted")
    return manifest


def verify_oracle_sources(manifest: dict[str, Any]) -> dict[str, Any]:
    failures: list[str] = []
    files: dict[str, Any] = {}
    expected_sources = {
        **manifest["upstream"]["sources"],
        **manifest["oracle_sources"],
    }
    root = ROOT.resolve()
    for relative, expected_hash in expected_sources.items():
        if not isinstance(relative, str) or "\\" in relative:
            raise GateFailure("oracle source paths must be repository-relative POSIX paths")
        source = (ROOT / relative).resolve()
        try:
            source.relative_to(root)
        except ValueError as exc:
            raise GateFailure(f"oracle source escapes repository root: {relative}") from exc
        actual_hash = sha256_lf_text_file(source) if source.is_file() else None
        matched = actual_hash == expected_hash
        files[relative] = {
            "expected_sha256": expected_hash,
            "actual_sha256": actual_hash,
            "matched": matched,
        }
        if not matched:
            failures.append(f"oracle source hash mismatch: {relative}")
    result = {
        "verified": not failures,
        "source_hash_mode": "canonical_lf_text_sha256",
        "files": files,
        "failures": failures,
    }
    if failures:
        raise GateFailure("; ".join(failures))
    return result


def load_corpus(manifest: dict[str, Any]) -> tuple[dict[str, Any], list[tuple[str, bytes, str]]]:
    corpus_pin = manifest.get("fuzz_corpus", {})
    expected_path = str(CORPUS_PATH.relative_to(ROOT)).replace("\\", "/")
    if corpus_pin.get("manifest") != expected_path:
        raise GateFailure("primary manifest points at an unexpected fuzz corpus")
    if corpus_pin.get("sha256") != sha256_lf_text_file(CORPUS_PATH):
        raise GateFailure("fuzz corpus manifest SHA256 does not match the primary manifest")
    try:
        corpus = json.loads(CORPUS_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateFailure(f"cannot read fuzz corpus manifest: {exc}") from exc
    if corpus.get("encoding") != "hex" or corpus.get("coverage_boundary") != "local_wire_decoder_only":
        raise GateFailure("fuzz corpus must be hex and local-wire-decoder-only")
    decoded: list[tuple[str, bytes, str]] = []
    names: set[str] = set()
    for entry in corpus.get("seeds", []):
        name = entry.get("name")
        encoded = entry.get("hex")
        expected_hash = entry.get("sha256")
        if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_]+", name) or name in names:
            raise GateFailure("fuzz corpus seed names must be unique safe identifiers")
        try:
            payload = bytes.fromhex(encoded)
        except (TypeError, ValueError) as exc:
            raise GateFailure(f"invalid hex corpus seed {name}") from exc
        actual_hash = hashlib.sha256(payload).hexdigest()
        if not payload or len(payload) > 255 or actual_hash != expected_hash:
            raise GateFailure(f"fuzz corpus seed {name} failed length or SHA256 validation")
        names.add(name)
        decoded.append((name, payload, actual_hash))
    if not decoded:
        raise GateFailure("fuzz corpus manifest has no seeds")
    if len(decoded) != corpus_pin.get("seed_count"):
        raise GateFailure("fuzz corpus seed count does not match the primary manifest")
    return corpus, decoded


def verify_sources(manifest: dict[str, Any], expected_commit: str | None) -> dict[str, Any]:
    upstream = manifest["upstream"]
    upstream_root = ROOT / upstream["path"]
    failures: list[str] = []
    file_results: dict[str, Any] = {}
    for relative, expected_hash in upstream["sources"].items():
        source = upstream_root / relative
        actual_hash = sha256_lf_text_file(source) if source.is_file() else None
        matched = actual_hash == expected_hash
        file_results[relative] = {
            "expected_sha256": expected_hash,
            "actual_sha256": actual_hash,
            "matched": matched,
        }
        if not matched:
            failures.append(f"upstream source hash mismatch: {relative}")

    upstream_head = git_output("-C", str(upstream_root), "rev-parse", "HEAD")
    if upstream_head != upstream["commit"]:
        failures.append("checked-out MeshCore commit does not match manifest")

    gitlink_line = git_output("ls-tree", "HEAD", "--", upstream["path"])
    gitlink_match = re.match(r"(\d{6}) (\w+) ([0-9a-f]{40})\t", gitlink_line)
    gitlink_mode = gitlink_match.group(1) if gitlink_match else None
    gitlink_type = gitlink_match.group(2) if gitlink_match else None
    gitlink_commit = gitlink_match.group(3) if gitlink_match else None
    if gitlink_mode != "160000" or gitlink_type != "commit":
        failures.append("repository MeshCore entry is not a gitlink")
    if gitlink_commit != upstream["commit"]:
        failures.append("repository gitlink does not match manifest")

    repository_commit = git_output("rev-parse", "HEAD")
    if expected_commit and repository_commit != expected_commit:
        failures.append(
            f"repository commit {repository_commit} does not match --commit {expected_commit}"
        )

    result = {
        "verified": not failures,
        "repository_commit": repository_commit,
        "expected_repository_commit": expected_commit,
        "upstream_commit": upstream_head,
        "checkout_commit": repository_commit,
        "gitlink_mode": gitlink_mode,
        "gitlink_type": gitlink_type,
        "gitlink_commit": gitlink_commit,
        "expected_upstream_commit": upstream["commit"],
        "source_hash_mode": "canonical_lf_text_sha256",
        "source_files": file_results,
        "allowlisted_source_sha256_verified": all(
            item["matched"] for item in file_results.values()
        ),
        "failures": failures,
    }
    if failures:
        raise GateFailure("; ".join(failures))
    return result


def command_plan(cc: str, cxx: str, build_dir: str = "$BUILD_DIR") -> list[list[str]]:
    common_sanitizers = "-fsanitize=address,undefined"
    fuzzer_compile_sanitizers = "-fsanitize=fuzzer-no-link,address,undefined"
    return [
        [
            cc,
            "-std=c11",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            common_sanitizers,
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            "-c",
            str(WIRE_SOURCE),
            "-o",
            str(Path(build_dir) / "meshcore_wire_vector.o"),
        ],
        [
            cxx,
            "-std=c++17",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            common_sanitizers,
            "-I",
            str(ROOT / "tests" / "meshcore_conformance" / "stubs"),
            "-isystem",
            str(ROOT / "third_party" / "MeshCore" / "src"),
            "-c",
            str(PACKET_SOURCE),
            "-o",
            str(Path(build_dir) / "meshcore_packet.o"),
        ],
        [
            cxx,
            "-std=c++17",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            common_sanitizers,
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            "-I",
            str(ROOT / "tests" / "meshcore_conformance" / "stubs"),
            "-isystem",
            str(ROOT / "third_party" / "MeshCore" / "src"),
            "-I",
            str(HARNESS_PATH.parent),
            "-c",
            str(HARNESS_PATH),
            "-o",
            str(Path(build_dir) / "meshcore_conformance.o"),
        ],
        [
            cxx,
            common_sanitizers,
            str(Path(build_dir) / "meshcore_wire_vector.o"),
            str(Path(build_dir) / "meshcore_packet.o"),
            str(Path(build_dir) / "meshcore_conformance.o"),
            "-o",
            str(Path(build_dir) / "meshcore_wire_conformance"),
        ],
        [
            cxx,
            "-std=c++17",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            common_sanitizers,
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ORACLE_ADAPTER_PATH.parent),
            "-I",
            str(ROOT / "tests" / "meshcore_conformance" / "stubs"),
            "-isystem",
            str(ROOT / "third_party" / "MeshCore" / "src"),
            "-c",
            str(ORACLE_ADAPTER_PATH),
            "-o",
            str(Path(build_dir) / "meshcore_oracle_adapter.o"),
        ],
        [
            cxx,
            "-std=c++17",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            common_sanitizers,
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ORACLE_ADAPTER_PATH.parent),
            "-isystem",
            str(ROOT / "third_party" / "MeshCore" / "src"),
            "-c",
            str(ORACLE_VECTORS_PATH),
            "-o",
            str(Path(build_dir) / "meshcore_oracle_vectors.o"),
        ],
        [
            cxx,
            common_sanitizers,
            str(Path(build_dir) / "meshcore_packet.o"),
            str(Path(build_dir) / "meshcore_oracle_adapter.o"),
            str(Path(build_dir) / "meshcore_oracle_vectors.o"),
            "-o",
            str(Path(build_dir) / "meshcore_oracle_vectors"),
        ],
        [
            cc,
            "-std=c11",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            fuzzer_compile_sanitizers,
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            "-c",
            str(WIRE_SOURCE),
            "-o",
            str(Path(build_dir) / "meshcore_wire_fuzz.o"),
        ],
        [
            cxx,
            "-std=c++17",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            fuzzer_compile_sanitizers,
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            "-c",
            str(FUZZ_PATH),
            "-o",
            str(Path(build_dir) / "meshcore_fuzz.o"),
        ],
        [
            cxx,
            "-fsanitize=fuzzer,address,undefined",
            str(Path(build_dir) / "meshcore_wire_fuzz.o"),
            str(Path(build_dir) / "meshcore_fuzz.o"),
            "-o",
            str(Path(build_dir) / "meshcore_wire_fuzz"),
        ],
    ]


def compiler_identity(compiler: str) -> str:
    result = run_process([compiler, "--version"], timeout=30)
    identity = (result.stdout or result.stderr).splitlines()[0].strip()
    if "clang" not in identity.lower():
        raise GateFailure(f"sanitizer gate requires Clang, got: {identity}")
    return identity


def parse_harness_output(stdout: str) -> dict[str, Any]:
    for line in reversed(stdout.splitlines()):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict) and "vectors" in value:
            return value
    raise GateFailure("conformance harness did not emit its JSON result")


def parse_oracle_output(stdout: str) -> dict[str, Any]:
    for line in reversed(stdout.splitlines()):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if (
            isinstance(value, dict)
            and value.get("coverage_boundary") == ORACLE_COVERAGE_BOUNDARY
        ):
            return value
    raise GateFailure("MeshCore oracle did not emit its JSON result")


def completed_fuzz_runs(stderr: str, requested: int) -> int | None:
    final_stats = re.findall(r"stat::number_of_executed_units:\s*(\d+)", stderr)
    if final_stats:
        return int(final_stats[-1])
    done_counts = re.findall(r"#(\d+)\s+DONE", stderr)
    if done_counts:
        return int(done_counts[-1])
    if requested == 0 and "Done 0 runs" in stderr:
        return 0
    return None


def base_report(
    manifest: dict[str, Any],
    oracle_manifest: dict[str, Any],
    args: argparse.Namespace,
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "artifact_type": "d1l_meshcore_wire_conformance",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "passed": False,
        "status": "not_run",
        "execution_complete": False,
        "coverage_boundary": "wire_envelope_only",
        "coverage_level": "wire_envelope_only",
        "issue_65_closure_eligible": False,
        "closure_ready": False,
        "excluded_coverage": {
            "crypto_vectors": 0,
            "retained_state_vectors": 0,
            "hardware_vectors": 0,
            "rf_vectors": 0,
            "packet_semantics_vectors": 0,
        },
        "scope": {
            "vector_oracle": "pinned_Packet_wire_read_write_only",
            "upstream_oracle_interface": ORACLE_COVERAGE_BOUNDARY,
            "fuzz_target": "local_wire_decoder_only",
            "packet_semantics_covered": False,
            "crypto_oracle_available": False,
        },
        "requested": {
            "commit": args.commit,
            "seed": args.seed,
            "fuzz_runs": args.runs,
            "sanitizers": ["address", "undefined"],
        },
        "manifest": {
            "path": str(MANIFEST_PATH.relative_to(ROOT)).replace("\\", "/"),
            "sha256": sha256_file(MANIFEST_PATH),
            "upstream_commit": manifest["upstream"]["commit"],
        },
        "oracle": {
            "coverage_boundary": ORACLE_COVERAGE_BOUNDARY,
            "static_manifest": {
                "path": str(ORACLE_MANIFEST_PATH.relative_to(ROOT)).replace(
                    "\\", "/"
                ),
                "sha256": sha256_file(ORACLE_MANIFEST_PATH),
                "corpus_version": ORACLE_CORPUS_VERSION,
                "abi_version": ORACLE_ABI_VERSION,
                "upstream_commit": oracle_manifest["upstream"]["commit"],
            },
            "capabilities": oracle_manifest["capabilities"],
            "source_verification": None,
            "result": None,
            "artifact_path": None,
        },
        "vector_matrix": manifest["vector_matrix"],
        "payload_version_gate": manifest["payload_version_gate"],
        "corpus": None,
        "commands": command_plan(args.cc, args.cxx),
        "source_verification": None,
        "vector_result": None,
        "fuzz_result": None,
        "sanitizer_errors": None,
        "memory_errors": None,
        "zero_overrun": None,
        "zero_corruption": None,
        "failure": None,
    }


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def build_oracle_artifact(report: dict[str, Any]) -> dict[str, Any]:
    oracle = report["oracle"]
    result = oracle["result"]
    source_verification = oracle["source_verification"]
    execution_complete = bool(report.get("execution_complete") and result)
    passed = bool(
        report.get("passed")
        and execution_complete
        and result.get("passed") is True
        and source_verification
        and source_verification.get("verified") is True
    )
    return {
        "schema_version": 1,
        "artifact_type": "meshcore_oracle_manifest",
        "generated_at": report["generated_at"],
        "status": "pass" if passed else report.get("status", "fail"),
        "passed": passed,
        "execution_complete": execution_complete,
        "coverage_boundary": ORACLE_COVERAGE_BOUNDARY,
        "corpus_version": ORACLE_CORPUS_VERSION,
        "abi_version": ORACLE_ABI_VERSION,
        "wp04_closure_eligible": False,
        "closure_ready": False,
        "repository_commit": (report.get("source_verification") or {}).get(
            "repository_commit"
        ),
        "upstream_commit": oracle["static_manifest"]["upstream_commit"],
        "static_manifest": oracle["static_manifest"],
        "source_verification": source_verification,
        "oracle_result": result,
        "capabilities": oracle["capabilities"],
        "pending_capabilities": [
            capability["id"]
            for capability in oracle["capabilities"]
            if capability["status"] == "pending"
        ],
        "failure": report.get("failure"),
    }


def execute(args: argparse.Namespace) -> tuple[int, dict[str, Any]]:
    manifest = load_manifest()
    oracle_manifest = load_oracle_manifest()
    report = base_report(manifest, oracle_manifest, args)
    try:
        if args.dry_run and os.environ.get("D1L_MESHCORE_CONFORMANCE_CI") == "1":
            raise GateFailure("dry-run is forbidden in GitHub Actions")
        if args.runs != DEFAULT_RUNS:
            raise GateFailure(f"release gate requires exactly {DEFAULT_RUNS} fuzz runs")
        if args.seed != DEFAULT_SEED:
            raise GateFailure(f"release gate requires deterministic seed {DEFAULT_SEED}")
        source_verification = verify_sources(manifest, args.commit)
        report["source_verification"] = source_verification
        report["oracle"]["source_verification"] = verify_oracle_sources(
            oracle_manifest
        )
        _corpus, corpus_seeds = load_corpus(manifest)
        report["corpus"] = {
            "path": str(CORPUS_PATH.relative_to(ROOT)).replace("\\", "/"),
            "manifest_sha256": sha256_lf_text_file(CORPUS_PATH),
            "coverage_boundary": "local_wire_decoder_only",
            "seed_count": len(corpus_seeds),
            "seeds": [
                {"name": name, "size": len(payload), "sha256": digest}
                for name, payload, digest in corpus_seeds
            ],
            "verified": True,
        }

        if args.dry_run:
            report["status"] = "dry_run"
            report["failure"] = "dry-run does not satisfy the conformance gate"
            return 0, report
        if os.name == "nt":
            raise GateFailure("real sanitizer conformance runs require the Ubuntu CI job")

        cc_identity = compiler_identity(args.cc)
        cxx_identity = compiler_identity(args.cxx)
        sanitizer_env = os.environ.copy()
        sanitizer_env["ASAN_OPTIONS"] = (
            "abort_on_error=1:detect_leaks=1:halt_on_error=1:strict_string_checks=1"
        )
        sanitizer_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

        with tempfile.TemporaryDirectory(prefix="d1l-meshcore-conformance-") as temporary:
            build_dir = Path(temporary)
            corpus_dir = build_dir / "corpus"
            corpus_dir.mkdir()
            for index, (name, payload, _digest) in enumerate(corpus_seeds):
                (corpus_dir / f"{index:02d}_{name}").write_bytes(payload)
            commands = command_plan(args.cc, args.cxx, str(build_dir))
            report["commands"] = commands
            for command in commands:
                run_process(command, timeout=args.timeout_sec, env=sanitizer_env)

            oracle = run_process(
                [str(build_dir / "meshcore_oracle_vectors")],
                timeout=args.timeout_sec,
                env=sanitizer_env,
            )
            oracle_result = parse_oracle_output(oracle.stdout)
            report["oracle"]["result"] = oracle_result
            if (
                oracle_result.get("passed") is not True
                or oracle_result.get("coverage_boundary")
                != ORACLE_COVERAGE_BOUNDARY
                or oracle_result.get("wp04_closure_eligible") is not False
                or oracle_result.get("abi_version") != ORACLE_ABI_VERSION
                or oracle_result.get("upstream_commit")
                != EXPECTED_UPSTREAM["commit"]
                or oracle_result.get("vectors")
                != {"roundtrip": 4, "invalid": 5, "semantic": 0, "total": 9}
                or oracle_result.get("failures") != 0
            ):
                raise GateFailure("MeshCore oracle result drifted from its fixed matrix")

            harness = run_process(
                [str(build_dir / "meshcore_wire_conformance")],
                timeout=args.timeout_sec,
                env=sanitizer_env,
            )
            vector_result = parse_harness_output(harness.stdout)
            report["vector_result"] = vector_result
            if (
                vector_result.get("passed") is not True
                or vector_result.get("coverage_boundary") != "wire_envelope_only"
                or vector_result.get("issue_65_closure_eligible") is not False
                or vector_result.get("vectors", {}).get("upstream_to_local") != 432
                or vector_result.get("vectors", {}).get("local_to_upstream") != 432
                or vector_result.get("vectors", {}).get("total") != 864
                or vector_result.get("path_length_encodings", {}).get("tested") != 256
                or vector_result.get("path_length_encodings", {}).get("valid") != 119
                or vector_result.get("path_length_encodings", {}).get("invalid") != 137
                or vector_result.get("payload_version_gate", {}).get("tested") != 4
                or vector_result.get("payload_version_gate", {}).get("accepted_v1") != 1
                or vector_result.get("payload_version_gate", {}).get("rejected_future") != 3
                or vector_result.get("payload_version_gate", {}).get("preserved_rejections") != 3
                or vector_result.get("undersized_encoder_capacities") != 254
                or vector_result.get("failures") != 0
            ):
                raise GateFailure("vector harness result did not satisfy the fixed matrix")

            findings_dir = (
                args.out.parent
                / f"meshcore_conformance_findings_{source_verification['repository_commit']}"
            ).resolve()
            findings_dir.mkdir(parents=True, exist_ok=True)
            if any(findings_dir.iterdir()):
                raise GateFailure(f"fuzzer findings directory is not empty: {findings_dir}")
            fuzz_command = [
                str(build_dir / "meshcore_wire_fuzz"),
                f"-runs={args.runs}",
                f"-seed={args.seed}",
                "-max_len=255",
                "-print_final_stats=1",
                f"-artifact_prefix={findings_dir}{os.sep}",
                str(corpus_dir),
            ]
            fuzz_started = time.monotonic()
            try:
                fuzz = run_process(
                    fuzz_command,
                    timeout=args.timeout_sec,
                    env=sanitizer_env,
                )
            except GateFailure:
                report["fuzz_result"] = {
                    "passed": False,
                    "engine": "libFuzzer",
                    "seed": args.seed,
                    "requested_runs": args.runs,
                    "completed_runs": None,
                    "duration_ms": round((time.monotonic() - fuzz_started) * 1000),
                    "max_input_bytes": 255,
                    "sanitizers": ["address", "undefined"],
                    "artifact_prefix": str(findings_dir),
                    "finding_files": sorted(path.name for path in findings_dir.iterdir()),
                }
                raise
            fuzz_duration_ms = round((time.monotonic() - fuzz_started) * 1000)
            completed = completed_fuzz_runs(fuzz.stderr, args.runs)
            finding_files = sorted(path.name for path in findings_dir.iterdir())
            report["fuzz_command"] = fuzz_command
            report["fuzz_result"] = {
                "passed": completed == args.runs,
                "engine": "libFuzzer",
                "seed": args.seed,
                "requested_runs": args.runs,
                "completed_runs": completed,
                "duration_ms": fuzz_duration_ms,
                "max_input_bytes": 255,
                "sanitizers": ["address", "undefined"],
                "artifact_prefix": str(findings_dir),
                "finding_files": finding_files,
                "findings": 0 if completed == args.runs and not finding_files else None,
            }
            if completed != args.runs:
                raise GateFailure(
                    f"libFuzzer completion count was {completed}, expected {args.runs}"
                )
            if finding_files:
                raise GateFailure("libFuzzer emitted finding artifacts despite a zero exit")

        report["compiler"] = {"cc": cc_identity, "cxx": cxx_identity}
        report["sanitizer_errors"] = 0
        report["memory_errors"] = 0
        report["zero_overrun"] = True
        report["zero_corruption"] = True
        report["execution_complete"] = True
        report["passed"] = True
        report["status"] = "pass"
        return 0, report
    except GateFailure as exc:
        report["status"] = "fail"
        report["failure"] = str(exc)
        return 1, report


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang", help="Clang C compiler")
    parser.add_argument("--cxx", default="clang++", help="Clang C++ compiler")
    parser.add_argument("--commit", help="exact repository commit expected by the gate")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--runs", "--fuzz-runs", dest="runs", type=int, default=DEFAULT_RUNS)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--oracle-out",
        type=Path,
        help="output path for the exact-commit MeshCore oracle artifact",
    )
    parser.add_argument("--timeout-sec", type=int, default=300)
    parser.add_argument("--require-sanitizers", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    if args.timeout_sec < 1:
        parser.error("--timeout-sec must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        status, report = execute(args)
    except (GateFailure, OSError, json.JSONDecodeError) as exc:
        report = {
            "schema_version": 1,
            "artifact_type": "d1l_meshcore_wire_conformance",
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "passed": False,
            "status": "fail",
            "execution_complete": False,
            "coverage_boundary": "wire_envelope_only",
            "coverage_level": "wire_envelope_only",
            "issue_65_closure_eligible": False,
            "closure_ready": False,
            "failure": str(exc),
        }
        status = 1
    oracle_artifact = None
    if "oracle" in report:
        repository_commit = (report.get("source_verification") or {}).get(
            "repository_commit"
        )
        artifact_commit = repository_commit or args.commit or "unknown"
        oracle_out = args.oracle_out or (
            args.out.parent / f"meshcore_oracle_manifest_{artifact_commit}.json"
        )
        report["oracle"]["artifact_path"] = str(oracle_out)
        oracle_artifact = build_oracle_artifact(report)
    write_report(args.out, report)
    if oracle_artifact is not None:
        write_report(oracle_out, oracle_artifact)
    print(json.dumps(report, sort_keys=True))
    return status


if __name__ == "__main__":
    sys.exit(main())
