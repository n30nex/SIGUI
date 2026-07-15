import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_settings_persistence_native_behavior(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for native settings persistence tests")

    executable = tmp_path / (
        "settings_persistence_test.exe"
        if os.name == "nt"
        else "settings_persistence_test"
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
        "-I",
        str(ROOT / "third_party/MeshCore/lib/ed25519"),
        str(ROOT / "main/app/settings_model.c"),
        str(ROOT / "main/app/settings_envelope.c"),
        str(ROOT / "overlays/meshcore_ed25519_defined/fe.c"),
        str(ROOT / "overlays/meshcore_ed25519_defined/ge.c"),
        str(ROOT / "third_party/MeshCore/lib/ed25519/keypair.c"),
        str(ROOT / "third_party/MeshCore/lib/ed25519/sha512.c"),
        str(ROOT / "tests/native/esp_nvs_stubs.c"),
        str(ROOT / "tests/native/settings_persistence_test.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert completed.stdout.strip() == "native settings persistence: ok"
