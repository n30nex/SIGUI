from scripts import scroll_probe_d1l, smoke_d1l, ui_tab_abuse_d1l


def test_ui_tab_abuse_dry_run_is_explicit_port_safe():
    report = ui_tab_abuse_d1l.dry_run_report(
        cycles=100,
        settle_sec=0.1,
        clear_crashlog_before_start=True,
    )

    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["hardware_required"] is False
    assert report["explicit_port_required"] is True
    assert report["cycles"] == 100
    assert report["tabs"] == ["home", "messages", "nodes", "map", "packets", "settings"]
    assert "ui status" in report["commands"]
    assert "ui tab packets" in report["commands"]


def test_scroll_probe_dry_run_and_screen_parser():
    screens = scroll_probe_d1l.parse_screens("messages,nodes,packets,settings,map")
    report = scroll_probe_d1l.dry_run_report(screens, dwell_sec=0.5, manual_touch=True)

    assert screens == ["messages", "nodes", "packets", "settings", "map"]
    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["explicit_port_required"] is True
    assert report["manual_touch"] is True
    assert "ui tab messages" in report["commands"]
    assert "crashlog" in report["commands"]


def test_scroll_probe_rejects_unknown_screen():
    try:
        scroll_probe_d1l.parse_screens("messages,unknown")
    except ValueError as exc:
        assert "unknown" in str(exc)
    else:
        raise AssertionError("parse_screens accepted an unknown screen")


def test_smoke_knows_ui_console_commands():
    assert "ui status" in smoke_d1l.SMOKE_COMMANDS
    assert smoke_d1l.expected_command_name("ui tab packets") == "ui tab"
