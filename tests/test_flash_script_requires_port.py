from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_flash_script_fails_without_port_message():
    text = (ROOT / "scripts/flash_d1l.ps1").read_text(encoding="utf-8")
    assert "D1L_PORT" in text
    assert "No D1L port supplied" in text
    assert "idf.py -p $Port flash" in text


def test_monitor_script_fails_without_port_message():
    text = (ROOT / "scripts/monitor_d1l.ps1").read_text(encoding="utf-8")
    assert "D1L_PORT" in text
    assert "No D1L port supplied" in text
    assert "idf.py -p $Port monitor" in text
