import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "tests" / "usb_command_parser" / "corpus.json"
MANIFEST = ROOT / "tests" / "usb_command_parser" / "manifest.json"
MARKER = "native USB command parser: ok (100000 deterministic cases)"


def canonical_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def compiler() -> str:
    selected = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
    if not selected:
        raise AssertionError("a C compiler is required for USB parser tests")
    return selected


def test_native_parser_contract_executes_100000_cases(tmp_path: Path):
    executable = tmp_path / ("usb_command_parser_test.exe" if os.name == "nt" else "usb_command_parser_test")
    command = [
        compiler(),
        "-std=c11",
        "-O1",
        "-g",
        "-fno-omit-frame-pointer",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main" / "comms" / "usb_command_parser.c"),
        str(ROOT / "tests" / "native" / "usb_command_parser_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == MARKER


def test_production_consoles_share_explicit_length_admission():
    source = (ROOT / "main" / "comms" / "usb_console.c").read_text(encoding="utf-8")
    cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")

    assert '"comms/usb_command_parser.c"' in cmake
    assert sum(
        line.strip() == "d1l_usb_command_admit_in_place("
        for line in source.splitlines()
    ) == 2
    assert "const size_t received_length = used;" in source
    assert source.count("line, received_length, sizeof(line), &command") == 2
    assert "handle_line(&command);" in source
    assert "static void handle_line(const d1l_usb_command_view_t *command)" in source
    assert "trim_line(line);" not in source
    assert re.search(r"\b(?:atoi|atof)\s*\(", source) is None
    assert source.count("d1l_usb_command_parse_int_exact(") == 5
    assert source.count("d1l_usb_command_parse_double_exact(") == 2

    normal_start = source.index("void d1l_usb_console_run(void)")
    recovery_start = source.index("void d1l_usb_console_run_factory_reset_recovery(")
    normal = source[normal_start:recovery_start]
    recovery = source[recovery_start:]
    for implementation in (normal, recovery):
        admission = implementation.index("d1l_usb_command_admit_in_place(")
        dispatch = min(
            index
            for token in ("handle_line(&command);", "d1l_usb_command_equals(")
            if (index := implementation.find(token)) >= 0
        )
        assert admission < dispatch


def test_corpus_and_manifest_are_hash_bound():
    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert corpus["encoding"] == "hex"
    assert corpus["coverage_boundary"] == "production_usb_command_admission"
    assert len(corpus["seeds"]) == 7
    assert manifest["corpus"] == {
        "path": "tests/usb_command_parser/corpus.json",
        "seed_count": 7,
        "sha256": canonical_sha256(CORPUS),
    }
    assert manifest["engine"] == "libFuzzer"
    assert manifest["runs"] == 100000
    assert manifest["seed"] == 13746277
    assert manifest["max_input_bytes"] == 320
    assert manifest["sanitizers"] == ["address", "undefined"]
    assert manifest["production_integration"] == [
        "main/CMakeLists.txt",
        "main/comms/usb_console.c",
        "main/comms/usb_command_parser.c",
        "main/comms/usb_command_parser.h",
    ]
    assert manifest["source_pins"] == {
        relative: canonical_sha256(ROOT / relative)
        for relative in manifest["source_pins"]
    }
    assert manifest["normal_console_bound"] is True
    assert manifest["factory_reset_recovery_console_bound"] is True
    assert manifest["full_buffer_wipe_on_reject"] is True
    assert manifest["wp05_closure_ready"] is False
    assert manifest["release_closure_ready"] is False
    for seed in corpus["seeds"]:
        payload = bytes.fromhex(seed["hex"])
        assert payload
        assert hashlib.sha256(payload).hexdigest() == seed["sha256"]


def test_libfuzzer_target_is_executable_when_clang_is_available(tmp_path: Path):
    if os.name == "nt":
        pytest.skip(
            "libFuzzer execution is covered by the dedicated pinned Linux Actions job"
        )
    clang = shutil.which("clang") or shutil.which("clang-18")
    if not clang:
        pytest.skip("libFuzzer execution is covered by the pinned Clang Actions job")

    executable = tmp_path / "usb_command_parser_fuzz"
    command = [
        clang,
        "-std=c11",
        "-O1",
        "-g",
        "-fno-omit-frame-pointer",
        "-fsanitize=fuzzer,address,undefined",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main" / "comms" / "usb_command_parser.c"),
        str(ROOT / "tests" / "fuzz" / "usb_command_parser_fuzz.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)

    corpus_data = json.loads(CORPUS.read_text(encoding="utf-8"))
    corpus_dir = tmp_path / "corpus"
    corpus_dir.mkdir()
    for index, seed in enumerate(corpus_data["seeds"]):
        (corpus_dir / f"{index:02d}_{seed['name']}").write_bytes(
            bytes.fromhex(seed["hex"])
        )
    findings = tmp_path / "findings"
    findings.mkdir()
    completed = subprocess.run(
        [
            str(executable),
            "-runs=100000",
            "-seed=13746277",
            "-max_len=320",
            "-print_final_stats=1",
            f"-artifact_prefix={findings}{os.sep}",
            str(corpus_dir),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=120,
        env={
            **os.environ,
            "ASAN_OPTIONS": "abort_on_error=1:detect_leaks=1:halt_on_error=1:strict_string_checks=1",
            "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
        },
    )
    assert re.search(r"#100000\b", completed.stderr)
    assert list(findings.iterdir()) == []


def test_libfuzzer_execution_is_pinned_to_dedicated_linux_actions_job():
    workflow = (ROOT / ".github" / "workflows" / "d1l-ci.yml").read_text(
        encoding="utf-8"
    )
    conformance_job = workflow.split("\n  meshcore-conformance:", 1)[1].split(
        "\n  rp2040-sd-bridge-build:", 1
    )[0]

    assert "runs-on: ubuntu-24.04" in conformance_job
    assert "Install pinned Clang sanitizer toolchain" in conformance_job
    assert "Run production USB command parser fuzz gate" in conformance_job
    assert "python ./scripts/usb_command_parser_fuzz_d1l.py" in conformance_job
    assert "--cc clang-18" in conformance_job
    assert "--seed 13746277" in conformance_job
    assert "--runs 100000" in conformance_job


def test_fuzz_receipt_dry_run_is_exact_source_bound(tmp_path: Path):
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
        capture_output=True, text=True,
    ).stdout.strip()
    receipt = tmp_path / "usb-command-parser-dry-run.json"
    subprocess.run(
        [
            sys.executable,
            os.fspath(ROOT / "scripts" / "usb_command_parser_fuzz_d1l.py"),
            "--commit",
            commit,
            "--dry-run",
            "--out",
            os.fspath(receipt),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    report = json.loads(receipt.read_text(encoding="utf-8"))
    assert report["artifact_type"] == "d1l_usb_command_parser_fuzz"
    assert report["status"] == "dry_run"
    assert report["passed"] is False
    assert report["execution_complete"] is False
    assert report["repository_commit"] == commit
    assert report["source_verification"]["verified"] is True
    assert report["corpus"]["seed_count"] == 7
    assert report["wp05_closure_ready"] is False
    assert report["release_closure_ready"] is False

    workflow = (ROOT / ".github" / "workflows" / "d1l-ci.yml").read_text(
        encoding="utf-8"
    )
    assert "Run production USB command parser fuzz gate" in workflow
    assert "usb_command_parser_fuzz_d1l.py" in workflow
    assert "--cc clang-18" in workflow
    assert "--runs 100000" in workflow
