import json

import pytest

from scripts import sd_reboot_remount_acceptance_d1l as remount_accept


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


def ready_storage_line(manager_state: str = "READY_SD") -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        f'"manager":{{"running":true,"state":"{manager_state}"}},'
        '"sd":{"state":"ready","filesystem":"fat32","present":true,"mounted":true,'
        '"data_root_ready":true,"rp2040_protocol_supported":true,'
        '"file_ops":true,"atomic_rename":true,"status_stale":false,'
        '"presence_stale":false,"refresh_failures":0,"file_line_max":512,'
        '"file_chunk_max":192,"path_max":96},"data_enabled":true,'
        '"data_backend":"mixed","message_store_backend":"sd",'
        '"dm_store_backend":"sd","route_store_backend":"sd",'
        '"packet_log_backend":"sd","stores":{"messages":"sd","dm":"sd",'
        '"routes":"sd","packets":"sd"},'
        '"retained_nvs":{"partition":"d1l_retained","marker_ready":true,'
        '"markers_complete":true,'
        '"anchor_ready":true,'
        '"sentinel_ready":true,'
        '"external_init_required":false,'
        '"initialized_this_boot":false,"ready":true,'
        '"init_error":"ESP_OK","migrated_keys":4,"migration_error":"ESP_OK"},'
        '"retained_sd":{"degraded":false,"backup_degraded":false,'
        '"stores":{"messages":{"nvs_mirror_last_error":"ESP_OK"},'
        '"dm":{"nvs_mirror_last_error":"ESP_OK"},'
        '"routes":{"nvs_mirror_last_error":"ESP_OK"},'
        '"packets":{"nvs_mirror_last_error":"ESP_OK"}}}}\n'
    )


def bridge_wait_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"manager":{"running":true,"state":"BRIDGE_WAIT"},'
        '"sd":{"state":"rp2040_unavailable","present":false,"mounted":false,'
        '"data_root_ready":false,"file_ops":false,"atomic_rename":false}}\n'
    )


def mount_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage remount",'
        '"sd":{"state":"ready","filesystem":"fat32","present":true,"mounted":true},'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def busy_remount_line(manager_state: str = "PING") -> str:
    return (
        '{"schema":1,"ok":false,"cmd":"storage remount","code":"ESP_ERR_INVALID_STATE",'
        f'"manager":{{"running":true,"state":"{manager_state}"}},'
        '"sd":{"state":"ready","present":true,"mounted":true,"data_root_ready":true},'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def filecanary_line() -> str:
    return '{"schema":1,"ok":true,"cmd":"storage filecanary","public_rf_tx":false,"formats_sd":false}\n'


CANARY_SEQUENCES = {"public": 101, "dm": 102, "route": 103, "packet": 104}


def retained_canary_line(token: str) -> str:
    fp = remount_accept.fingerprint_for_token(token)
    return json.dumps(
        {
            "schema": 1,
            "ok": True,
            "cmd": "storage retained-canary",
            "token": token,
            "fingerprint": fp,
            **{f"{name}_seq": value for name, value in CANARY_SEQUENCES.items()},
            "backends": {name: "sd" for name in ("messages", "dm", "routes", "packets")},
            "public_rf_tx": False,
            "formats_sd": False,
        }
    ) + "\n"


def map_tile_canary_line(token: str) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage map-tile-canary",'
        f'"token":"{token}","rename_replace":true,"read_final":true,'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def map_tile_check_line(token: str) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage map-tile-check",'
        f'"token":"{token}","stat_final":true,"read_final":true,'
        '"public_rf_tx":false,"formats_sd":false}\n'
    )


def readback_lines(token: str) -> list[str]:
    fp = remount_accept.fingerprint_for_token(token)
    text = f"sd-retained-canary {token}"
    return [
        json.dumps(
            {
                "schema": 1,
                "ok": True,
                "cmd": "messages public",
                "count": 1,
                "filtered": True,
                "search": token,
                "page_count": 1,
                "total_matches": 1,
                "entries": [{"seq": 101, "direction": "tx", "author": "SD Canary", "text": text, "delivered": True}],
            }
        ) + "\n",
        json.dumps(
            {
                "schema": 1,
                "ok": True,
                "cmd": "messages dm",
                "count": 1,
                "filtered": True,
                "fingerprint": fp,
                "page_count": 1,
                "total_matches": 1,
                "thread_count": 1,
                "entries": [{
                    "seq": 102,
                    "fingerprint": fp,
                    "alias": "SD Canary",
                    "direction": "tx",
                    "text": text,
                    "delivered": True,
                    "acked": True,
                    "ack_hash": remount_accept.retained_canary_hash(token),
                }],
            }
        ) + "\n",
        json.dumps(
            {
                "schema": 1,
                "ok": True,
                "cmd": "routes trace",
                "fingerprint": fp,
                "route_count": 1,
                "entries": [{
                    "seq": 103,
                    "target": fp,
                    "label": "SD Canary",
                    "kind": "sd_canary",
                    "route": "local",
                    "direction": "tx",
                    "payload_len": len(text),
                }],
            }
        ) + "\n",
        json.dumps(
            {
                "schema": 1,
                "ok": True,
                "cmd": "packets search",
                "count": 1,
                "filter": {"direction": "any", "kind": "any", "search": token},
                "entries": [{
                    "seq": 104,
                    "direction": "tx",
                    "kind": "sd_canary",
                    "payload_len": len(text),
                    "note": f"sd-canary {token}",
                }],
            }
        ) + "\n",
    ]


def health_line(boot_nonce: int | None = 2) -> str:
    nonce = f',"boot_nonce":{boot_nonce}' if boot_nonce is not None else ""
    return f'{{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true{nonce}}}\n'


def test_storage_ready_requires_fat32_filesystem():
    ready = json.loads(ready_storage_line())
    assert remount_accept.storage_ready(ready) is True
    ready["sd"]["filesystem"] = "exfat"
    assert remount_accept.storage_ready(ready) is False


def test_storage_ready_requires_manager_ready_sd_not_cached_sd_fields():
    cached_ready = json.loads(ready_storage_line("PING"))
    assert remount_accept.storage_ready(cached_ready) is False

    cached_ready["manager"]["state"] = "READY_SD"
    assert remount_accept.storage_ready(cached_ready) is True


def install_fake_serial(monkeypatch, serials):
    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())


def test_dry_run_is_serial_only_and_read_only_after_reboot():
    report = remount_accept.dry_run_report("remount1")

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert "storage map-tile-canary remount1" in report["commands"]
    assert "storage map-tile-check remount1" in report["commands"]
    assert "reboot" in report["commands"]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("setup confirm" in command for command in report["commands"])


def test_slow_sd_operations_get_bounded_long_timeouts(monkeypatch):
    calls = []

    def fake_send(_ser, command, timeout):
        calls.append((command, timeout))
        return {"ok": True, "cmd": command, "rebooting": command == "reboot"}

    monkeypatch.setattr(remount_accept, "send_console_command", fake_send)
    remount_accept.run_command(object(), "storage status", 5.0)
    remount_accept.run_command(object(), "storage remount", 5.0)
    remount_accept.run_command(object(), "storage filecanary", 5.0)
    remount_accept.run_command(object(), "storage retained-canary remount1", 5.0)
    remount_accept.run_command(object(), "storage map-tile-canary remount1", 5.0)
    remount_accept.run_command(object(), "reboot", 5.0)
    remount_accept.run_command(object(), "reboot", 25.0)

    assert calls == [
        ("storage status", 5.0),
        ("storage remount", 120.0),
        ("storage filecanary", 120.0),
        ("storage retained-canary remount1", 180.0),
        ("storage map-tile-canary remount1", 120.0),
        ("reboot", 20.0),
        ("reboot", 25.0),
    ]


def test_reboot_remount_requires_retained_readbacks_and_read_only_map_check(monkeypatch):
    token = "remount1"
    pre = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        filecanary_line(),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
        health_line(1),
    ]
    reboot = ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']
    post = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        *readback_lines(token),
        map_tile_check_line(token),
        health_line(),
    ]
    pre_serial = FakeSerial(pre)
    reboot_serial = FakeSerial(reboot)
    post_serial = FakeSerial(post)
    install_fake_serial(monkeypatch, [pre_serial, reboot_serial, post_serial])
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["reboot_command_passed"] is True
    assert report["reboot_route_flush"] == "ESP_OK"
    assert report["reboot_proven"] is True
    assert report["pre_reboot_boot_nonce"] == 1
    assert report["post_reboot_boot_nonce"] == 2
    assert report["pre_remount_ready"] is True
    assert report["post_remount_ready"] is True
    assert report["pre_remount_command_passed"] is True
    assert report["post_remount_command_passed"] is True
    assert report["retained_history_sd_ready_before"] is True
    assert report["retained_history_sd_ready_after"] is True
    assert report["filecanary_passed"] is True
    assert report["retained_canary_passed"] is True
    assert report["pre_reboot_readbacks_ok"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["pre_map_tile_canary_passed"] is True
    assert report["post_map_tile_canary_passed"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert "storage remount\n" in pre_serial.writes
    assert "storage remount\n" in post_serial.writes
    assert "storage map-tile-check remount1\n" in post_serial.writes
    assert "storage map-tile-check remount1" in report["commands"]


def test_failed_filecanary_cannot_pass_reboot_remount_acceptance(monkeypatch):
    token = "remount1"
    pre = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        (
            '{"schema":1,"ok":false,"cmd":"storage filecanary",'
            '"code":"ESP_FAIL","public_rf_tx":false,"formats_sd":false}\n'
        ),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
        health_line(1),
    ]
    reboot = [
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,'
        '"route_flush":"ESP_OK"}\n'
    ]
    post = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        *readback_lines(token),
        map_tile_check_line(token),
        health_line(2),
    ]
    install_fake_serial(
        monkeypatch,
        [FakeSerial(pre), FakeSerial(reboot), FakeSerial(post)],
    )
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["filecanary_passed"] is False
    assert report["retained_canary_passed"] is True
    assert report["pre_reboot_readbacks_ok"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["ok"] is False


def test_reboot_flush_failure_cannot_pass_remount_acceptance(monkeypatch):
    token = "remount1"
    pre = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        filecanary_line(),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
        health_line(1),
    ]
    reboot = [
        '{"schema":1,"ok":false,"cmd":"reboot",'
        '"code":"ESP_ERR_NVS_NOT_ENOUGH_SPACE","rebooting":false}\n'
    ]
    post = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        *readback_lines(token),
        map_tile_check_line(token),
        health_line(),
    ]
    pre_serial = FakeSerial(pre)
    reboot_serial = FakeSerial(reboot)
    post_serial = FakeSerial(post)
    install_fake_serial(monkeypatch, [pre_serial, reboot_serial, post_serial])
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["reboot_command_passed"] is False
    assert report["reboot_proven"] is False
    assert report["post_remount_ready"] is False
    assert report["post_reboot_readbacks_ok"] is False
    assert report["post_map_tile_canary_passed"] is False
    assert report["ok"] is False
    reboot_result = next(row for row in report["results"] if row.get("cmd") == "reboot")
    assert reboot_result["code"] == "ESP_ERR_NVS_NOT_ENOUGH_SPACE"
    assert post_serial.writes == []
    assert f"storage map-tile-check {token}" not in report["commands"]


@pytest.mark.parametrize("post_nonce", [1, None])
def test_successful_reboot_requires_changed_valid_nonce(monkeypatch, post_nonce):
    token = "remount1"
    pre = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        filecanary_line(),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
        health_line(1),
    ]
    reboot = ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']
    post = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        *readback_lines(token),
        map_tile_check_line(token),
        health_line(post_nonce),
    ]
    install_fake_serial(
        monkeypatch,
        [FakeSerial(pre), FakeSerial(reboot), FakeSerial(post)],
    )
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["reboot_command_passed"] is True
    assert report["reboot_proven"] is False
    assert report["post_remount_ready"] is False
    assert report["post_reboot_readbacks_ok"] is False
    assert report["ok"] is False


def test_reboot_remount_polls_transient_bridge_wait_until_ready(monkeypatch):
    token = "remount1"
    pre = [
        bridge_wait_storage_line(),
        mount_line(),
        bridge_wait_storage_line(),
        ready_storage_line(),
        filecanary_line(),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
        health_line(1),
    ]
    reboot = ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']
    post = [
        bridge_wait_storage_line(),
        mount_line(),
        bridge_wait_storage_line(),
        ready_storage_line(),
        *readback_lines(token),
        map_tile_check_line(token),
        health_line(),
    ]
    pre_serial = FakeSerial(pre)
    reboot_serial = FakeSerial(reboot)
    post_serial = FakeSerial(post)
    install_fake_serial(monkeypatch, [pre_serial, reboot_serial, post_serial])
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=2,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["pre_remount_ready"] is True
    assert report["post_remount_ready"] is True
    assert pre_serial.writes[:4] == [
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage status\n",
    ]
    assert post_serial.writes[:4] == [
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage status\n",
    ]


def test_mount_sequence_waits_after_busy_remount_even_when_cached_sd_is_ready(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_line("PING"),
            busy_remount_line("PING"),
            ready_storage_line("READY_SD"),
            ready_storage_line("READY_SD"),
        ]
    )
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    commands, results, storage_after = remount_accept.run_mount_sequence(
        ser,
        timeout=1.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert remount_accept.remount_manager_busy(results[1]) is True
    assert remount_accept.storage_ready(storage_after) is True
    assert commands == ["storage status", "storage remount", "storage status", "storage status"]
    assert ser.writes == [f"{command}\n" for command in commands]
    assert remount_accept.remount_transition_passed(results[1], storage_after) is True


def test_generic_remount_error_cannot_pass_with_cached_ready_status(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_line(),
            '{"schema":1,"ok":false,"cmd":"storage remount","code":"ESP_FAIL"}\n',
            ready_storage_line(),
        ]
    )
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    _commands, results, storage_after = remount_accept.run_mount_sequence(
        ser,
        timeout=1.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert remount_accept.storage_ready(storage_after) is True
    assert remount_accept.remount_manager_busy(results[1]) is False
    assert remount_accept.remount_transition_passed(results[1], storage_after) is False


def test_pre_mount_timeout_stops_before_remount_canaries_and_reboot(monkeypatch):
    ser = FakeSerial(
        ['{"schema":1,"ok":false,"cmd":"storage status","code":"TIMEOUT"}\n']
    )
    install_fake_serial(monkeypatch, [ser])

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        "remount1",
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert ser.writes == ["storage status\n"]
    assert report["commands"] == ["storage status"]
    assert report["pre_sequence_complete"] is False
    assert report["post_sequence_complete"] is False
    assert report["timed_out_command"] == "storage status"
    assert report["reboot_command_passed"] is False
    assert report["ok"] is False


@pytest.mark.parametrize("include_reboot", [True, False])
def test_pre_canary_timeout_stops_before_later_commands(monkeypatch, include_reboot):
    ser = FakeSerial(
        [
            ready_storage_line(),
            mount_line(),
            ready_storage_line(),
            '{"schema":1,"ok":false,"cmd":"storage filecanary","code":"TIMEOUT"}\n',
        ]
    )
    install_fake_serial(monkeypatch, [ser])

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        "remount1",
        include_reboot=include_reboot,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert ser.writes == [
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "storage filecanary\n",
    ]
    assert report["pre_sequence_complete"] is False
    assert report["post_sequence_complete"] is False
    assert report["timed_out_command"] == "storage filecanary"
    assert "storage retained-canary remount1" not in report["commands"]
    assert "health" not in report["commands"]
    assert "reboot" not in report["commands"]
    assert report["ok"] is False


def test_post_reboot_mount_timeout_stops_before_readbacks(monkeypatch):
    token = "remount1"
    pre = [
        ready_storage_line(),
        mount_line(),
        ready_storage_line(),
        filecanary_line(),
        retained_canary_line(token),
        map_tile_canary_line(token),
        *readback_lines(token),
        health_line(1),
    ]
    reboot = [
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n'
    ]
    post = [
        '{"schema":1,"ok":false,"cmd":"storage status","code":"TIMEOUT"}\n'
    ]
    post_serial = FakeSerial(post)
    install_fake_serial(
        monkeypatch,
        [FakeSerial(pre), FakeSerial(reboot), post_serial],
    )
    monkeypatch.setattr(remount_accept.time, "sleep", lambda _seconds: None)

    report = remount_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert post_serial.writes == ["storage status\n"]
    assert report["pre_sequence_complete"] is True
    assert report["post_sequence_complete"] is False
    assert report["timed_out_command"] == "storage status"
    assert f"messages public search {token}" not in report["commands"][
        report["commands"].index("reboot") + 1 :
    ]
    assert f"storage map-tile-check {token}" not in report["commands"]
    assert report["post_reboot_readbacks_ok"] is False
    assert report["ok"] is False
