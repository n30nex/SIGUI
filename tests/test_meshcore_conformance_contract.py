import hashlib
import json
import os
import re
import subprocess
import sys
import copy
from pathlib import Path

import pytest

from scripts import meshcore_conformance_d1l as conformance
from tests.meshcore_conformance_fixture import completed_report


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "meshcore_conformance_d1l.py"
MANIFEST = ROOT / "tests" / "meshcore_conformance" / "manifest.json"
CORPUS = ROOT / "tests" / "meshcore_conformance" / "corpus.json"
ADVERT_CORPUS = ROOT / "tests" / "meshcore_conformance" / "advert_corpus.json"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def canonical_lf_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def dry_run_environment() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("D1L_MESHCORE_CONFORMANCE_CI", None)
    return env


def test_conformance_manifest_pins_exact_upstream_and_fixed_scope():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    assert manifest["coverage_boundary"] == "wire_envelope_only"
    assert manifest["issue_65_closure_eligible"] is False
    assert manifest["upstream"]["commit"] == "e8d3c53ba1ea863937081cd0caad759b832f3028"
    assert manifest["vectors"]["local_to_upstream"] == 504
    assert manifest["vectors"]["upstream_to_local"] == 504
    assert manifest["vectors"]["total"] == 1008
    assert [item["name"] for item in manifest["vector_matrix"]["payload_types"]] == [
        "TXT", "ACK", "ADVERT", "GRP_TXT", "PATH", "TRACE", "MULTIPART"
    ]
    assert len(manifest["vector_matrix"]["route_types"]) == 4
    assert len(manifest["vector_matrix"]["path_cases"]) == 9
    assert manifest["vector_matrix"]["payload_lengths"] == [1, 184]
    assert manifest["payload_version_gate"] == {
        "permissive_envelope_versions": [0, 1, 2, 3],
        "accepted_v1_versions": [0],
        "rejected_future_versions": [1, 2, 3],
        "preserve_output_on_reject": True,
    }
    assert manifest["fuzz_corpus"]["manifest"] == "tests/meshcore_conformance/corpus.json"
    assert manifest["fuzz_corpus"]["seed_count"] == 5
    assert manifest["fuzz_corpus"]["sha256"] == canonical_lf_sha256(CORPUS)
    assert manifest["advert_fuzz_corpus"] == {
        "manifest": "tests/meshcore_conformance/advert_corpus.json",
        "sha256": canonical_lf_sha256(ADVERT_CORPUS),
        "seed_count": 8,
    }
    assert manifest["fuzz"] == {
        "engine": "libFuzzer",
        "runs": 100000,
        "seed": 13746277,
        "max_input_bytes": 255,
        "sanitizers": ["address", "undefined"],
    }
    assert manifest["advert_fuzz"] == {
        "engine": "libFuzzer",
        "runs": 100000,
        "seed": 13746277,
        "max_input_bytes": 33,
        "sanitizers": ["address", "undefined"],
        "target": "local_advert_parser",
        "production_sources": [
            "main/mesh/advert_data.c",
            "main/mesh/advert_data.h",
        ],
        "invalid_output_policy": "canonical_zeroed_with_type_N",
    }
    for relative, expected in manifest["upstream"]["sources"].items():
        actual = canonical_lf_sha256(ROOT / "third_party" / "MeshCore" / relative)
        assert actual == expected


def test_corpus_manifest_is_deterministic_and_hash_checked():
    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))

    assert corpus["encoding"] == "hex"
    assert corpus["coverage_boundary"] == "local_wire_decoder_only"
    assert len(corpus["seeds"]) >= 5
    assert len({seed["name"] for seed in corpus["seeds"]}) == len(corpus["seeds"])
    for seed in corpus["seeds"]:
        payload = bytes.fromhex(seed["hex"])
        assert payload
        assert len(payload) <= 255
        assert hashlib.sha256(payload).hexdigest() == seed["sha256"]


def test_advert_corpus_is_deterministic_and_covers_reject_boundaries():
    corpus = json.loads(ADVERT_CORPUS.read_text(encoding="utf-8"))

    assert corpus["encoding"] == "hex"
    assert corpus["coverage_boundary"] == "local_advert_parser"
    assert len(corpus["seeds"]) == 8
    seeds = {seed["name"]: seed for seed in corpus["seeds"]}
    assert {"truncated_location", "latitude_out_of_range", "oversize"} <= set(
        seeds
    )
    for seed in seeds.values():
        payload = bytes.fromhex(seed["hex"])
        assert payload
        assert len(payload) <= 33
        assert hashlib.sha256(payload).hexdigest() == seed["sha256"]


def test_corpus_pin_is_identical_after_windows_crlf_checkout(tmp_path):
    crlf = tmp_path / "corpus.json"
    crlf.write_bytes(CORPUS.read_bytes().replace(b"\n", b"\r\n"))
    assert conformance.sha256_lf_text_file(crlf) == canonical_lf_sha256(CORPUS)

    advert_crlf = tmp_path / "advert_corpus.json"
    advert_crlf.write_bytes(ADVERT_CORPUS.read_bytes().replace(b"\n", b"\r\n"))
    assert conformance.sha256_lf_text_file(advert_crlf) == canonical_lf_sha256(
        ADVERT_CORPUS
    )


def test_upstream_source_pins_are_identical_after_windows_crlf_checkout(tmp_path):
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    for relative, expected in manifest["upstream"]["sources"].items():
        source = ROOT / "third_party" / "MeshCore" / relative
        crlf = tmp_path / Path(relative).name
        canonical = source.read_bytes().replace(b"\r\n", b"\n")
        crlf.write_bytes(canonical.replace(b"\n", b"\r\n"))
        assert conformance.sha256_lf_text_file(crlf) == expected


def test_harness_has_fixed_bidirectional_matrix_and_structural_sweeps():
    harness = read("tests/meshcore_conformance/meshcore_wire_conformance.cpp")
    fuzzer = read("tests/meshcore_conformance/meshcore_wire_fuzz.cpp")
    advert_fuzzer = read("tests/meshcore_conformance/meshcore_advert_fuzz.cpp")
    shim = read("tests/meshcore_conformance/meshcore_packet_checked.hpp")
    sha_stub = read("tests/meshcore_conformance/stubs/SHA256.h")

    assert "kExpectedPerDirection == 504" in harness
    assert "upstream_to_local == 504U" in harness
    assert "local_to_upstream == 504U" in harness
    assert "value <= 0xFFU" in harness
    assert "result.valid != 119U" in harness
    assert "result.invalid != 137U" in harness
    assert "kRequired == 254U" in harness
    assert "data_region_unchanged" in harness
    assert "verify_payload_version_gate" in harness
    assert "payload_version_gate" in harness
    assert "d1l_meshcore_wire_decode" in fuzzer
    assert "d1l_meshcore_wire_decode_v1" in fuzzer
    assert "d1l_meshcore_wire_encode" in fuzzer
    assert "packet.header != data[0]" in fuzzer
    assert "packet.version != ((data[0] >> 6U) & 0x03U)" in fuzzer
    assert "D1L_MESHCORE_MAX_PACKET_PAYLOAD == MAX_PACKET_PAYLOAD" in shim
    assert "D1L_MESHCORE_ROUTE_DIRECT == ROUTE_TYPE_DIRECT" in shim
    assert "D1L_MESHCORE_PAYLOAD_MULTIPART == PAYLOAD_TYPE_MULTIPART" in shim
    assert "D1L_MESHCORE_PAYLOAD_VER_1 == PAYLOAD_VER_1" in shim
    assert "Packet" not in fuzzer
    assert "d1l_advert_data_parse" in advert_fuzzer
    assert "local_advert_parser" not in advert_fuzzer
    assert "canonical_zeroed_with_type_N" not in advert_fuzzer
    assert "std::memcmp(&first, &second, sizeof(first)) == 0" in advert_fuzzer
    assert "size <= D1L_ADVERT_DATA_MAX_LEN" in advert_fuzzer
    assert "first.lat_e6 >= -90000000" in advert_fuzzer
    assert "first.lon_e6 >= -180000000" in advert_fuzzer
    assert "MAX_PACKET_PAYLOAD == 184" in shim
    assert "std::abort()" in sha_stub
    assert "update(const void *" in sha_stub


def test_v1_decoder_rejects_future_versions_without_changing_output():
    source = read("main/mesh/meshcore_wire.c")
    header = read("main/mesh/meshcore_wire.h")
    runner = read("scripts/meshcore_conformance_d1l.py")
    boundary = read("docs/MESHCORE_CONFORMANCE.md")

    assert "#define D1L_MESHCORE_PAYLOAD_VER_1 0x00U" in header
    assert "bool d1l_meshcore_wire_decode_v1" in header
    body = source.split("bool d1l_meshcore_wire_decode_v1", 1)[1].split(
        "bool d1l_meshcore_wire_write_prefix", 1
    )[0]
    assert "d1l_meshcore_wire_packet_t packet;" in body
    assert "d1l_meshcore_wire_decode(raw, size, &packet)" in body
    assert "packet.version != D1L_MESHCORE_PAYLOAD_VER_1" in body
    assert body.index("packet.version !=") < body.index("*out_packet = packet")
    assert 'vector_result.get("payload_version_gate", {}).get("tested") != 4' in runner
    assert "d1l_meshcore_wire_decode_v1()" in boundary
    assert "leaves its output unchanged" in boundary


def test_production_service_uses_the_codec_exercised_by_the_harness():
    service = read("main/mesh/meshcore_service.c")
    admin_runtime = read("main/mesh/meshcore_admin_runtime.c")
    cmake = read("main/CMakeLists.txt")

    assert '#include "mesh/meshcore_wire.h"' in service
    assert '"mesh/meshcore_wire.c"' in cmake
    # Channel, DM, ACK, PATH, advert, and authenticated admin RESPONSE each
    # enter through the same fail-closed production decoder.
    assert service.count("d1l_meshcore_wire_decode_v1(") == 6
    assert service.count("d1l_meshcore_wire_decode(") == 0
    ack_builder = service.split("static esp_err_t build_dm_ack_response", 1)[1].split(
        "static bool dispatch_bounded_dm_ack", 1
    )[0]
    # Existing packet builders plus admin ANON_REQ and regular REQ both use
    # the single production prefix encoder exercised by conformance vectors.
    assert service.count("d1l_meshcore_wire_write_prefix(") == 7
    assert admin_runtime.count("d1l_meshcore_wire_write_prefix(") == 2
    assert ack_builder.count("d1l_meshcore_wire_write_prefix(") == 3
    assert (
        service.count("d1l_meshcore_wire_write_prefix(")
        + admin_runtime.count("d1l_meshcore_wire_write_prefix(")
        - ack_builder.count("d1l_meshcore_wire_write_prefix(")
        == 6
    )
    assert "parse_wire_packet" not in service
    for legacy_helper in [
        "path_len_valid",
        "path_hash_size",
        "path_hash_count",
        "path_byte_len",
    ]:
        assert not re.search(
            rf"static\s+(?:bool|uint8_t)\s+{legacy_helper}\s*\(", service
        )


def test_documented_ci_cli_dry_run_is_fail_closed(tmp_path):
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    output = tmp_path / "meshcore_conformance.json"
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--cc",
            "clang-18",
            "--cxx",
            "clang++-18",
            "--commit",
            commit,
            "--seed",
            "13746277",
            "--runs",
            "100000",
            "--out",
            str(output),
            "--dry-run",
        ],
        cwd=ROOT,
        env=dry_run_environment(),
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr
    report = json.loads(output.read_text(encoding="utf-8"))
    assert report["passed"] is False
    assert report["execution_complete"] is False
    assert report["status"] == "dry_run"
    assert report["coverage_boundary"] == "wire_envelope_only"
    assert report["coverage_level"] == "wire_envelope_only"
    assert report["issue_65_closure_eligible"] is False
    assert report["closure_ready"] is False
    assert report["source_verification"]["verified"] is True
    assert report["source_verification"]["gitlink_mode"] == "160000"
    assert report["source_verification"]["gitlink_commit"] == (
        "e8d3c53ba1ea863937081cd0caad759b832f3028"
    )
    assert report["source_verification"]["checkout_commit"] == commit
    assert report["source_verification"]["allowlisted_source_sha256_verified"] is True
    assert report["source_verification"]["source_hash_mode"] == (
        "canonical_lf_text_sha256"
    )
    assert report["corpus"]["verified"] is True
    assert report["advert_corpus"]["verified"] is True
    assert report["advert_corpus"]["coverage_boundary"] == "local_advert_parser"
    assert report["advert_corpus"]["seed_count"] == 8
    assert report["scope"]["fuzz_target"] == "local_wire_decoder_only"
    assert report["scope"]["semantic_fuzz_targets"] == ["local_advert_parser"]
    assert report["scope"]["advert_parser_fuzz_covered"] is True
    assert report["scope"]["packet_semantics_covered"] is False
    assert len(report["vector_matrix"]["payload_types"]) == 7
    assert report["payload_version_gate"] == {
        "permissive_envelope_versions": [0, 1, 2, 3],
        "accepted_v1_versions": [0],
        "rejected_future_versions": [1, 2, 3],
        "preserve_output_on_reject": True,
    }
    assert report["zero_overrun"] is None
    assert report["zero_corruption"] is None
    commands = "\n".join(" ".join(command) for command in report["commands"])
    assert "third_party\\MeshCore\\src\\Packet.cpp" in commands or (
        "third_party/MeshCore/src/Packet.cpp" in commands
    )
    assert "-fsanitize=fuzzer,address,undefined" in commands
    assert "-fsanitize=address,undefined" in commands
    assert "main\\mesh\\advert_data.c" in commands or "main/mesh/advert_data.c" in commands
    assert "meshcore_advert_fuzz" in commands
    assert not re.search(r"\bCOM\d+\b", commands, re.IGNORECASE)


def test_manifest_validation_rejects_empty_upstream_source_allowlist(tmp_path, monkeypatch):
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    manifest["upstream"]["sources"] = {}
    changed = tmp_path / "manifest.json"
    changed.write_text(json.dumps(manifest), encoding="utf-8")
    monkeypatch.setattr(conformance, "MANIFEST_PATH", changed)

    with pytest.raises(conformance.GateFailure, match="source allowlist"):
        conformance.load_manifest()


def test_github_actions_cannot_turn_dry_run_into_a_green_gate(tmp_path):
    output = tmp_path / "forbidden-dry-run.json"
    env = os.environ.copy()
    env["D1L_MESHCORE_CONFORMANCE_CI"] = "1"
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--out",
            str(output),
            "--dry-run",
        ],
        cwd=ROOT,
        env=env,
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    report = json.loads(output.read_text(encoding="utf-8"))
    assert report["status"] == "fail"
    assert report["passed"] is False
    assert "dry-run is forbidden in GitHub Actions" in report["failure"]


def test_completed_report_validator_rejects_semantically_incomplete_green_receipts():
    commit = "a" * 40
    report = completed_report(commit, ROOT / ".github" / "d1l-build-inputs.json")
    conformance.validate_completed_report(report, commit)
    canonical = conformance.canonicalize_release_report(report)
    conformance.validate_completed_report(
        canonical,
        commit,
        require_generated_at=False,
    )

    mutations = []
    incomplete_fuzz = copy.deepcopy(report)
    incomplete_fuzz["fuzz_result"]["completed_runs"] = 99999
    mutations.append((incomplete_fuzz, "fuzz_complete"))
    incomplete_advert_fuzz = copy.deepcopy(report)
    incomplete_advert_fuzz["advert_fuzz_result"]["completed_runs"] = 99999
    mutations.append((incomplete_advert_fuzz, "advert_fuzz_complete"))
    stale_advert_corpus = copy.deepcopy(report)
    stale_advert_corpus["advert_corpus"]["manifest_sha256"] = "0" * 64
    mutations.append((stale_advert_corpus, "advert_corpus"))
    incomplete_vectors = copy.deepcopy(report)
    incomplete_vectors["vector_result"]["vectors"]["total"] = 863
    mutations.append((incomplete_vectors, "vector_counts"))
    stale_source = copy.deepcopy(report)
    stale_source["source_verification"]["source_files"]["src/Packet.cpp"][
        "matched"
    ] = False
    mutations.append((stale_source, "source_hashes"))
    unverified_compiler = copy.deepcopy(report)
    unverified_compiler["toolchain"]["clang"]["bytes_verified"] = False
    mutations.append((unverified_compiler, "toolchain_bytes"))
    hidden_sanitizer_exception = copy.deepcopy(report)
    hidden_sanitizer_exception["sanitizer_policy"]["exceptions"] = [
        {"source": "hidden.c", "compiler_flag": "-fno-sanitize=undefined"}
    ]
    mutations.append((hidden_sanitizer_exception, "sanitizer_policy"))
    false_full_ubsan_claim = copy.deepcopy(report)
    false_full_ubsan_claim["full_ubsan_clean"] = False
    mutations.append((false_full_ubsan_claim, "full_ubsan_clean_true"))
    unverified_sanitizer_policy = copy.deepcopy(report)
    unverified_sanitizer_policy["sanitizer_policy_passed"] = False
    mutations.append((unverified_sanitizer_policy, "sanitizer_policy_passed"))
    sanitizer_suppression = copy.deepcopy(report)
    sanitizer_suppression["commands"][5].insert(1, "-fno-sanitize=shift-base")
    mutations.append(
        (sanitizer_suppression, "sanitizer_disable_flags_absent")
    )
    unrelated_sanitizer_suppression = copy.deepcopy(report)
    unrelated_sanitizer_suppression["commands"][5].insert(
        1,
        "-fno-sanitize=undefined",
    )
    mutations.append(
        (unrelated_sanitizer_suppression, "sanitizer_disable_flags_absent")
    )
    string_form_sanitizer_suppression = copy.deepcopy(report)
    string_form_sanitizer_suppression["commands"][0] = (
        "clang-18 -fno-sanitize=undefined source.c"
    )
    mutations.append((string_form_sanitizer_suppression, "commands"))
    weakened_non_exception_sanitizers = copy.deepcopy(report)
    weakened_non_exception_sanitizers["commands"][0] = [
        (
            "-fsanitize=address"
            if argument == "-fsanitize=address,undefined"
            else argument
        )
        for argument in weakened_non_exception_sanitizers["commands"][0]
    ]
    mutations.append((weakened_non_exception_sanitizers, "commands"))

    for payload, failure in mutations:
        with pytest.raises(ValueError, match=failure):
            conformance.validate_completed_report(payload, commit)
        canonical_payload = conformance.canonicalize_release_report(payload)
        with pytest.raises(ValueError, match=failure):
            conformance.validate_completed_report(
                canonical_payload,
                commit,
                require_generated_at=False,
            )


def test_runner_persists_fuzzer_reproducers_and_records_elapsed_time():
    runner = SCRIPT.read_text(encoding="utf-8")
    assert "meshcore_conformance_findings_" in runner
    assert "meshcore_advert_fuzz_findings_" in runner
    assert 'f"-artifact_prefix={findings_dir}{os.sep}"' in runner
    assert 'f"-artifact_prefix={advert_findings_dir}{os.sep}"' in runner
    assert '"duration_ms": fuzz_duration_ms' in runner
    assert '"duration_ms": advert_fuzz_duration_ms' in runner
    assert 'report["zero_overrun"] = True' in runner
    assert 'report["zero_corruption"] = True' in runner


def test_wrong_seed_writes_failure_json_and_exits_nonzero(tmp_path):
    output = tmp_path / "failed.json"
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--seed",
            "1",
            "--runs",
            "100000",
            "--out",
            str(output),
            "--dry-run",
        ],
        cwd=ROOT,
        env=dry_run_environment(),
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    report = json.loads(output.read_text(encoding="utf-8"))
    assert report["passed"] is False
    assert report["status"] == "fail"
    assert "deterministic seed 13746277" in report["failure"]
