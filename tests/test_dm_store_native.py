import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_dm_retained_backend_reconciliation_and_split_retry(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native DM store tests")

    executable = tmp_path / (
        "dm_store_behavior_test.exe" if os.name == "nt" else "dm_store_behavior_test"
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
        str(ROOT / "main/mesh/dm_delivery_state.c"),
        str(ROOT / "main/mesh/dm_store.c"),
        str(ROOT / "main/mesh/user_text.c"),
        str(ROOT / "tests/native/dm_store_behavior_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native DM retained durability: ok"
