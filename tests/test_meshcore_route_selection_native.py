import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_meshcore_route_selection_native_vectors(tmp_path: Path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native MeshCore route tests")

    binary = tmp_path / (
        "meshcore_route_selection_test.exe"
        if os.name == "nt"
        else "meshcore_route_selection_test"
    )
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/mesh/meshcore_wire.c"),
        str(ROOT / "tests/native/meshcore_route_selection_test.c"),
        "-o",
        str(binary),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    result = subprocess.run(
        [str(binary)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert result.stdout.strip() == "meshcore route selection vectors passed"
