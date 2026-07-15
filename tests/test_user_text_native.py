import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def compile_and_run(tmp_path: Path, name: str, sources: list[str], marker: str) -> None:
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native user-text tests")
    executable = tmp_path / (f"{name}.exe" if os.name == "nt" else name)
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        *[str(ROOT / source) for source in sources],
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == marker


def test_strict_utf8_and_138_byte_boundary(tmp_path):
    compile_and_run(
        tmp_path,
        "user_text_test",
        ["main/mesh/user_text.c", "tests/native/user_text_test.c"],
        "native user text: ok",
    )


def test_authenticated_plaintext_admission_is_canonical(tmp_path):
    compile_and_run(
        tmp_path,
        "meshcore_text_plaintext_test",
        [
            "main/mesh/user_text.c",
            "main/mesh/meshcore_text_plaintext.c",
            "tests/native/meshcore_text_plaintext_test.c",
        ],
        "native MeshCore text plaintext: ok",
    )
