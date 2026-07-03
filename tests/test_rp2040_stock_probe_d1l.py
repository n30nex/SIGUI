import pytest

from scripts import rp2040_stock_probe_d1l as probe


def test_stock_probe_dry_run_is_safe_and_port_guarded():
    args = probe.parse_args(["--dry-run", "--port", "COM12"])

    report = probe.build_report(args)

    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["port"] == "COM12"
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["manual_user_required"] is False
    assert report["commands"] == ["rp2040 status", "rp2040 ping", "rp2040 stock-probe"]


def test_stock_probe_refuses_forbidden_ports():
    args = probe.parse_args(["--dry-run", "--port", "COM29"])

    with pytest.raises(ValueError):
        probe.build_report(args)
