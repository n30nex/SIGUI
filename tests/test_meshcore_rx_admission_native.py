import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_meshcore_rx_admission_native(tmp_path: Path) -> None:
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native RX admission tests")
    binary = tmp_path / (
        "meshcore_rx_admission_test.exe"
        if os.name == "nt"
        else "meshcore_rx_admission_test"
    )
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "tests/native/stubs"),
            "-I",
            str(ROOT / "main"),
            str(ROOT / "main/mesh/meshcore_rx_admission.c"),
            str(ROOT / "tests/native/meshcore_rx_admission_test.c"),
            "-o",
            str(binary),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [str(binary)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native MeshCore RX admission: ok"
