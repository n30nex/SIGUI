from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_dm_conversation_list_native(tmp_path: Path) -> None:
    binary = tmp_path / "dm_conversation_list_test"
    command = [
        "gcc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "tests/native/stubs"),
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/app/dm_conversation_list.c"),
        str(ROOT / "tests/native/dm_conversation_list_test.c"),
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    completed = subprocess.run(
        [str(binary)], check=True, cwd=ROOT, text=True, capture_output=True
    )
    assert completed.stdout.strip() == "native DM conversation list: ok"
