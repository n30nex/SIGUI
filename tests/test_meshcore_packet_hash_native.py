import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_production_packet_hash_and_cache_native_behavior(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for packet-hash tests")

    executable = tmp_path / (
        "meshcore_packet_hash_test.exe" if os.name == "nt" else "meshcore_packet_hash_test"
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
        str(ROOT / "main/mesh/meshcore_packet_hash.c"),
        str(ROOT / "main/mesh/meshcore_wire.c"),
        str(ROOT / "tests/native/meshcore_packet_hash_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native MeshCore packet hash: ok"
