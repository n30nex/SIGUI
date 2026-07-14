import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_volatile_ui_canaries_do_not_consume_durable_store_lineage(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native retained-store tests")

    executable = tmp_path / (
        "retained_store_volatile_test.exe"
        if os.name == "nt"
        else "retained_store_volatile_test"
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
        str(ROOT / "main/mesh/message_store.c"),
        str(ROOT / "main/mesh/dm_delivery_state.c"),
        str(ROOT / "main/mesh/dm_store.c"),
        str(ROOT / "main/mesh/packet_log.c"),
        str(ROOT / "tests/native/retained_store_volatile_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native volatile retained-store isolation: ok"
