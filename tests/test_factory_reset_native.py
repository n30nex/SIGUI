import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_factory_reset_native_behavior(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native factory reset tests")

    executable = tmp_path / (
        "factory_reset_test.exe" if os.name == "nt" else "factory_reset_test"
    )
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "tests/native/stubs"),
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/storage/factory_reset.c"),
        str(ROOT / "tests/native/factory_reset_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native factory reset coordinator: ok"
