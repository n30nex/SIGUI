from pathlib import Path

from scripts import scroll_probe_d1l, smoke_d1l, ui_tab_abuse_d1l

ROOT = Path(__file__).resolve().parents[1]


def test_ui_tab_abuse_dry_run_is_explicit_port_safe():
    report = ui_tab_abuse_d1l.dry_run_report(
        cycles=500,
        settle_sec=0.1,
        clear_crashlog_before_start=True,
    )

    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["hardware_required"] is False
    assert report["explicit_port_required"] is True
    assert report["cycles"] == 500
    assert report["release_min_cycles"] == 500
    assert "heap_free" in report["telemetry_fields"]
    assert "ui_task_stack_free_words" in report["telemetry_fields"]
    assert report["tabs"] == ["home", "messages", "nodes", "map", "packets", "settings"]
    assert "ui status" in report["commands"]
    assert "ui tab packets" in report["commands"]


def test_scroll_probe_dry_run_and_screen_parser():
    screens = scroll_probe_d1l.parse_screens("home,public,dm-thread,nodes,packets,settings,storage,wi-fi,map")
    report = scroll_probe_d1l.dry_run_report(screens, dwell_sec=0.5, manual_touch=True)

    assert screens == [
        "home",
        "public_messages",
        "dm_thread",
        "nodes",
        "packets",
        "settings",
        "storage",
        "wifi",
        "map",
    ]
    assert report["ok"] is True
    assert report["mode"] == "dry-run"
    assert report["explicit_port_required"] is True
    assert report["manual_touch"] is True
    assert {"screen": "storage", "tab": "settings", "label": "Storage settings"} in report["surface_plan"]
    assert {"screen": "dm_thread", "tab": "messages", "label": "DM thread"} in report["surface_plan"]
    assert "ui tab messages" in report["commands"]
    assert "crashlog" in report["commands"]


def test_scroll_probe_rejects_unknown_screen():
    try:
        scroll_probe_d1l.parse_screens("messages,unknown")
    except ValueError as exc:
        assert "unknown" in str(exc)
    else:
        raise AssertionError("parse_screens accepted an unknown screen")


def test_active_release_docs_do_not_treat_short_tab_abuse_as_final_proof():
    release_docs = [
        "docs/DESKOSFINAL.md",
        "docs/ROADMAP.md",
        "docs/KNOWN_LIMITATIONS.md",
        "docs/RELEASE_CHECKLIST.md",
        "docs/TEST_PLAN_D1L.md",
    ]
    offenders = [
        rel
        for rel in release_docs
        if "100-cycle" in (ROOT / rel).read_text(encoding="utf-8")
    ]
    assert offenders == []


def test_smoke_knows_ui_console_commands():
    assert "ui status" in smoke_d1l.SMOKE_COMMANDS
    assert smoke_d1l.expected_command_name("ui tab packets") == "ui tab"
