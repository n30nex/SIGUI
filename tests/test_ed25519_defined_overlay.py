import json
import shutil
import subprocess
import sys
from pathlib import Path

from scripts import validate_ed25519_defined_overlay as validator


ROOT = Path(__file__).resolve().parents[1]


def test_checked_in_overlay_is_exact_mechanical_transform_with_preserved_license():
    receipt = validator.validate_checked_in_overlay()

    assert {
        name: entry["transformed_expressions"] for name, entry in receipt.items()
    } == validator.EXPECTED_TRANSFORM_COUNTS
    assert sum(validator.EXPECTED_TRANSFORM_COUNTS.values()) == 215


def test_overlay_has_no_declared_sanitizer_exception_flags():
    source = (ROOT / "scripts" / "validate_ed25519_defined_overlay.py").read_text(
        encoding="utf-8"
    )
    required_build = source[source.index("if sanitizer:") : source.index("sources = [")]

    assert "-fsanitize=address,undefined" in required_build
    assert "-fno-sanitize-recover=all" in required_build
    assert "-fno-sanitize=" not in required_build


def test_baseline_and_overlay_are_byte_identical_on_host_corpus(tmp_path):
    compiler = next(
        (candidate for name in ("clang-18", "clang", "gcc", "cc")
         if (candidate := shutil.which(name))),
        None,
    )
    assert compiler is not None, "host differential test requires a C compiler"
    receipt = tmp_path / "ed25519-defined-overlay.json"
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "validate_ed25519_defined_overlay.py"),
            "--cc",
            compiler,
            "--sanitizers",
            "off",
            "--out",
            str(receipt),
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        timeout=180,
    )

    assert result.returncode == 0, result.stderr
    payload = json.loads(receipt.read_text(encoding="utf-8"))
    assert payload["differential"] == {
        "deterministic_cases": 256,
        "byte_for_byte_equal": True,
        "output_bytes": 150541,
        "output_sha256": (
            "c37ccb9a5b8a7805dbe0afe44c170eec994e6748f08b2b5296caa67cd8ca4ecb"
        ),
    }
    assert payload["known_answer_tests"] == {
        "RFC8032_section_7_1_test_1_empty_message": "passed",
        "RFC8032_section_7_1_test_2_one_byte_message": "passed",
    }
