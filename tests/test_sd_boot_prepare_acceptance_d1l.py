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
        '"data_enabled":true,'
        '"data_backend":"mixed","message_store_backend":"sd",'
        '"dm_store_backend":"sd","route_store_backend":"sd",'
        '"packet_log_backend":"sd",'
        '"stores":{"messages":"sd","dm":"sd","routes":"sd","packets":"sd"}}\n'
    )


def setup_required_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"not_fat32_or_unmountable","present":true,"mounted":false,'
        '"data_root_ready":false,"rp2040_protocol_supported":true,'
        '"needs_fat32":true,"file_ops":false,"atomic_rename":false},'
        '"setup_action":"prepare_fat32_on_computer",'
        '"data_backend":"nvs"}\n'
    )


def firmware_mount_error_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"not_fat32_or_unmountable","present":true,"mounted":false,'
        '"data_root_ready":false,"rp2040_protocol_supported":true,'
        '"needs_fat32":true,"mount_error":23,"mount_data":8,'
        '"file_ops":false,"atomic_rename":false},'
        '"setup_action":"inspect_rp2040_sd_mount_error_firmware_path",'
        '"data_backend":"nvs"}\n'
    )


def storage_mount_line(state: str = "ready") -> str:
    return (
        f'{{"schema":1,"ok":true,"cmd":"storage remount",'
        f'"sd":{{"state":"{state}","rp2040_protocol_supported":true}},'
        f'"public_rf_tx":false,"formats_sd":false}}\n'
    )


def storage_setup_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage setup",'
        '"setup_action":"prepare_fat32_on_computer",'
        '"policy":"no_device_format",'
        '"needs_fat32":true,'
        '"will_format":false,'
        '"format_requested":false,'
        '"format_performed":false,'
        '"fallback":"nvs"}\n'
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
        assert not any("setup confirm" in command for command in scenario["commands"])


def test_dry_run_unformatted_never_formats():
    report = boot_accept.dry_run_report("unformatted")

    assert report["ok"] is True
    assert report["formats_sd"] is False
    assert report["commands"] == [
        "rp2040 ping",
        "storage status",
        "storage remount",
        "storage status",
        "storage setup",
        "health",
    ]


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
        "storage remount\n",
        "storage status\n",
        "storage filecanary\n",
        "health\n",
    ]


def test_unformatted_default_reports_computer_fat32_without_format(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            setup_required_storage_line(),
            storage_mount_line("not_fat32_or_unmountable"),
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
    assert report["classification"] == "computer_fat32_required"
    assert report["formats_sd"] is False
    assert report["format_command_sent"] is False
    assert report["format_confirmed"] is False
    assert report["format_allowed"] is False
    assert all("setup confirm" not in write for write in ser.writes)


def test_unformatted_mount_error_is_classified_as_firmware_path(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            firmware_mount_error_storage_line(),
            storage_mount_line("not_fat32_or_unmountable"),
            firmware_mount_error_storage_line(),
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
    assert report["classification"] == "firmware_mount_error_fallback"
    assert report["formats_sd"] is False
    assert report["format_command_sent"] is False
    assert report["format_confirmed"] is False
    assert report["format_allowed"] is False
