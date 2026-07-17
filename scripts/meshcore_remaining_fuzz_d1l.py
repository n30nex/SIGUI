#!/usr/bin/env python3
"""Run the four remaining production-backed WP-05 fuzz targets."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "tests" / "meshcore_remaining_fuzz" / "manifest.json"
CORPUS_PATH = ROOT / "tests" / "meshcore_remaining_fuzz" / "corpus.json"
AES_ROOT = (
    ROOT
    / "third_party"
    / "sensecap_indicator_esp32"
    / "components"
    / "LoRaWAN"
    / "soft-se"
)
AES_SOURCE = AES_ROOT / "aes.c"
STUB_ROOT = ROOT / "tests" / "meshcore_remaining_fuzz" / "stubs"
ORACLE_STUB_ROOT = ROOT / "tests" / "meshcore_oracle" / "stubs"
DEFAULT_RUNS = 100_000
DEFAULT_SEED = 13_746_277
MAX_INPUT_BYTES = 256
TARGET_IDS = (
    "decrypt_auth_path",
    "dm_session_transition_input",
    "retained_envelope_decoder",
    "route_path_parser",
)
EXPECTED_SEED_COUNTS = {
    "decrypt_auth_path": 6,
    "dm_session_transition_input": 5,
    "retained_envelope_decoder": 10,
    "route_path_parser": 12,
}
NATIVE_SPECS = {
    "decrypt_auth_path": {
        "sources": ["main/mesh/meshcore_crypto.c"],
        "harness": "tests/native/meshcore_crypto_test.c",
        "marker": "native MeshCore decrypt/auth crypto: ok",
        "crypto_adapter": True,
    },
    "dm_session_transition_input": {
        "sources": ["main/mesh/dm_delivery_state.c"],
        "harness": "tests/native/dm_delivery_state_test.c",
        "marker": "native DM delivery state: ok",
    },
    "retained_envelope_decoder": {
        "sources": ["main/app/settings_envelope.c"],
        "harness": "tests/native/settings_envelope_test.c",
        "marker": "native settings envelope: ok",
    },
    "route_path_parser": {
        "sources": [
            "main/mesh/meshcore_wire.c",
            "main/mesh/meshcore_path_state.c",
        ],
        "harness": "tests/native/meshcore_route_selection_test.c",
        "marker": "meshcore route selection vectors passed",
    },
}
FUZZ_SPECS = {
    "decrypt_auth_path": {
        "sources": ["main/mesh/meshcore_crypto.c"],
        "harness": "tests/fuzz/meshcore_decrypt_auth_fuzz.c",
        "crypto_adapter": True,
    },
    "dm_session_transition_input": {
        "sources": ["main/mesh/dm_delivery_state.c"],
        "harness": "tests/fuzz/dm_delivery_transition_fuzz.c",
    },
    "retained_envelope_decoder": {
        "sources": ["main/app/settings_envelope.c"],
        "harness": "tests/fuzz/settings_envelope_fuzz.c",
    },
    "route_path_parser": {
        "sources": [
            "main/mesh/meshcore_wire.c",
            "main/mesh/meshcore_path_state.c",
        ],
        "harness": "tests/fuzz/meshcore_route_path_fuzz.c",
    },
}


class GateFailure(RuntimeError):
    pass


def sha256_lf(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def run(
    command: Sequence[str], *, timeout: int, env: dict[str, str]
) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        list(command), cwd=ROOT, capture_output=True, text=True,
        timeout=timeout, env=env, check=False,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"command failed ({completed.returncode}): {list(command)!r}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed


def repository_commit() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True,
        text=True, check=True,
    )
    commit = completed.stdout.strip().lower()
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None:
        raise GateFailure("repository HEAD is not an exact 40-hex commit")
    return commit


def load_inputs() -> tuple[
    dict, dict[str, list[tuple[str, bytes, str]]], dict[str, str]
]:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    corpus = json.loads(CORPUS_PATH.read_text(encoding="utf-8"))
    if (
        manifest.get("schema_version") != 1
        or manifest.get("work_package") != "WP-05"
        or manifest.get("target") != "remaining_wp05_fuzz_targets"
        or manifest.get("status") != "implemented"
        or manifest.get("coverage_boundary")
        != "production_remaining_wp05_fuzz_targets"
        or manifest.get("targets")
        != [{"id": target, "status": "implemented"} for target in TARGET_IDS]
        or manifest.get("engine") != "libFuzzer"
        or manifest.get("runs_per_target") != DEFAULT_RUNS
        or manifest.get("total_runs") != DEFAULT_RUNS * len(TARGET_IDS)
        or manifest.get("seed") != DEFAULT_SEED
        or manifest.get("max_input_bytes") != MAX_INPUT_BYTES
        or manifest.get("sanitizers") != ["address", "undefined"]
        or manifest.get("required_host") != "ubuntu_clang_18_actions"
        or manifest.get("wp05_closure_ready") is not False
        or manifest.get("full_semantic_matrix_covered") is not False
        or manifest.get("release_closure_ready") is not False
    ):
        raise GateFailure("remaining WP-05 fuzz manifest policy drifted")
    if (
        corpus.get("schema_version") != 1
        or corpus.get("encoding") != "hex"
        or corpus.get("coverage_boundary") != manifest["coverage_boundary"]
        or list(corpus.get("targets", {})) != list(TARGET_IDS)
    ):
        raise GateFailure("remaining WP-05 fuzz corpus policy drifted")

    corpus_pin = manifest.get("corpus")
    expected_pin = {
        "path": "tests/meshcore_remaining_fuzz/corpus.json",
        "sha256": sha256_lf(CORPUS_PATH),
        "seed_counts": EXPECTED_SEED_COUNTS,
    }
    if corpus_pin != expected_pin:
        raise GateFailure("remaining WP-05 fuzz corpus pin drifted")

    target_seeds: dict[str, list[tuple[str, bytes, str]]] = {}
    for target in TARGET_IDS:
        seeds: list[tuple[str, bytes, str]] = []
        for seed in corpus["targets"][target]:
            name = seed.get("name")
            if not isinstance(name, str) or re.fullmatch(r"[a-z0-9_]+", name) is None:
                raise GateFailure(f"invalid corpus seed name for {target}")
            payload = bytes.fromhex(seed.get("hex", ""))
            digest = hashlib.sha256(payload).hexdigest()
            if (
                not payload
                or len(payload) > MAX_INPUT_BYTES
                or digest != seed.get("sha256")
            ):
                raise GateFailure(f"corpus seed failed validation: {target}/{name}")
            seeds.append((name, payload, digest))
        expected_count = EXPECTED_SEED_COUNTS[target]
        if (
            len(seeds) != expected_count
            or len({name for name, _payload, _digest in seeds}) != expected_count
        ):
            raise GateFailure(f"corpus seed count drifted: {target}")
        target_seeds[target] = seeds

    source_pins = manifest.get("source_pins")
    if not isinstance(source_pins, dict) or not source_pins:
        raise GateFailure("remaining WP-05 fuzz source pins are missing")
    verified: dict[str, str] = {}
    root = ROOT.resolve()
    for relative, expected in source_pins.items():
        path = (ROOT / relative).resolve()
        try:
            path.relative_to(root)
        except ValueError as exc:
            raise GateFailure(f"source pin escapes repository: {relative}") from exc
        actual = sha256_lf(path)
        if actual != expected:
            raise GateFailure(f"source pin mismatch: {relative}")
        verified[relative] = actual
    return manifest, target_seeds, verified


def common_c_flags(args: argparse.Namespace, sanitizer: str) -> list[str]:
    return [
        args.cc, "-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
        f"-fsanitize={sanitizer}", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "main"),
    ]


def build_crypto_binary(
    args: argparse.Namespace, build: Path, target: str, harness: Path,
    output: Path, *, fuzzer: bool, env: dict[str, str]
) -> None:
    compile_sanitizer = (
        "fuzzer-no-link,address,undefined" if fuzzer else "address,undefined"
    )
    link_sanitizer = "fuzzer,address,undefined" if fuzzer else "address,undefined"
    prefix = build / f"{target}_{'fuzz' if fuzzer else 'native'}"
    aes_object = prefix.with_name(prefix.name + "_aes.o")
    crypto_object = prefix.with_name(prefix.name + "_crypto.o")
    adapter_object = prefix.with_name(prefix.name + "_adapter.o")
    harness_object = prefix.with_name(prefix.name + "_harness.o")
    base = ["-O1", "-g", "-fno-omit-frame-pointer", f"-fsanitize={compile_sanitizer}"]
    run(
        [args.cc, "-std=c11", *base, "-Wall", "-Wextra", "-Werror",
         "-D__CORTEX_M=0", "-DAES_DEC_PREKEYED=1", "-I", str(AES_ROOT),
         "-c", str(AES_SOURCE), "-o", str(aes_object)],
        timeout=args.timeout_sec, env=env,
    )
    run(
        [args.cc, "-std=c11", *base, "-Wall", "-Wextra", "-Werror",
         "-I", str(STUB_ROOT), "-I", str(ROOT / "main"),
         "-c", str(ROOT / "main/mesh/meshcore_crypto.c"),
         "-o", str(crypto_object)],
        timeout=args.timeout_sec, env=env,
    )
    run(
        [args.cxx, "-std=c++17", *base, "-Wall", "-Wextra", "-Werror",
         "-I", str(STUB_ROOT), "-I", str(ORACLE_STUB_ROOT),
         "-I", str(AES_ROOT),
         "-c", str(ROOT / "tests/meshcore_remaining_fuzz/crypto_host_adapter.cpp"),
         "-o", str(adapter_object)],
        timeout=args.timeout_sec, env=env,
    )
    run(
        [args.cc, "-std=c11", *base, "-Wall", "-Wextra", "-Werror",
         "-I", str(STUB_ROOT), "-I", str(ROOT / "main"),
         "-c", str(harness), "-o", str(harness_object)],
        timeout=args.timeout_sec, env=env,
    )
    run(
        [args.cxx, f"-fsanitize={link_sanitizer}", str(aes_object),
         str(crypto_object), str(adapter_object), str(harness_object),
         "-o", str(output)],
        timeout=args.timeout_sec, env=env,
    )


def build_plain_c_binary(
    args: argparse.Namespace, spec: dict, output: Path, *, fuzzer: bool,
    env: dict[str, str]
) -> None:
    sanitizer = "fuzzer,address,undefined" if fuzzer else "address,undefined"
    command = common_c_flags(args, sanitizer)
    command.extend(str(ROOT / source) for source in spec["sources"])
    command.extend([str(ROOT / spec["harness"]), "-o", str(output)])
    run(command, timeout=args.timeout_sec, env=env)


def execute(args: argparse.Namespace) -> tuple[int, dict]:
    started = time.monotonic()
    report: dict = {
        "schema_version": 1,
        "artifact_type": "d1l_meshcore_remaining_wp05_fuzz",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "status": "fail",
        "passed": False,
        "execution_complete": False,
        "repository_commit": None,
        "manifest": None,
        "corpus": None,
        "source_verification": None,
        "native_results": {},
        "fuzz_results": {},
        "target_count": len(TARGET_IDS),
        "runs_per_target": DEFAULT_RUNS,
        "total_requested_runs": DEFAULT_RUNS * len(TARGET_IDS),
        "total_completed_runs": 0,
        "sanitizers": ["address", "undefined"],
        "host_only": True,
        "hardware_closure_eligible": False,
        "rf_closure_eligible": False,
        "wp05_closure_ready": False,
        "full_semantic_matrix_covered": False,
        "release_closure_ready": False,
        "residual_gaps": [
            "three_partial_wp05_semantic_requirements",
            "exact_candidate_rf_and_physical_acceptance",
        ],
        "failure": None,
    }
    try:
        if args.runs != DEFAULT_RUNS or args.seed != DEFAULT_SEED:
            raise GateFailure("release evidence requires the fixed 100000-run seed")
        commit = repository_commit()
        if args.commit and args.commit.lower() != commit:
            raise GateFailure("requested commit does not match repository HEAD")
        manifest, target_seeds, source_pins = load_inputs()
        report["repository_commit"] = commit
        report["manifest"] = {
            "path": "tests/meshcore_remaining_fuzz/manifest.json",
            "sha256": sha256_lf(MANIFEST_PATH),
            "target": manifest["target"],
        }
        report["corpus"] = {
            "path": "tests/meshcore_remaining_fuzz/corpus.json",
            "sha256": sha256_lf(CORPUS_PATH),
            "seed_counts": EXPECTED_SEED_COUNTS,
        }
        report["source_verification"] = {
            "verified": True,
            "source_hash_mode": "canonical_lf_text_sha256",
            "files": source_pins,
        }
        if args.dry_run:
            report["status"] = "dry_run"
            report["failure"] = "dry-run does not satisfy the fuzz gate"
            return 0, report
        if os.name == "nt":
            raise GateFailure("real ASan/UBSan libFuzzer execution requires Ubuntu Actions")

        env = {
            **os.environ,
            "ASAN_OPTIONS": (
                "abort_on_error=1:detect_leaks=1:halt_on_error=1:"
                "strict_string_checks=1"
            ),
            "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
        }
        with tempfile.TemporaryDirectory(prefix="d1l-remaining-wp05-fuzz-") as temporary:
            build = Path(temporary)
            for target in TARGET_IDS:
                spec = NATIVE_SPECS[target]
                binary = build / f"{target}_native"
                if spec.get("crypto_adapter"):
                    build_crypto_binary(
                        args, build, target, ROOT / spec["harness"], binary,
                        fuzzer=False, env=env,
                    )
                else:
                    build_plain_c_binary(args, spec, binary, fuzzer=False, env=env)
                completed = run([str(binary)], timeout=args.timeout_sec, env=env)
                marker = spec["marker"]
                if completed.stdout.strip() != marker:
                    raise GateFailure(f"native marker drifted: {target}")
                report["native_results"][target] = {
                    "passed": True,
                    "stdout": marker,
                }

            for target in TARGET_IDS:
                spec = FUZZ_SPECS[target]
                binary = build / f"{target}_fuzz"
                if spec.get("crypto_adapter"):
                    build_crypto_binary(
                        args, build, target, ROOT / spec["harness"], binary,
                        fuzzer=True, env=env,
                    )
                else:
                    build_plain_c_binary(args, spec, binary, fuzzer=True, env=env)
                corpus_dir = build / f"{target}_corpus"
                corpus_dir.mkdir()
                for index, (name, payload, _digest) in enumerate(target_seeds[target]):
                    (corpus_dir / f"{index:02d}_{name}").write_bytes(payload)
                findings = build / f"{target}_findings"
                findings.mkdir()
                command = [
                    str(binary), f"-runs={args.runs}", f"-seed={args.seed}",
                    f"-max_len={MAX_INPUT_BYTES}", "-print_final_stats=1",
                    f"-artifact_prefix={findings}{os.sep}", str(corpus_dir),
                ]
                fuzz_started = time.monotonic()
                completed = run(command, timeout=args.timeout_sec, env=env)
                duration_ms = round((time.monotonic() - fuzz_started) * 1000)
                finding_files = sorted(path.name for path in findings.iterdir())
                if re.search(r"#100000\s+DONE\b", completed.stderr) is None:
                    raise GateFailure(
                        f"libFuzzer did not complete exactly 100000 runs: {target}"
                    )
                if finding_files:
                    raise GateFailure(f"libFuzzer emitted findings: {target}")
                report["fuzz_results"][target] = {
                    "passed": True,
                    "engine": "libFuzzer",
                    "seed": args.seed,
                    "requested_runs": args.runs,
                    "completed_runs": args.runs,
                    "max_input_bytes": MAX_INPUT_BYTES,
                    "duration_ms": duration_ms,
                    "finding_files": finding_files,
                    "findings": 0,
                }
                report["total_completed_runs"] += args.runs

        report["status"] = "pass"
        report["passed"] = True
        report["execution_complete"] = True
        report["duration_ms"] = round((time.monotonic() - started) * 1000)
        return 0, report
    except (GateFailure, OSError, ValueError, json.JSONDecodeError) as exc:
        report["failure"] = str(exc)
        report["duration_ms"] = round((time.monotonic() - started) * 1000)
        return 1, report


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--commit")
    parser.add_argument("--runs", type=int, default=DEFAULT_RUNS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--timeout-sec", type=int, default=180)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    status, report = execute(args)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return status


if __name__ == "__main__":
    raise SystemExit(main())
