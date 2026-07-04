from pathlib import Path

import pytest

from scripts import rp2040_stock_probe_d1l as probe

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_stock_probe_dry_run_is_safe_and_port_guarded():
    args = probe.parse_args(["--dry-run", "--port", "COM12"])

    report = probe.build_report(args)

    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["port"] == "COM12"
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["manual_user_required"] is False
    assert report["commands"] == [
        "rp2040 status",
        "rp2040 baud-probe 700",
        "rp2040 ping",
        "rp2040 stock-probe",
    ]


def test_stock_probe_refuses_forbidden_ports():
    args = probe.parse_args(["--dry-run", "--port", "COM29"])

    with pytest.raises(ValueError):
        probe.build_report(args)


def test_rp2040_baud_probe_console_contract_is_no_sd_no_rf():
    header = read("main/hal/rp2040_bridge.h")
    source = read("main/hal/rp2040_bridge.c")
    console = read("main/comms/usb_console.c")
    validation = read("scripts/autonomous_hardware_validate_d1l.py")

    assert "d1l_rp2040_bridge_baud_probe" in header
    assert "d1l_rp2040_bridge_set_baud" in header
    assert "D1L_RP2040_BAUD_PROBE_MAX_RESULTS" in header
    assert "supported_rp2040_baud" in source
    assert "d1l_rp2040_bridge_ping(&ping, per_probe_timeout)" in source
    assert "d1l_rp2040_bridge_stock_probe(&stock, per_probe_timeout)" in source
    assert "d1l_rp2040_bridge_set_baud(original_baud)" in source
    assert "cmd_rp2040_baud_probe" in console
    assert "cmd_rp2040_set_baud" in console
    assert "rp2040 baud-probe [timeout_ms]" in console
    assert "rp2040 set-baud <baud>" in console
    assert "public_rf_tx" in console
    assert "formats_sd" in console
    assert "sd_touched" in console
    assert "rp2040_try_baud_probe" in validation
    assert "RP2040_BAUD_PROBE_TIMEOUT_MS = 700" in validation
    assert "rp2040 baud-probe" in validation
    assert "rp2040 set-baud" in validation
    assert 'FORBIDDEN_PORTS = {"COM" + "8", "COM" + "11", "COM" + "29"}' in validation
