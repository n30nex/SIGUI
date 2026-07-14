import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_retained_worker_timeout_queue_and_lock_recovery(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for retained worker tests")

    executable = tmp_path / (
        "retained_store_worker_test.exe"
        if os.name == "nt"
        else "retained_store_worker_test"
    )
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pthread",
        "-I",
        str(ROOT / "tests/native/retained_worker_stubs"),
        "-I",
        str(ROOT / "tests/native/stubs"),
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/storage/retained_store_scheduler.c"),
        str(ROOT / "main/mesh/route_store_worker.c"),
        str(ROOT / "tests/native/retained_store_worker_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native retained-store worker lifecycle: ok"
