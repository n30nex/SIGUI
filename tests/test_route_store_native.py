import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_production_route_store_coalescing_and_compact_fallback(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native route-store tests")

    executable = tmp_path / ("route_store_behavior_test.exe" if os.name == "nt" else "route_store_behavior_test")
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "tests/native/stubs"),
        "-I",
        str(ROOT / "tests/native"),
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/mesh/route_store.c"),
        str(ROOT / "tests/native/esp_nvs_stubs.c"),
        str(ROOT / "tests/native/route_store_behavior_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native route-store durability behavior: ok"
