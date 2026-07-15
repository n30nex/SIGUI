from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_ui_compose_eligibility_native(tmp_path: Path) -> None:
    binary = tmp_path / "ui_compose_eligibility_test"
    command = [
        "gcc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "tests/native/stubs"),
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/ui/ui_compose_eligibility.c"),
        str(ROOT / "tests/native/ui_compose_eligibility_test.c"),
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    completed = subprocess.run(
        [str(binary)], check=True, cwd=ROOT, text=True, capture_output=True
    )
    assert completed.stdout.strip() == "native UI compose eligibility: ok"
