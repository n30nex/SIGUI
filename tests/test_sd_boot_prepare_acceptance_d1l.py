from pathlib import Path

from scripts import sd_boot_prepare_acceptance_d1l as boot_accept

ROOT = Path(__file__).resolve().parents[1]


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

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


def rp2040_ping_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"rp2040 ping","bridge_ready":true,'
        '"protocol_supported":true,"sd_touched":false,'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def ready_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"ready","present":true,"mounted":true,'
        '"data_root_ready":true,"rp2040_protocol_supported":true,'
        '"file_ops":true,"atomic_rename":true,'
        '"file_line_max":512,"file_chunk_max":192,"path_max":96},'
        '"setup_action":"retained_history_sd_enabled",'
        '"format_action":"not_needed","data_enabled":true,'
        '"data_backend":"mixed","message_store_backend":"sd",'
        '"dm_store_backend":"sd","route_store_backend":"sd",'
        '"packet_log_backend":"sd",'
        '"stores":{"messages":"sd","dm":"sd","routes":"sd","packets":"sd"}}\n'
    )


def setup_required_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"setup_required","present":true,"mounted":false,'
        '"data_root_ready":false,"rp2040_protocol_supported":true,'
        '"file_ops":false,"atomic_rename":false},'
        '"setup_action":"format_confirmation_required",'
        '"format_action":"confirm_required","data_backend":"nvs"}\n'
    )


def storage_mount_line(state: str = "ready") -> str:
    return (
        f'{{"schema":1,"ok":true,"cmd":"storage mount",'
        f'"sd":{{"state":"{state}","rp2040_protocol_supported":true}},'
        f'"public_rf_tx":false,"formats_sd":false}}\n'
    )


def storage_setup_line(*, requested: bool = False, performed: bool = False) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage setup",'
        '"setup_action":"format_confirmation_required",'
        '"format_action":"confirm_required",'
        '"confirmation_phrase":"FORMAT-DESKOS-SD",'
        '"will_format":false,'
        f'"format_requested":{str(requested).lower()},'
        f'"format_performed":{str(performed).lower()},'
        '"fallback":"nvs"}\n'
    )


def storage_setup_timeout_line() -> str:
    return (
        '{"schema":1,"ok":false,"cmd":"storage setup","code":"ESP_ERR_TIMEOUT",'
        '"hint":"RP2040 SD format command timed out; no format confirmation was received"}\n'
    )


def filecanary_success_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage filecanary",'
        '"rename_replace":true,"read_final":true,'
        '"delete_final":true,"stat_deleted":true,'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def health_line() -> str:
    return '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n'


def install_fake_serial(monkeypatch, ser):
    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())


def test_dry_run_all_is_safe_by_default():
    report = boot_accept.dry_run_report("all")

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert set(report["scenarios"]) == set(boot_accept.SCENARIOS)
    for scenario in report["scenarios"].values():
        assert not any(command.startswith("mesh send public") for command in scenario["commands"])
        assert not any("FORMAT-DESKOS-SD" in command for command in scenario["commands"])


def test_dry_run_unformatted_can_show_explicit_format_plan():
    report = boot_accept.dry_run_report("unformatted", allow_format_confirm=True)

    assert report["ok"] is True
    assert report["formats_sd"] is True
    assert "storage setup confirm FORMAT-DESKOS-SD" in report["commands"]


def test_confirmed_format_uses_extended_timeout(monkeypatch):
    calls = []

    def fake_send(_ser, command, timeout):
        calls.append((command, timeout))
        return {"schema": 1, "ok": True, "cmd": command}

    monkeypatch.setattr(boot_accept, "send_console_command", fake_send)

    boot_accept.send_with_timeout(object(), "storage setup confirm FORMAT-DESKOS-SD", 5.0)

    assert calls == [("storage setup confirm FORMAT-DESKOS-SD", 660.0)]


def test_firmware_format_timeout_matches_acceptance_window():
    header = (ROOT / "main/storage/storage_status.h").read_text(encoding="utf-8")

    assert "#define D1L_STORAGE_RP2040_SD_FORMAT_TIMEOUT_MS 660000U" in header


def test_correct_structure_requires_ready_storage_and_file_canary(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_mount_line(),
            ready_storage_line(),
            filecanary_success_line(),
            health_line(),
        ]
    )
    install_fake_serial(monkeypatch, ser)

    report = boot_accept.run_acceptance(
        port="COM12",
        baud=115200,
        timeout=1.0,
        scenario="correct-structure",
    )

    assert report["ok"] is True
    assert report["classification"] == "ready_sd_file_gate"
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["format_command_sent"] is False
    assert report["format_confirmed"] is False
    assert ser.writes == [
        "rp2040 ping\n",
        "storage status\n",
        "storage mount\n",
        "storage status\n",
        "storage filecanary\n",
        "health\n",
    ]


def test_unformatted_default_requires_confirmation_without_format(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            setup_required_storage_line(),
            storage_mount_line("setup_required"),
            setup_required_storage_line(),
            storage_setup_line(),
            health_line(),
        ]
    )
    install_fake_serial(monkeypatch, ser)

    report = boot_accept.run_acceptance(
        port="COM12",
        baud=115200,
        timeout=1.0,
        scenario="unformatted",
    )

    assert report["ok"] is True
    assert report["classification"] == "setup_requires_confirmation"
    assert report["formats_sd"] is False
    assert report["format_command_sent"] is False
    assert report["format_confirmed"] is False
    assert "storage setup confirm FORMAT-DESKOS-SD\n" not in ser.writes


def test_unformatted_allow_format_confirms_and_requires_ready_card(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            setup_required_storage_line(),
            storage_mount_line("setup_required"),
            setup_required_storage_line(),
            storage_setup_line(),
            storage_setup_line(requested=True, performed=True),
            ready_storage_line(),
            filecanary_success_line(),
            health_line(),
        ]
    )
    install_fake_serial(monkeypatch, ser)

    report = boot_accept.run_acceptance(
        port="COM12",
        baud=115200,
        timeout=1.0,
        scenario="unformatted",
        allow_format_confirm=True,
    )

    assert report["ok"] is True
    assert report["classification"] == "format_confirmed_ready"
    assert report["format_allowed"] is True
    assert report["formats_sd"] is True
    assert report["format_command_sent"] is True
    assert report["format_confirmed"] is True
    assert "storage setup confirm FORMAT-DESKOS-SD\n" in ser.writes


def test_unformatted_allow_format_timeout_is_not_confirmed(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            setup_required_storage_line(),
            storage_mount_line("setup_required"),
            setup_required_storage_line(),
            storage_setup_line(),
            storage_setup_timeout_line(),
            setup_required_storage_line(),
            health_line(),
        ]
    )
    install_fake_serial(monkeypatch, ser)

    report = boot_accept.run_acceptance(
        port="COM12",
        baud=115200,
        timeout=1.0,
        scenario="unformatted",
        allow_format_confirm=True,
    )

    assert report["ok"] is False
    assert report["classification"] == "format_confirmed_not_ready"
    assert report["format_allowed"] is True
    assert report["format_command_sent"] is True
    assert report["format_confirmed"] is False
    assert report["formats_sd"] is False
    assert report["filecanary"] is None
