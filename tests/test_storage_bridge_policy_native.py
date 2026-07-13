import os
import shutil
import subprocess
from pathlib import Path

from tools.rp2040_sd_protocol import SCENARIOS, STATUS_REPLY, status_line


ROOT = Path(__file__).resolve().parents[1]


def test_production_sd_reply_and_stale_policy_native_behavior(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native SD bridge policy tests")

    executable = tmp_path / (
        "storage_bridge_policy_test.exe" if os.name == "nt" else "storage_bridge_policy_test"
    )
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/hal/rp2040_sd_reply.c"),
        str(ROOT / "main/storage/storage_status_policy.c"),
        str(ROOT / "tests/native/storage_bridge_policy_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True, capture_output=True, text=True)

    for name, scenario in SCENARIOS.items():
        completed = subprocess.run(
            [str(executable), STATUS_REPLY, status_line(scenario)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 0, (
            f"production parser rejected host reference scenario {name}: "
            f"{status_line(scenario)}"
        )
