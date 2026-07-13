import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_advert_data_is_registered_and_platform_independent():
    cmake = read("main/CMakeLists.txt")
    header = read("main/mesh/advert_data.h")
    source = read("main/mesh/advert_data.c")

    assert '"mesh/advert_data.c"' in cmake
    assert "D1L_ADVERT_DATA_MAX_LEN 32U" in header
    assert "D1L_ADVERT_DATA_NAME_LEN 24U" in header
    assert "d1l_advert_data_parse" in header
    assert "malloc" not in source
    assert "calloc" not in source
    assert "realloc" not in source
    assert "free(" not in source
    assert "esp_" not in source
    assert "freertos" not in source.lower()


def test_advert_data_native_vectors(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for the native advert-data vectors")

    executable = tmp_path / ("advert_data_test.exe" if os.name == "nt" else "advert_data_test")
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/mesh/advert_data.c"),
        str(ROOT / "tests/native/advert_data_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True, capture_output=True, text=True)
