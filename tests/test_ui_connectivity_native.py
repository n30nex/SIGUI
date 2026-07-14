import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_ui_connectivity_view_model_native_truth_table(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for UI connectivity tests")
    executable = tmp_path / (
        "ui_connectivity_test.exe" if os.name == "nt" else "ui_connectivity_test"
    )
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            str(ROOT / "main/ui/ui_connectivity.c"),
            str(ROOT / "tests/native/ui_connectivity_test.c"),
            "-o",
            str(executable),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native UI connectivity view model: ok"
