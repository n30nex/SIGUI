import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_contact_uri_native_vectors(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for contact URI vectors")

    executable = tmp_path / ("contact_uri_test.exe" if os.name == "nt" else "contact_uri_test")
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/mesh/contact_uri.c"),
        str(ROOT / "tests/native/contact_uri_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "contact URI vectors: ok"


def test_contact_uri_parser_is_bounded_and_platform_independent():
    header = (ROOT / "main/mesh/contact_uri.h").read_text(encoding="utf-8")
    source = (ROOT / "main/mesh/contact_uri.c").read_text(encoding="utf-8")
    assert "D1L_CONTACT_URI_MAX_LEN 223U" in header
    assert "D1L_CONTACT_URI_NAME_LEN 32U" in header
    assert "d1l_contact_uri_parse" in header
    assert "d1l_contact_uri_format" in header
    assert "utf8_name_is_valid" in source
    for token in ("malloc", "calloc", "realloc", "free(", "esp_", "freertos"):
        assert token not in source.lower()
