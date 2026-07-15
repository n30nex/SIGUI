import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_channel_message_coordinator_retries_revision_races(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for coordinator tests")

    executable = tmp_path / (
        "channel_message_coordinator_test.exe"
        if os.name == "nt"
        else "channel_message_coordinator_test"
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
        str(ROOT / "main/mesh/channel_message_coordinator.c"),
        str(ROOT / "tests/native/channel_message_coordinator_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native channel-message coordinator: ok"
