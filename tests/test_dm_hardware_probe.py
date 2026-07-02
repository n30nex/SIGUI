from pathlib import Path

from scripts import probe_d1l_dm
from scripts.smoke_d1l import expected_command_name


class FakeSerial:
    def __init__(self):
        self.reset_count = 0

    def reset_input_buffer(self):
        self.reset_count += 1


def test_dm_probe_dry_run_is_dm_only():
    report = probe_d1l_dm.dry_run_report(
        "0BF0A701D5AE2DB6",
        "unit_token",
        Path(r"F:\Meshcorebot\logs\meshcorebot.status.json"),
        "COM11",
    )

    assert report["ok"] is True
    assert report["public_rf_transmit"] is False
    assert any(command == "mesh send dm 0BF0A701D5AE2DB6 unit_token" for command in report["commands"])
    assert not any(command.startswith("mesh send public ") for command in report["commands"])


def test_dm_probe_report_checks_d1l_and_meshbot_evidence():
    before = {
        "serial": {"active_port": "COM11", "meshcore_connected": True},
        "counters": {"rx_contact_total": 7, "rx_log_total": 20},
    }
    after = {
        "serial": {"active_port": "COM11", "meshcore_connected": True},
        "counters": {"rx_contact_total": 8, "rx_log_total": 23},
    }
    steps = [
        {"command": "mesh send dm 0BF0A701D5AE2DB6 unit_token", "result": {"ok": True, "cmd": "mesh send dm"}},
        {
            "command": "messages dm",
            "result": {"ok": True, "cmd": "messages dm", "entries": [{"text": "unit_token"}]},
        },
        {
            "command": "packets search unit_token",
            "result": {"ok": True, "cmd": "packets search", "entries": [{"text_preview": "unit_token"}]},
        },
        {
            "command": "routes trace 0BF0A701D5AE2DB6",
            "result": {"ok": True, "cmd": "routes trace", "fingerprint": "0BF0A701D5AE2DB6"},
        },
        {"command": "health", "result": {"ok": True, "cmd": "health", "board_ready": True, "ui_ready": True}},
    ]

    report = probe_d1l_dm.build_report(
        port="COM7",
        baud=115200,
        bot_status_path=Path("status.json"),
        bot_port="COM11",
        fingerprint="0BF0A701D5AE2DB6",
        token="unit_token",
        commands=[step["command"] for step in steps],
        steps=steps,
        bot_before=before,
        bot_after_dm_wait=after,
        bot_after=after,
        min_contact_delta=1,
    )

    assert report["ok"] is True
    assert report["deltas"]["rx_contact_total"] == 1
    assert report["checks"]["no_public_commands"] is True
    assert report["checks"]["route_trace_has_target"] is True


def test_dm_send_prefix_matches_console_response_name():
    assert expected_command_name("mesh send dm 0BF0A701D5AE2DB6 hello") == "mesh send dm"
    assert expected_command_name("messages dm") == "messages dm"
    assert expected_command_name("messages dm 0BF0A701D5AE2DB6") == "messages dm"
    assert expected_command_name("messages dm clear") == "messages dm clear"


def test_dm_probe_retries_read_only_timeout(monkeypatch):
    responses = iter([
        {"ok": False, "cmd": "identity status", "code": "TIMEOUT"},
        {"ok": True, "cmd": "identity status", "fingerprint": "BA14729E8588E30B"},
    ])
    calls = []

    def fake_send(_ser, command, _timeout):
        calls.append(command)
        return next(responses)

    monkeypatch.setattr(probe_d1l_dm, "send_console_command", fake_send)
    monkeypatch.setattr(probe_d1l_dm.time, "sleep", lambda _seconds: None)
    ser = FakeSerial()

    result = probe_d1l_dm.send_probe_command(ser, "identity status", 1.0)

    assert result["ok"] is True
    assert calls == ["identity status", "identity status"]
    assert ser.reset_count == 1


def test_dm_probe_does_not_retry_dm_send_timeout(monkeypatch):
    calls = []

    def fake_send(_ser, command, _timeout):
        calls.append(command)
        return {"ok": False, "cmd": "mesh send dm", "code": "TIMEOUT"}

    monkeypatch.setattr(probe_d1l_dm, "send_console_command", fake_send)
    ser = FakeSerial()

    result = probe_d1l_dm.send_probe_command(ser, "mesh send dm 0BF0A701D5AE2DB6 token", 1.0)

    assert result["ok"] is False
    assert calls == ["mesh send dm 0BF0A701D5AE2DB6 token"]
    assert ser.reset_count == 0
