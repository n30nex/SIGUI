import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCAN_DIRS = ["scripts", ".github/workflows", "main", "boards"]
COM_PATTERN = re.compile(r"\bCOM\d+\b", re.IGNORECASE)


def test_no_executable_hardcoded_com_ports():
    offenders = []
    for rel in SCAN_DIRS:
        path = ROOT / rel
        if not path.exists():
            continue
        for file in path.rglob("*"):
            if file.is_file() and file.suffix.lower() not in {".png", ".bin", ".elf", ".pyc", ".pyo"}:
                text = file.read_text(encoding="utf-8", errors="ignore")
                if COM_PATTERN.search(text):
                    offenders.append(str(file.relative_to(ROOT)))
    assert offenders == []
