import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_ui_nodes_role_summary_native_truth_table(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for UI Nodes summary tests")
    executable = tmp_path / (
        "ui_nodes_summary_test.exe" if os.name == "nt" else "ui_nodes_summary_test"
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
            str(ROOT / "main/ui/ui_nodes_summary.c"),
            str(ROOT / "tests/native/ui_nodes_summary_test.c"),
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
    assert completed.stdout.strip() == "native UI Nodes role summary: ok"
