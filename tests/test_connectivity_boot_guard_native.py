import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_connectivity_boot_guard_native(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for connectivity boot guard tests")
    executable = tmp_path / (
        "connectivity_boot_guard_test.exe"
        if os.name == "nt"
        else "connectivity_boot_guard_test"
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
            str(ROOT / "main/comms/connectivity_boot_guard.c"),
            str(ROOT / "tests/native/connectivity_boot_guard_test.c"),
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
    assert completed.stdout.strip() == "native connectivity boot guard: ok"
