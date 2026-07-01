import json

from scripts import smoke_d1l
from scripts.smoke_d1l import SMOKE_COMMANDS, dry_run_report, expected_command_name, parse_jsonl, send_console_command


class FakeSerial:
    def __init__(self, responses):
        self.responses = [line.encode("utf-8") for line in responses]
        self.writes = []
        self.reset_count = 0

    def write(self, data):
        self.writes.append(data.decode("utf-8"))

    def readline(self):
        return self.responses.pop(0) if self.responses else b""

    def reset_input_buffer(self):
        self.reset_count += 1


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
    assert "storage status" in report["commands"]
    assert "storage setup" in report["commands"]
    assert not any(command.startswith("mesh send public") for command in report["commands"])


def test_storage_setup_confirm_maps_to_storage_setup_response():
    assert expected_command_name("storage setup confirm FORMAT-DESKOS-SD") == "storage setup"
    assert expected_command_name("storage map-tile-check remount1") == "storage map-tile-check"
    assert expected_command_name("storage export-canary exportTest1") == "storage export-canary"
    assert expected_command_name("storage export-diagnostics diagTest1") == "storage export-diagnostics"
    assert expected_command_name("storage export-data dataTest1") == "storage export-data"

    ser = FakeSerial(
        [
            '{"schema":1,"ok":false,"cmd":"storage setup","code":"ESP_ERR_NOT_SUPPORTED","hint":"no format was performed"}\n',
        ]
    )
    result = send_console_command(ser, "storage setup confirm FORMAT-DESKOS-SD", timeout=1)

    assert result["cmd"] == "storage setup"
    assert result["code"] == "ESP_ERR_NOT_SUPPORTED"
    assert "no format was performed" in result["hint"]
    assert ser.writes == ["storage setup confirm FORMAT-DESKOS-SD\n"]


def test_contact_mutation_commands_map_to_console_responses():
    assert expected_command_name("contacts add A1B2C3D4E5F60789 CodexTemp") == "contacts add"
    assert expected_command_name("contacts rename A1B2C3D4E5F60789 CodexTemp2") == "contacts rename"
    assert expected_command_name("contacts delete A1B2C3D4E5F60789") == "contacts delete"


def test_persistence_check_reboots_and_restores_defaults(monkeypatch):
    monkeypatch.setattr(smoke_d1l.time, "sleep", lambda _seconds: None)
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"settings reset"}\n',
            '{"schema":1,"ok":true,"cmd":"wifi off","setting_enabled":false}\n',
            '{"schema":1,"ok":true,"cmd":"ble off","setting_enabled":false}\n',
            '{"schema":1,"ok":true,"cmd":"settings set name","node_name":"D1L Smoke Persist"}\n',
            '{"schema":1,"ok":true,"cmd":"settings set pathhash","path_hash_bytes":3}\n',
            '{"schema":1,"ok":true,"cmd":"settings get","node_name":"D1L Smoke Persist","path_hash_bytes":3,"wifi_enabled":false,"ble_companion_enabled":false}\n',
            '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true}\n',
            '{"schema":1,"ok":true,"cmd":"settings get","node_name":"D1L Smoke Persist","path_hash_bytes":3,"wifi_enabled":false,"ble_companion_enabled":false}\n',
            '{"schema":1,"ok":true,"cmd":"settings reset","node_name":"D1L Desk"}\n',
            '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true}\n',
            '{"schema":1,"ok":true,"cmd":"settings get","node_name":"D1L Desk","path_hash_bytes":1,"wifi_enabled":false,"ble_companion_enabled":false}\n',
        ]
    )

    report = smoke_d1l.run_persistence_check(ser, timeout=1)

    assert report["ok"] is True
    assert report["after_reboot_ok"] is True
    assert report["cleanup_ok"] is True
    assert ser.writes == [
        "settings reset\n",
        "wifi off\n",
        "ble off\n",
        "settings set name D1L Smoke Persist\n",
        "settings set pathhash 3\n",
        "settings get\n",
        "reboot\n",
        "settings get\n",
        "settings reset\n",
        "reboot\n",
        "settings get\n",
    ]
    assert ser.reset_count == 2
