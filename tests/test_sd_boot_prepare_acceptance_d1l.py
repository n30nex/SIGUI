import json
from pathlib import Path

import pytest

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

    def open(self):
        pass

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


def clean_retained_health_json() -> str:
    store = (
        '"sd_read_fail_count":0,"sd_write_fail_count":0,'
        '"sd_rename_fail_count":0,"nvs_mirror_fail_count":0,'
        '"sd_last_error":"ESP_OK","nvs_mirror_last_error":"ESP_OK",'
        '"sd_degraded_latched":false'
    )
    stores = ",".join(
        f'"{name}":{{{store}}}' for name in boot_accept.RETAINED_STORE_NAMES
    )
    return (
        '"retained_sd":{"degraded":false,"backup_degraded":false,'
        f'"stores":{{{stores}}}}}'
    )


def ready_storage_line(*, attempt: int = 1) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        f'"manager":{{"running":true,"state":"READY_SD","attempt":{attempt}}},'
        '"sd":{"state":"ready","filesystem":"fat32","present":true,"mounted":true,'
        '"data_root_ready":true,"rp2040_protocol_supported":true,'
        '"file_ops":true,"atomic_rename":true,'
        '"file_line_max":512,"file_chunk_max":192,"path_max":96,'
        '"status_stale":false,"presence_stale":false,"refresh_failures":0},'
        '"setup_action":"retained_history_sd_enabled",'
        '"data_enabled":true,'
        '"data_backend":"mixed","message_store_backend":"sd",'
        '"dm_store_backend":"sd","route_store_backend":"sd",'
        '"packet_log_backend":"sd",'
        '"stores":{"messages":"sd","dm":"sd","routes":"sd","packets":"sd"},'
        f'{clean_retained_health_json()}}}\n'
    )


def degraded_ready_storage_line() -> str:
    payload = json.loads(ready_storage_line())
    packets = payload["retained_sd"]["stores"]["packets"]
    packets["sd_read_fail_count"] = 1
    packets["sd_last_error"] = "ESP_ERR_TIMEOUT"
    packets["sd_degraded_latched"] = True
    payload["retained_sd"]["degraded"] = True
    return json.dumps(payload, separators=(",", ":")) + "\n"


def degraded_existing_data_fallback_line() -> str:
    payload = json.loads(setup_required_storage_line())
    payload["retained_sd"] = json.loads("{" + clean_retained_health_json() + "}")[
        "retained_sd"
    ]
    packets = payload["retained_sd"]["stores"]["packets"]
    packets["sd_write_fail_count"] = 1
    packets["sd_last_error"] = "ESP_ERR_TIMEOUT"
    packets["sd_degraded_latched"] = True
    payload["retained_sd"]["degraded"] = True
    return json.dumps(payload, separators=(",", ":")) + "\n"


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


def storage_mount_line(state: str = "ready", *, quiesced: bool = True) -> str:
    return (
        f'{{"schema":1,"ok":true,"cmd":"storage remount",'
        f'"retained_worker_quiesce_acquired":{str(quiesced).lower()},'
        f'"sd":{{"state":"{state}","rp2040_protocol_supported":true}},'
        f'"public_rf_tx":false,"formats_sd":false}}\n'
    )


def storage_remount_busy_line(*, attempt: int = 4) -> str:
    return (
        '{"schema":1,"ok":false,"cmd":"storage remount",'
        '"code":"ESP_ERR_INVALID_STATE",'
        '"retained_worker_quiesce_acquired":false,'
        f'"manager":{{"running":true,"state":"READY_SD","attempt":{attempt}}},'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def protocol_pending_storage_line(state: str = "protocol_pending") -> str:
    return (
        f'{{"schema":1,"ok":true,"cmd":"storage status",'
        f'"sd":{{"state":"{state}","present":false,"mounted":false,'
        f'"data_root_ready":false,"rp2040_protocol_supported":false,'
        f'"file_ops":false,"atomic_rename":false}},'
        f'"setup_action":"bridge_protocol_pending","data_backend":"nvs"}}\n'
    )


def bridge_unavailable_storage_line(*, packets_backend: str = "nvs") -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"protocol_pending","present":false,"mounted":false,'
        '"data_root_ready":false,"rp2040_protocol_supported":false,'
        '"file_ops":false,"atomic_rename":false},'
        '"setup_action":"bridge_protocol_pending","data_enabled":false,'
        '"data_backend":"nvs","stores":{"messages":"nvs","dm":"nvs",'
        f'"routes":"nvs","packets":"{packets_backend}"}}}}\n'
    )


def rp2040_timeout_line() -> str:
    return (
        '{"schema":1,"ok":false,"cmd":"rp2040 ping",'
        '"code":"ESP_ERR_TIMEOUT","bridge_ready":true,'
        '"protocol_supported":false,"sd_touched":false,'
        '"public_rf_tx":false,"formats_sd":false}\n'
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


def test_retained_store_gate_rejects_every_degraded_counter_error_and_latch():
    clean = json.loads(ready_storage_line())
    assert boot_accept.retained_store_gate_ready(clean) is True

    mutations = [
        ("degraded", True),
        ("backup_degraded", True),
    ]
    for field, value in mutations:
        payload = json.loads(ready_storage_line())
        payload["retained_sd"][field] = value
        assert boot_accept.retained_store_gate_ready(payload) is False

    for store_name in boot_accept.RETAINED_STORE_NAMES:
        for field in boot_accept.RETAINED_STORE_COUNTER_FIELDS:
            payload = json.loads(ready_storage_line())
            payload["retained_sd"]["stores"][store_name][field] = 1
            assert boot_accept.retained_store_gate_ready(payload) is False
        for field in ("sd_last_error", "nvs_mirror_last_error"):
            payload = json.loads(ready_storage_line())
            payload["retained_sd"]["stores"][store_name][field] = "ESP_ERR_TIMEOUT"
            assert boot_accept.retained_store_gate_ready(payload) is False
        payload = json.loads(ready_storage_line())
        payload["retained_sd"]["stores"][store_name]["sd_degraded_latched"] = True
        assert boot_accept.retained_store_gate_ready(payload) is False


def test_manager_progress_requires_later_attempt_or_fallback_transition():
    busy = json.loads(storage_remount_busy_line(attempt=4))
    assert (
        boot_accept.manager_progressed_from_busy(
            busy, json.loads(ready_storage_line(attempt=4))
        )
        is False
    )
    assert (
        boot_accept.manager_progressed_from_busy(
            busy, json.loads(ready_storage_line(attempt=5))
        )
        is True
    )

    busy["manager"].pop("attempt")
    same_ready = json.loads(ready_storage_line())
    same_ready["manager"].pop("attempt")
    assert boot_accept.manager_progressed_from_busy(busy, same_ready) is False
    same_ready["manager"]["state"] = "STATUS"
    assert boot_accept.manager_progressed_from_busy(busy, same_ready) is True
    same_ready["manager"] = {"running": False, "state": "READY_SD"}
    assert boot_accept.manager_progressed_from_busy(busy, same_ready) is True


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


def test_correct_structure_rejects_remount_without_retained_worker_quiesce(
    monkeypatch,
):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_mount_line(quiesced=False),
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

    assert report["ok"] is False
    assert report["classification"] == "retained_worker_not_quiesced"
    assert report["retained_worker_quiesce_acquired"] is False
    assert ser.writes == [
        "rp2040 ping\n",
        "storage status\n",
        "storage remount\n",
    ]


def test_boot_acceptance_stops_immediately_after_failed_remount(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            '{"schema":1,"ok":false,"cmd":"storage remount","code":"ESP_FAIL"}\n',
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

    assert report["ok"] is False
    assert report["classification"] == "storage_remount_failed"
    assert ser.writes == [
        "rp2040 ping\n",
        "storage status\n",
        "storage remount\n",
    ]


def test_boot_acceptance_retries_explicit_manager_busy_once_after_fresh_status(
    monkeypatch,
):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_remount_busy_line(),
            ready_storage_line(attempt=4),
            ready_storage_line(attempt=5),
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
        mount_poll_attempts=2,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["classification"] == "ready_sd_file_gate"
    assert report["retained_worker_quiesce_acquired"] is True
    assert report["storage_remount"]["ok"] is True
    assert len(report["storage_remount_attempts"]) == 2
    assert report["storage_remount_attempts"][0]["code"] == "ESP_ERR_INVALID_STATE"
    assert report["storage_remount_attempts"][1]["retained_worker_quiesce_acquired"] is True
    assert len(report["storage_remount_busy_statuses"]) == 2
    assert report["storage_remount_manager_progress_observed"] is True
    assert ser.writes == [
        "rp2040 ping\n",
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage filecanary\n",
        "health\n",
    ]


def test_boot_acceptance_does_not_retry_same_attempt_ready_status(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(attempt=4),
            storage_remount_busy_line(attempt=4),
            ready_storage_line(attempt=4),
            storage_mount_line(),
            ready_storage_line(attempt=5),
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
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is False
    assert report["classification"] == "storage_remount_failed"
    assert report["storage_remount_manager_progress_observed"] is False
    assert len(report["storage_remount_attempts"]) == 1
    assert ser.writes == [
        "rp2040 ping\n",
        "storage status\n",
        "storage remount\n",
        "storage status\n",
    ]


@pytest.mark.parametrize(
    ("retry_line", "classification"),
    [
        (
            '{"schema":1,"ok":false,"cmd":"storage remount","code":"ESP_FAIL"}\n',
            "storage_remount_failed",
        ),
        (storage_mount_line(quiesced=False), "retained_worker_not_quiesced"),
    ],
)
def test_boot_acceptance_busy_retry_failure_stops_before_later_storage_ops(
    monkeypatch, retry_line, classification
):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_remount_busy_line(),
            ready_storage_line(attempt=5),
            retry_line,
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
        mount_poll_attempts=2,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is False
    assert report["classification"] == classification
    assert report["filecanary"] is None
    assert report["storage_setup"] is None
    assert report["health"] is None
    assert len(report["storage_remount_attempts"]) == 2
    assert ser.writes == [
        "rp2040 ping\n",
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage remount\n",
    ]


def test_existing_data_ready_but_degraded_retained_store_polls_then_fails(
    monkeypatch,
):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_mount_line(),
            degraded_ready_storage_line(),
            degraded_ready_storage_line(),
            degraded_ready_storage_line(),
            storage_setup_line(),
            health_line(),
        ]
    )
    install_fake_serial(monkeypatch, ser)

    report = boot_accept.run_acceptance(
        port="COM12",
        baud=115200,
        timeout=1.0,
        scenario="existing-data",
        mount_poll_attempts=2,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is False
    assert report["retained_worker_quiesce_acquired"] is True
    assert report["storage_file_gate_ready"] is True
    assert report["retained_store_gate_ready"] is False
    assert report["classification"] == "existing_data_policy_not_reported"
    assert ser.writes.count("storage status\n") == 4


def test_existing_data_fallback_rejects_degraded_retained_health(monkeypatch):
    degraded_fallback = degraded_existing_data_fallback_line()
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_mount_line(),
            degraded_fallback,
            storage_setup_line(),
            health_line(),
        ]
    )
    install_fake_serial(monkeypatch, ser)

    report = boot_accept.run_acceptance(
        port="COM12",
        baud=115200,
        timeout=1.0,
        scenario="existing-data",
        mount_poll_attempts=0,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is False
    assert report["classification"] == "existing_data_policy_not_reported"
    assert report["scenario_prerequisite"]["satisfied"] is False
    assert boot_accept.retained_store_health_clean(json.loads(degraded_fallback)) is False


def test_boot_remount_timeout_exceeds_firmware_deadline(monkeypatch):
    calls = []

    def fake_send(_ser, command, timeout):
        calls.append((command, timeout))
        return {"ok": True, "cmd": command}

    monkeypatch.setattr(boot_accept, "send_console_command", fake_send)
    boot_accept.send_with_timeout(object(), "storage remount", 1.0)

    assert calls == [("storage remount", 75.0)]


def test_rp2040_unavailable_requires_complete_fail_closed_fallback(monkeypatch):
    ser = FakeSerial(
        [rp2040_timeout_line(), bridge_unavailable_storage_line(), health_line()]
    )
    install_fake_serial(monkeypatch, ser)

    report = boot_accept.run_acceptance(
        port="COM12",
        baud=115200,
        timeout=1.0,
        scenario="rp2040-unavailable",
        mount_poll_attempts=0,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["classification"] == "bridge_unavailable_fallback"
    assert report["scenario_prerequisite"]["satisfied"] is True
    assert report["storage_file_gate_ready"] is False
    assert report["retained_store_gate_ready"] is False


def test_rp2040_unavailable_rejects_protocol_pending_with_live_packet_backend():
    storage = json.loads(bridge_unavailable_storage_line(packets_backend="sd"))

    assert boot_accept.bridge_unavailable_fallback_ready(storage) is False
    passed, classification = boot_accept.boot_prepare_passed(
        "rp2040-unavailable",
        storage_after=storage,
        storage_setup=None,
        filecanary=None,
        health={"ok": True},
        public_rf_tx=False,
        formats_sd=False,
    )
    assert passed is False
    assert classification == "bridge_unavailable_not_reported"


def test_correct_structure_waits_through_bridge_resync_states(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_mount_line("protocol_pending"),
            protocol_pending_storage_line("protocol_pending"),
            protocol_pending_storage_line("rp2040_unavailable"),
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
        mount_poll_attempts=5,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["classification"] == "ready_sd_file_gate"
    assert ser.writes == [
        "rp2040 ping\n",
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage status\n",
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


def test_unformatted_ready_card_does_not_satisfy_rejection_evidence(monkeypatch):
    ser = FakeSerial(
        [
            rp2040_ping_line(),
            ready_storage_line(),
            storage_mount_line("ready"),
            ready_storage_line(),
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

    assert report["ok"] is False
    assert report["classification"] == "unformatted_policy_not_reported"
    assert report["formats_sd"] is False
    assert report["format_command_sent"] is False
    assert report["format_confirmed"] is False


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
