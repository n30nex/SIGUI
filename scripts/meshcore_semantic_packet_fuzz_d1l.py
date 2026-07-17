#!/usr/bin/env python3
"""Build and execute the production MeshCore semantic packet fuzz gate."""

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
MANIFEST_PATH = ROOT / "tests" / "meshcore_packet_semantics" / "manifest.json"
CORPUS_PATH = ROOT / "tests" / "meshcore_packet_semantics" / "corpus.json"
DEFAULT_RUNS = 100_000
DEFAULT_SEED = 13_746_277
MAX_INPUT_BYTES = 256
NATIVE_MARKER = (
    "native MeshCore semantic packet parser: ok (100000 deterministic cases)"
)


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


def load_inputs() -> tuple[dict, list[tuple[str, bytes, str]], dict[str, str]]:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    corpus = json.loads(CORPUS_PATH.read_text(encoding="utf-8"))
    if (
        manifest.get("schema_version") != 1
        or manifest.get("target") != "semantic_packet_parser"
        or manifest.get("status") != "implemented"
        or manifest.get("coverage_boundary")
        != "production_meshcore_semantic_packet_admission"
        or manifest.get("engine") != "libFuzzer"
        or manifest.get("runs") != DEFAULT_RUNS
        or manifest.get("seed") != DEFAULT_SEED
        or manifest.get("max_input_bytes") != MAX_INPUT_BYTES
        or manifest.get("sanitizers") != ["address", "undefined"]
        or manifest.get("wp05_closure_ready") is not False
        or manifest.get("release_closure_ready") is not False
    ):
        raise GateFailure("semantic packet parser manifest policy drifted")
    if corpus.get("coverage_boundary") != manifest["coverage_boundary"]:
        raise GateFailure("semantic packet parser corpus boundary drifted")

    corpus_pin = manifest.get("corpus", {})
    if corpus_pin != {
        "path": "tests/meshcore_packet_semantics/corpus.json",
        "seed_count": 14,
        "sha256": sha256_lf(CORPUS_PATH),
    }:
        raise GateFailure("semantic packet parser corpus pin drifted")

    seeds: list[tuple[str, bytes, str]] = []
    for seed in corpus.get("seeds", []):
        name = seed.get("name")
        if not isinstance(name, str) or re.fullmatch(r"[a-z0-9_]+", name) is None:
            raise GateFailure("semantic packet corpus seed name is invalid")
        payload = bytes.fromhex(seed.get("hex", ""))
        digest = hashlib.sha256(payload).hexdigest()
        if (
            not payload
            or len(payload) > MAX_INPUT_BYTES
            or digest != seed.get("sha256")
        ):
            raise GateFailure(f"semantic packet corpus seed failed validation: {name}")
        seeds.append((name, payload, digest))
    if len(seeds) != 14 or len({item[0] for item in seeds}) != 14:
        raise GateFailure("semantic packet corpus seed count or names drifted")

    source_pins = manifest.get("source_pins")
    if not isinstance(source_pins, dict) or not source_pins:
        raise GateFailure("semantic packet parser source pins are missing")
    verified: dict[str, str] = {}
    for relative, expected in source_pins.items():
        path = (ROOT / relative).resolve()
        try:
            path.relative_to(ROOT.resolve())
        except ValueError as exc:
            raise GateFailure(f"source pin escapes repository: {relative}") from exc
        actual = sha256_lf(path)
        if actual != expected:
            raise GateFailure(f"source pin mismatch: {relative}")
        verified[relative] = actual
    return manifest, seeds, verified


def execute(args: argparse.Namespace) -> tuple[int, dict]:
    started = time.monotonic()
    report: dict = {
        "schema_version": 1,
        "artifact_type": "d1l_meshcore_semantic_packet_fuzz",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "status": "fail",
        "passed": False,
        "execution_complete": False,
        "repository_commit": None,
        "manifest": None,
        "corpus": None,
        "source_verification": None,
        "native_result": None,
        "fuzz_result": None,
        "sanitizers": ["address", "undefined"],
        "host_only": True,
        "hardware_closure_eligible": False,
        "rf_closure_eligible": False,
        "wp05_closure_ready": False,
        "release_closure_ready": False,
        "residual_gaps": [
            "decrypt_mac_and_signature_authentication",
            "state_transition_and_replay_semantics",
            "rf_and_physical_acceptance",
        ],
        "failure": None,
    }
    try:
        if args.runs != DEFAULT_RUNS or args.seed != DEFAULT_SEED:
            raise GateFailure("release evidence requires the fixed 100000-run seed")
        commit = repository_commit()
        if args.commit and args.commit.lower() != commit:
            raise GateFailure("requested commit does not match repository HEAD")
        manifest, seeds, source_pins = load_inputs()
        report["repository_commit"] = commit
        report["manifest"] = {
            "path": "tests/meshcore_packet_semantics/manifest.json",
            "sha256": sha256_lf(MANIFEST_PATH),
            "target": manifest["target"],
        }
        report["corpus"] = {
            "path": "tests/meshcore_packet_semantics/corpus.json",
            "sha256": sha256_lf(CORPUS_PATH),
            "seed_count": len(seeds),
            "seeds": [
                {"name": name, "size": len(payload), "sha256": digest}
                for name, payload, digest in seeds
            ],
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
        with tempfile.TemporaryDirectory(prefix="d1l-semantic-packet-fuzz-") as temporary:
            build = Path(temporary)
            native = build / "meshcore_packet_semantics_test"
            fuzz = build / "meshcore_packet_semantics_fuzz"
            common = [
                args.cc, "-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "main"),
            ]
            production = [
                str(ROOT / "main/mesh/meshcore_wire.c"),
                str(ROOT / "main/mesh/meshcore_packet_semantics.c"),
            ]
            native_compile = [
                *common, "-fsanitize=address,undefined", *production,
                str(ROOT / "tests/native/meshcore_packet_semantics_test.c"),
                "-o", str(native),
            ]
            run(native_compile, timeout=args.timeout_sec, env=env)
            native_completed = run([str(native)], timeout=args.timeout_sec, env=env)
            if native_completed.stdout.strip() != NATIVE_MARKER:
                raise GateFailure("native semantic packet parser marker drifted")
            report["native_result"] = {
                "passed": True,
                "deterministic_cases": DEFAULT_RUNS,
                "stdout": NATIVE_MARKER,
            }

            fuzz_compile = [
                *common, "-fsanitize=fuzzer,address,undefined", *production,
                str(ROOT / "tests/fuzz/meshcore_packet_semantics_fuzz.c"),
                "-o", str(fuzz),
            ]
            run(fuzz_compile, timeout=args.timeout_sec, env=env)
            corpus_dir = build / "corpus"
            corpus_dir.mkdir()
            for index, (name, payload, _digest) in enumerate(seeds):
                (corpus_dir / f"{index:02d}_{name}").write_bytes(payload)
            findings = build / "findings"
            findings.mkdir()
            fuzz_command = [
                str(fuzz), f"-runs={args.runs}", f"-seed={args.seed}",
                f"-max_len={MAX_INPUT_BYTES}", "-print_final_stats=1",
                f"-artifact_prefix={findings}{os.sep}", str(corpus_dir),
            ]
            fuzz_started = time.monotonic()
            fuzz_completed = run(fuzz_command, timeout=args.timeout_sec, env=env)
            duration_ms = round((time.monotonic() - fuzz_started) * 1000)
            finding_files = sorted(path.name for path in findings.iterdir())
            if re.search(r"#100000\s+DONE\b", fuzz_completed.stderr) is None:
                raise GateFailure("libFuzzer did not report exactly 100000 completed runs")
            if finding_files:
                raise GateFailure("libFuzzer emitted finding artifacts")
            report["fuzz_result"] = {
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
    parser.add_argument("--commit")
    parser.add_argument("--runs", type=int, default=DEFAULT_RUNS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--timeout-sec", type=int, default=120)
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
