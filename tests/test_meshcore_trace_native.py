import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


def test_meshcore_trace_native(tmp_path: pathlib.Path) -> None:
    compiler = os.environ.get("CC", "gcc")
    executable = tmp_path / "meshcore_trace_test"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            str(ROOT / "tests/native/meshcore_trace_test.c"),
            str(ROOT / "main/mesh/meshcore_wire.c"),
            "-o",
            str(executable),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [str(executable)],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    assert completed.stdout.strip() == "meshcore_trace_test: ok"
