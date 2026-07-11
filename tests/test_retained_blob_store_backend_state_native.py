import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_retained_backend_generation_records_unobserved_transition_cycle(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native retained-store tests")

    executable = tmp_path / (
        "retained_blob_store_backend_state_test.exe"
        if os.name == "nt"
        else "retained_blob_store_backend_state_test"
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
        str(ROOT / "main/storage/retained_blob_store.c"),
        str(ROOT / "tests/native/retained_blob_store_backend_state_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native retained backend generation: ok"
