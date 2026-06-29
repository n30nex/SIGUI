import json

from scripts.smoke_d1l import SMOKE_COMMANDS, dry_run_report, parse_jsonl


def test_parse_jsonl_ignores_logs():
    lines = [
        "I (123) boot: log line\n",
        '{"schema":1,"ok":true,"cmd":"version"}\n',
        "d1l> ",
        '{"schema":1,"ok":false,"cmd":"radiohw","code":"SPI_NOT_READY"}\n',
    ]
    parsed = parse_jsonl(lines)
    assert [item["cmd"] for item in parsed] == ["version", "radiohw"]


def test_dry_run_lists_phase1_commands():
    report = dry_run_report()
    assert report["ok"] is True
    assert "radiohw" in report["commands"]
    assert "touch test" in report["commands"]
    assert report["commands"] == SMOKE_COMMANDS
