from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_ui_channel_sheet_secret_lifetime_native(tmp_path: Path) -> None:
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for channel sheet tests")
    binary = tmp_path / (
        "ui_channel_sheets_secret_test.exe"
        if os.name == "nt"
        else "ui_channel_sheets_secret_test"
    )
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DD1L_UI_CHANNEL_SHEETS_SECRET_TEST=1",
            "-I",
            str(ROOT / "tests/native/stubs"),
            "-I",
            str(ROOT / "main"),
            str(ROOT / "main/ui/ui_channel_sheets.c"),
            str(ROOT / "tests/native/ui_channel_sheets_secret_test.c"),
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
    assert completed.stdout.strip() == "native UI channel sheet secrets: ok"
