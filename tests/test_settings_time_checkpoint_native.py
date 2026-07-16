import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_settings_time_checkpoint_native_behavior(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError(
            "A C compiler is required for native retained time checkpoint tests"
        )

    executable = tmp_path / (
        "settings_time_checkpoint_test.exe"
        if os.name == "nt"
        else "settings_time_checkpoint_test"
    )
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DD1L_TEST_REAL_MUTEX=1",
        "-pthread",
        "-I",
        str(ROOT / "tests/native/stubs"),
        "-I",
        str(ROOT / "tests/native"),
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/app/settings_envelope.c"),
        str(ROOT / "main/app/settings_time_checkpoint.c"),
        str(ROOT / "tests/native/esp_nvs_stubs.c"),
        str(ROOT / "tests/native/settings_time_checkpoint_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native retained time checkpoint: ok"
