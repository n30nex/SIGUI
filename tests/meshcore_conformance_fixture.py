from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

from scripts import meshcore_conformance_d1l as conformance
from scripts import verify_ci_tool_inputs


def toolchain_receipt(build_inputs_path: Path, commit: str) -> dict:
    metadata = verify_ci_tool_inputs.load_metadata(build_inputs_path)
    contract = verify_ci_tool_inputs.clang_contract(metadata)
    executable = contract["executable"]
    return {
        "schema": 1,
        "kind": verify_ci_tool_inputs.RECEIPT_KIND,
        "ok": True,
        "source_commit": commit,
        "metadata": {
            "path": ".github/d1l-build-inputs.json",
            "sha256": verify_ci_tool_inputs.sha256_file(build_inputs_path),
        },
        "runner": contract["runner"],
        "architecture": contract["architecture"],
        "clang": {
            "version": contract["version"],
            "package_name": contract["package_name"],
            "package_version": contract["package_version"],
            "archive": {**contract["archive"], "verified": True},
            "executables": {
                role: {
                    "command": executable["commands"][index],
                    "resolved_path": executable["resolved_path"],
                    "sha256": executable["sha256"],
                    "size": executable["size"],
                    "verified": True,
                }
                for index, role in enumerate(("cc", "cxx"))
            },
            "bytes_verified": True,
        },
    }


def completed_report(
    commit: str,
    build_inputs_path: Path,
    *,
    generated_at: datetime | None = None,
) -> dict:
    source_files = {
        name: {
            "expected_sha256": digest,
            "actual_sha256": digest,
            "matched": True,
        }
        for name, digest in conformance.EXPECTED_UPSTREAM["sources"].items()
    }
    seeds = [
        {
            "name": f"seed_{index}",
            "size": index + 1,
            "sha256": f"{index + 1:064x}",
        }
        for index in range(conformance.EXPECTED_FUZZ_CORPUS["seed_count"])
    ]
    temporary = "/tmp/d1l-meshcore-conformance-fixture"
    commands = [
        ["clang-18", f"step-{index}"]
        for index in range(len(conformance.command_plan("cc", "cxx")))
    ]
    for index, source in enumerate(
        conformance.ED25519_SHIFT_BASE_EXCEPTION_SOURCES,
        start=5,
    ):
        commands[index] = [
            "clang-18",
            "-fsanitize=address,undefined",
            conformance.ED25519_SHIFT_BASE_EXCEPTION_FLAG,
            "-c",
            str(source),
        ]
    return {
        "schema_version": 1,
        "artifact_type": "d1l_meshcore_wire_conformance",
        "generated_at": (generated_at or datetime.now(timezone.utc)).isoformat().replace(
            "+00:00", "Z"
        ),
        "passed": True,
        "status": "pass",
        "execution_complete": True,
        "coverage_boundary": "wire_envelope_only",
        "coverage_level": "wire_envelope_only",
        "closure_ready": False,
        "issue_65_closure_eligible": False,
        "excluded_coverage": {
            "crypto_vectors": 0,
            "retained_state_vectors": 0,
            "hardware_vectors": 0,
            "rf_vectors": 0,
            "packet_semantics_vectors": 0,
        },
        "scope": {
            "vector_oracle": "pinned_Packet_wire_read_write_only",
            "fuzz_target": "local_wire_decoder_only",
            "packet_semantics_covered": False,
            "crypto_oracle_available": False,
        },
        "requested": {
            "commit": commit,
            "seed": conformance.DEFAULT_SEED,
            "fuzz_runs": conformance.DEFAULT_RUNS,
            "sanitizers": ["address", "undefined"],
        },
        "sanitizer_policy": conformance.ED25519_SANITIZER_POLICY,
        "full_ubsan_clean": False,
        "sanitizer_policy_passed": True,
        "vector_matrix": conformance.EXPECTED_VECTOR_MATRIX,
        "payload_version_gate": conformance.EXPECTED_PAYLOAD_VERSION_GATE,
        "source_verification": {
            "verified": True,
            "repository_commit": commit,
            "expected_repository_commit": commit,
            "checkout_commit": commit,
            "upstream_commit": conformance.EXPECTED_UPSTREAM["commit"],
            "gitlink_mode": "160000",
            "gitlink_type": "commit",
            "gitlink_commit": conformance.EXPECTED_UPSTREAM["commit"],
            "expected_upstream_commit": conformance.EXPECTED_UPSTREAM["commit"],
            "source_hash_mode": "canonical_lf_text_sha256",
            "source_files": source_files,
            "allowlisted_source_sha256_verified": True,
            "failures": [],
        },
        "corpus": {
            "path": conformance.EXPECTED_FUZZ_CORPUS["manifest"],
            "manifest_sha256": conformance.EXPECTED_FUZZ_CORPUS["sha256"],
            "coverage_boundary": "local_wire_decoder_only",
            "seed_count": len(seeds),
            "seeds": seeds,
            "verified": True,
        },
        "commands": commands,
        "fuzz_command": [f"{temporary}/meshcore_wire_fuzz", "-runs=100000"],
        "vector_result": {
            "passed": True,
            "coverage_boundary": "wire_envelope_only",
            "issue_65_closure_eligible": False,
            "vectors": {
                "upstream_to_local": 432,
                "local_to_upstream": 432,
                "total": 864,
            },
            "path_length_encodings": {"tested": 256, "valid": 119, "invalid": 137},
            "payload_version_gate": {
                "tested": 4,
                "accepted_v1": 1,
                "rejected_future": 3,
                "preserved_rejections": 3,
            },
            "undersized_encoder_capacities": 254,
            "failures": 0,
        },
        "fuzz_result": {
            "passed": True,
            "engine": "libFuzzer",
            "seed": conformance.DEFAULT_SEED,
            "requested_runs": conformance.DEFAULT_RUNS,
            "completed_runs": conformance.DEFAULT_RUNS,
            "duration_ms": 1000,
            "max_input_bytes": 255,
            "sanitizers": ["address", "undefined"],
            "artifact_prefix": "/tmp/findings-fixture",
            "finding_files": [],
            "findings": 0,
        },
        "compiler": {
            "cc": "Ubuntu clang version 18.1.3 (1ubuntu1)",
            "cxx": "Ubuntu clang version 18.1.3 (1ubuntu1)",
        },
        "toolchain": toolchain_receipt(build_inputs_path, commit),
        "sanitizer_errors": 0,
        "memory_errors": 0,
        "zero_overrun": True,
        "zero_corruption": True,
        "failure": None,
    }


def write_completed_report(
    path: Path,
    commit: str,
    build_inputs_path: Path,
    *,
    generated_at: datetime | None = None,
) -> dict:
    report = completed_report(
        commit,
        build_inputs_path,
        generated_at=generated_at,
    )
    path.write_text(json.dumps(report, sort_keys=True), encoding="ascii")
    return report
