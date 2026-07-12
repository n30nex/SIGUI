import json

from scripts import sd_retained_history_acceptance_d1l as retained_accept
import pytest


READY_STORAGE = (
    '{"schema":1,"ok":true,"cmd":"storage status",'
    '"manager":{"running":true,"state":"READY_SD"},'
    '"sd":{"state":"ready","filesystem":"fat32",'
    '"present":true,"mounted":true,"data_root_ready":true,'
    '"rp2040_protocol_supported":true,"file_ops":true,"atomic_rename":true,'
    '"file_line_max":512,"file_chunk_max":192,"path_max":96,'
    '"status_stale":false,"presence_stale":false,"refresh_failures":0},'
    '"data_enabled":true,"data_backend":"mixed","message_store_backend":"sd",'
    '"dm_store_backend":"sd","route_store_backend":"sd","packet_log_backend":"sd",'
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
    '"packets":{"nvs_mirror_last_error":"ESP_OK"}}},'
    '"stores":{"messages":"sd","dm":"sd","routes":"sd","packets":"sd"}}\n'
)


class FakeSerial:
    instances = []

    def __init__(self, responses):
        self.responses = [line.encode("utf-8") for line in responses]
        self.writes = []
        self.reset_count = 0
        FakeSerial.instances.append(self)

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


def health_line(boot_nonce: int | None = 2) -> str:
    nonce = f',"boot_nonce":{boot_nonce}' if boot_nonce is not None else ""
    return f'{{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true{nonce}}}\n'


CANARY_SEQUENCES = {"public": 101, "dm": 102, "route": 103, "packet": 104}


def retained_canary_line(token: str, fingerprint: str) -> str:
    return json.dumps(
        {
            "schema": 1,
            "ok": True,
            "cmd": "storage retained-canary",
            "token": token,
            "fingerprint": fingerprint,
            **{f"{name}_seq": value for name, value in CANARY_SEQUENCES.items()},
            "backends": {name: "sd" for name in ("messages", "dm", "routes", "packets")},
            "public_rf_tx": False,
            "formats_sd": False,
        }
    ) + "\n"


def retained_readback_lines(token: str, fingerprint: str) -> list[str]:
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
                "entries": [
                    {
                        "seq": CANARY_SEQUENCES["public"],
                        "direction": "tx",
                        "author": "SD Canary",
                        "text": text,
                        "delivered": True,
                    }
                ],
            }
        ) + "\n",
        json.dumps(
            {
                "schema": 1,
                "ok": True,
                "cmd": "messages dm",
                "count": 1,
                "filtered": True,
                "fingerprint": fingerprint,
                "page_count": 1,
                "total_matches": 1,
                "thread_count": 1,
                "entries": [
                    {
                        "seq": CANARY_SEQUENCES["dm"],
                        "fingerprint": fingerprint,
                        "alias": "SD Canary",
                        "direction": "tx",
                        "text": text,
                        "delivered": True,
                        "acked": True,
                        "ack_hash": retained_accept.retained_canary_hash(token),
                    }
                ],
            }
        ) + "\n",
        json.dumps(
            {
                "schema": 1,
                "ok": True,
                "cmd": "routes trace",
                "fingerprint": fingerprint,
                "route_count": 1,
                "entries": [
                    {
                        "seq": CANARY_SEQUENCES["route"],
                        "target": fingerprint,
                        "label": "SD Canary",
                        "kind": "sd_canary",
                        "route": "local",
                        "direction": "tx",
                        "payload_len": len(text),
                    }
                ],
            }
        ) + "\n",
        json.dumps(
            {
                "schema": 1,
                "ok": True,
                "cmd": "packets search",
                "count": 1,
                "filter": {"direction": "any", "kind": "any", "search": token},
                "entries": [
                    {
                        "seq": CANARY_SEQUENCES["packet"],
                        "direction": "tx",
                        "kind": "sd_canary",
                        "payload_len": len(text),
                        "note": f"sd-canary {token}",
                    }
                ],
            }
        ) + "\n",
    ]


def retained_readback_results(token: str, fingerprint: str) -> dict[str, dict]:
    return {
        command: json.loads(line)
        for command, line in zip(
            retained_accept.retained_readback_commands(token),
            retained_readback_lines(token, fingerprint),
        )
    }


def test_dry_run_is_serial_only_and_has_no_format_or_public_rf():
    report = retained_accept.dry_run_report("sdToken1")

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert "storage retained-canary sdToken1" in report["commands"]
    assert "reboot" in report["commands"]
    expected_readbacks = retained_accept.retained_readback_commands("sdToken1")
    canary_index = report["commands"].index("storage retained-canary sdToken1")
    reboot_index = report["commands"].index("reboot")
    assert report["commands"][canary_index + 1 : reboot_index] == [
        *expected_readbacks,
        "storage status",
        "health",
    ]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("setup confirm" in command for command in report["commands"])
    assert not any("COM11" in command or "COM29" in command for command in report["commands"])


def test_fingerprint_for_token_is_stable_hex():
    first = retained_accept.fingerprint_for_token("sdToken1")
    second = retained_accept.fingerprint_for_token("sdToken1")

    assert first == second
    assert len(first) == 16
    assert all(ch in "0123456789ABCDEF" for ch in first)


def test_readbacks_reject_empty_results_that_only_echo_search_and_fingerprint():
    token = "sdToken1"
    fingerprint = retained_accept.fingerprint_for_token(token)
    canary = json.loads(retained_canary_line(token, fingerprint))
    results = {
        f"messages public search {token}": {
            "ok": True,
            "cmd": "messages public",
            "filtered": True,
            "search": token,
            "count": 1,
            "page_count": 0,
            "total_matches": 0,
            "entries": [],
        },
        f"messages dm {fingerprint}": {
            "ok": True,
            "cmd": "messages dm",
            "filtered": True,
            "fingerprint": fingerprint,
            "count": 1,
            "page_count": 0,
            "total_matches": 0,
            "thread_count": 0,
            "entries": [],
        },
        f"routes trace {fingerprint}": {
            "ok": True,
            "cmd": "routes trace",
            "fingerprint": fingerprint,
            "route_count": 0,
            "entries": [],
        },
        f"packets search {token}": {
            "ok": True,
            "cmd": "packets search",
            "count": 1,
            "filter": {"direction": "any", "kind": "any", "search": token},
            "entries": [],
        },
    }

    assert retained_accept.readbacks_pass(results, token, fingerprint, canary) is False


def test_readbacks_reject_wrong_sequence_and_non_sd_canary_metadata():
    token = "sdToken1"
    fingerprint = retained_accept.fingerprint_for_token(token)
    canary = json.loads(retained_canary_line(token, fingerprint))
    results = retained_readback_results(token, fingerprint)
    results[f"packets search {token}"]["entries"][0]["seq"] += 1

    assert retained_accept.readbacks_pass(results, token, fingerprint, canary) is False

    results = retained_readback_results(token, fingerprint)
    canary["backends"]["routes"] = "nvs"
    assert retained_accept.readbacks_pass(results, token, fingerprint, canary) is False


def test_retained_canary_gets_targeted_longer_command_timeout(monkeypatch):
    calls = []

    def fake_send(_ser, command, timeout):
        calls.append((command, timeout))
        return {"ok": True, "cmd": command}

    monkeypatch.setattr(retained_accept, "send_console_command", fake_send)
    retained_accept.run_commands(
        object(),
        [
            "storage filecanary",
            "storage retained-canary sdToken1",
            "messages public search sdToken1",
            "reboot",
            "health",
        ],
        5.0,
    )
    retained_accept.run_command(object(), "reboot", 25.0)

    assert calls == [
        ("storage filecanary", 120.0),
        ("storage retained-canary sdToken1", 180.0),
        ("messages public search sdToken1", 5.0),
        ("reboot", 20.0),
        ("health", 5.0),
        ("reboot", 25.0),
    ]


def test_command_sequence_stops_after_timeout_without_queueing_more(monkeypatch):
    calls = []

    def fake_send(_ser, command, timeout):
        calls.append((command, timeout))
        return {"schema": 1, "ok": False, "cmd": command, "code": "TIMEOUT"}

    monkeypatch.setattr(retained_accept, "send_console_command", fake_send)
    results = retained_accept.run_commands(
        object(),
        ["storage filecanary", "storage retained-canary token", "health"],
        5.0,
    )

    assert calls == [("storage filecanary", 120.0)]
    assert [row["code"] for row in results] == [
        "TIMEOUT",
        "SKIPPED_AFTER_TIMEOUT",
        "SKIPPED_AFTER_TIMEOUT",
    ]
    assert retained_accept.sequence_completed(results) is False


def test_ignored_boot_help_marks_unexpected_console_restart():
    assert retained_accept.unexpected_console_restart(
        [{"code": "TIMEOUT", "ignored_json": [{"cmd": "help", "ok": True}]}]
    ) is True
    assert retained_accept.unexpected_console_restart(
        [{"ok": True, "cmd": "storage retained-canary"}]
    ) is False
    assert retained_accept.unexpected_console_restart(
        [
            {
                "ok": True,
                "cmd": "storage status",
                "ignored_boot_help_seen": True,
                "ignored_json": [{"cmd": "noise-5", "ok": True}],
            }
        ]
    ) is True


def test_command_sequence_stops_after_ignored_boot_help(monkeypatch):
    calls = []

    def fake_run_command(_ser, command, _timeout):
        calls.append(command)
        return {
            "schema": 1,
            "ok": True,
            "cmd": command,
            "ignored_json_count": 1,
            "ignored_json": [{"cmd": "help", "ok": True}],
        }

    monkeypatch.setattr(retained_accept, "run_command", fake_run_command)
    results = retained_accept.run_commands(
        object(),
        ["storage filecanary", "storage retained-canary token", "health"],
        5.0,
    )

    assert calls == ["storage filecanary"]
    assert [row.get("code") for row in results] == [
        None,
        "SKIPPED_AFTER_UNEXPECTED_RESTART",
        "SKIPPED_AFTER_UNEXPECTED_RESTART",
    ]
    assert retained_accept.unexpected_console_restart(results) is True
    assert retained_accept.sequence_completed(results) is False


def test_command_sequence_stops_when_boot_help_falls_out_of_ignored_tail():
    responses = ['{"schema":1,"ok":true,"cmd":"help"}\n']
    responses.extend(
        f'{{"schema":1,"ok":true,"cmd":"noise-{index}"}}\n'
        for index in range(6)
    )
    responses.extend(
        [
            '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
            '{"schema":1,"ok":true,"cmd":"health"}\n',
        ]
    )
    ser = FakeSerial(responses)

    results = retained_accept.run_commands(
        ser, ["storage filecanary", "health"], 1.0
    )

    assert ser.writes == ["storage filecanary\n"]
    assert results[0]["ignored_json_count"] == 7
    assert results[0]["ignored_boot_help_seen"] is True
    assert results[1]["code"] == "SKIPPED_AFTER_UNEXPECTED_RESTART"
    assert retained_accept.sequence_completed(results) is False


def test_preflight_boot_help_stops_before_filecanary_and_reboot(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"help"}\n',
            READY_STORAGE,
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        "sdToken1",
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=10.0,
        allow_unavailable=False,
    )

    assert ser.writes == ["storage status\n"]
    assert report["unexpected_restart_before_reboot"] is True
    assert report["pre_sequence_complete"] is False
    assert report["reboot_command_passed"] is False
    assert report["results"][0]["ignored_json"] == [
        {"cmd": "help", "ok": True}
    ]
    assert report["results"][1]["code"] == "SKIPPED_AFTER_UNEXPECTED_RESTART"
    assert report["ok"] is False


def test_allow_unavailable_cannot_pass_after_boot_help(monkeypatch):
    token = "sdToken1"
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status",'
            '"sd":{"state":"protocol_pending"}}\n',
            '{"schema":1,"ok":false,"cmd":"storage filecanary",'
            '"code":"ESP_ERR_NOT_SUPPORTED","step":"preflight"}\n',
            '{"schema":1,"ok":true,"cmd":"help"}\n',
            '{"schema":1,"ok":false,"cmd":"storage retained-canary",'
            '"code":"SD_RETAINED_HISTORY_NOT_READY"}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=True,
    )

    assert ser.writes == [
        "storage status\n",
        "storage filecanary\n",
        f"storage retained-canary {token}\n",
    ]
    assert report["retained_canary_unavailable_ok"] is True
    assert report["unexpected_restart_before_reboot"] is True
    assert report["pre_sequence_complete"] is False
    assert report["ok"] is False


def test_acceptance_does_not_reboot_after_timed_out_pre_sequence(monkeypatch):
    token = "sdToken1"
    ser = FakeSerial([READY_STORAGE])

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    calls = []

    def fake_run_command(_ser, command, _timeout):
        calls.append(command)
        if command != "storage filecanary":
            raise AssertionError(f"unexpected command after timeout: {command}")
        return {
            "schema": 1,
            "ok": False,
            "cmd": command,
            "code": "TIMEOUT",
        }

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept, "run_command", fake_run_command)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert calls == ["storage filecanary"]
    assert report["pre_sequence_complete"] is False
    assert report["reboot_command_passed"] is False
    assert report["reboot_proven"] is False
    assert report["ok"] is False


def test_preflight_timeout_stops_before_filecanary_and_reboot(monkeypatch):
    timeout_line = (
        '{"schema":1,"ok":false,"cmd":"storage status","code":"TIMEOUT"}\n'
    )
    ser = FakeSerial([timeout_line])

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        "sdToken1",
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=10.0,
        allow_unavailable=False,
    )

    assert ser.writes == ["storage status\n"]
    assert report["pre_sequence_complete"] is False
    assert report["reboot_command_passed"] is False
    assert report["results"][1]["code"] == "SKIPPED_AFTER_TIMEOUT"
    assert report["ok"] is False


def test_no_reboot_mode_does_not_send_health_after_presequence_timeout(monkeypatch):
    timeout_line = (
        '{"schema":1,"ok":false,"cmd":"storage status","code":"TIMEOUT"}\n'
    )
    ser = FakeSerial([timeout_line])

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        "sdToken1",
        include_reboot=False,
        reboot_settle_sec=0.0,
        mount_wait_sec=10.0,
        allow_unavailable=False,
    )

    assert ser.writes == ["storage status\n"]
    assert report["pre_sequence_complete"] is False
    assert report["ok"] is False


def test_post_reboot_storage_timeout_stops_before_readbacks(monkeypatch):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    pre = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fp),
        *retained_readback_lines(token, fp),
        READY_STORAGE,
        health_line(1),
    ]
    reboot = [
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,'
        '"route_flush":"ESP_OK"}\n'
    ]
    post = [
        '{"schema":1,"ok":false,"cmd":"storage status","code":"TIMEOUT"}\n'
    ]
    serials = [FakeSerial(pre), FakeSerial(reboot), FakeSerial(post)]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=10.0,
        allow_unavailable=False,
    )

    post_serial = FakeSerial.instances[-1]
    assert post_serial.writes == ["storage status\n"]
    assert report["post_reboot_storage_ready_poll_count"] == 1
    assert report["post_reboot_readbacks_ok"] is False
    assert report["reboot_proven"] is False
    assert report["ok"] is False


def test_allow_unavailable_accepts_pre_bridge_refusal(monkeypatch):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"}}\n',
            '{"schema":1,"ok":false,"cmd":"storage filecanary","code":"ESP_ERR_NOT_SUPPORTED","step":"preflight"}\n',
            '{"schema":1,"ok":false,"cmd":"storage retained-canary","code":"SD_RETAINED_HISTORY_NOT_READY"}\n',
            '{"schema":1,"ok":true,"cmd":"messages public","entries":[]}\n',
            f'{{"schema":1,"ok":true,"cmd":"messages dm","fingerprint":"{fp}","entries":[]}}\n',
            f'{{"schema":1,"ok":true,"cmd":"routes trace","fingerprint":"{fp}","entries":[]}}\n',
            '{"schema":1,"ok":true,"cmd":"packets search","entries":[]}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"}}\n',
            health_line(1),
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=True,
    )

    assert report["ok"] is True
    assert report["retained_canary_unavailable_ok"] is True
    assert report["filecanary_unavailable_ok"] is True
    assert "reboot" not in report["commands"]
    assert ser.writes[2] == "storage retained-canary sdToken1\n"


def test_acceptance_requires_pre_and_post_reboot_readbacks(monkeypatch):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    responses = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fp),
        *retained_readback_lines(token, fp),
        READY_STORAGE,
        health_line(1),
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n',
        READY_STORAGE,
        *retained_readback_lines(token, fp),
        health_line(2),
    ]
    serials = [FakeSerial(responses[:9]), FakeSerial(responses[9:10]), FakeSerial(responses[10:])]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert report["ok"] is True
    assert report["reboot_command_passed"] is True
    assert report["reboot_route_flush"] == "ESP_OK"
    assert report["reboot_proven"] is True
    assert report["pre_reboot_boot_nonce"] == 1
    assert report["post_reboot_boot_nonce"] == 2
    assert report["retained_canary_passed"] is True
    assert report["pre_reboot_readbacks_ok"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["storage_file_gate_ready_before"] is True
    assert report["storage_file_gate_ready_after"] is True
    assert report["retained_history_sd_ready_before"] is True
    assert report["retained_history_sd_ready_after_canary"] is True
    assert report["retained_history_sd_ready_after"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False


def test_acceptance_waits_for_post_reboot_autonomous_sd_mount(monkeypatch):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    transient = json.loads(READY_STORAGE)
    transient["manager"]["state"] = "MOUNT"
    transient["sd"].update(
        {
            "state": "mount_pending",
            "mounted": False,
            "data_root_ready": False,
            "file_ops": False,
            "atomic_rename": False,
        }
    )
    for field in (
        "message_store_backend",
        "dm_store_backend",
        "route_store_backend",
        "packet_log_backend",
    ):
        transient[field] = "nvs"
    transient_line = json.dumps(transient) + "\n"
    pre = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fp),
        *retained_readback_lines(token, fp),
        READY_STORAGE,
        health_line(1),
    ]
    post = [
        transient_line,
        READY_STORAGE,
        *retained_readback_lines(token, fp),
        health_line(2),
    ]
    serials = [
        FakeSerial(pre),
        FakeSerial(
            ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']
        ),
        FakeSerial(post),
    ]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=2.0,
        allow_unavailable=False,
    )

    assert report["ok"] is True
    assert report["post_reboot_storage_ready_waited"] is True
    assert report["post_reboot_storage_ready_poll_count"] == 2
    assert report["storage_after"]["manager"]["state"] == "READY_SD"
    reboot_index = report["commands"].index("reboot")
    assert report["commands"][reboot_index + 1 : reboot_index + 3] == [
        "storage status",
        "storage status",
    ]


def test_acceptance_reports_and_rejects_public_rf_flag_from_canary(monkeypatch):
    token = "sdToken1"
    fingerprint = retained_accept.fingerprint_for_token(token)
    canary = json.loads(retained_canary_line(token, fingerprint))
    canary["public_rf_tx"] = True
    pre = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        json.dumps(canary) + "\n",
        *retained_readback_lines(token, fingerprint),
        READY_STORAGE,
        health_line(1),
    ]
    post = [READY_STORAGE, *retained_readback_lines(token, fingerprint), health_line(2)]
    serials = [
        FakeSerial(pre),
        FakeSerial(['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']),
        FakeSerial(post),
    ]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert report["public_rf_tx"] is True
    assert report["formats_sd"] is False
    assert report["retained_canary_passed"] is False
    assert report["ok"] is False


def test_acceptance_rejects_mirror_failure_immediately_after_canary(monkeypatch):
    token = "sdToken1"
    fingerprint = retained_accept.fingerprint_for_token(token)
    mirror_failure_payload = json.loads(READY_STORAGE)
    mirror_failure_payload["retained_sd"]["backup_degraded"] = True
    mirror_failure_payload["retained_sd"]["stores"]["dm"][
        "nvs_mirror_last_error"
    ] = "ESP_ERR_NVS_NOT_ENOUGH_SPACE"
    mirror_failure_status = json.dumps(mirror_failure_payload) + "\n"
    pre = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fingerprint),
        *retained_readback_lines(token, fingerprint),
        mirror_failure_status,
        health_line(1),
    ]
    post = [
        READY_STORAGE,
        *retained_readback_lines(token, fingerprint),
        health_line(2),
    ]
    serials = [
        FakeSerial(pre),
        FakeSerial(
            ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']
        ),
        FakeSerial(post),
    ]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert report["retained_history_sd_ready_before"] is True
    assert report["retained_history_sd_ready_after_canary"] is False
    assert report["retained_history_sd_ready_after"] is True
    assert report["storage_after_canary"]["retained_sd"]["stores"]["dm"][
        "nvs_mirror_last_error"
    ] == "ESP_ERR_NVS_NOT_ENOUGH_SPACE"
    assert report["ok"] is False


def test_reboot_flush_failure_cannot_pass_retained_acceptance(monkeypatch):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    pre = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fp),
        *retained_readback_lines(token, fp),
        READY_STORAGE,
        health_line(1),
    ]
    reboot = [
        '{"schema":1,"ok":false,"cmd":"reboot",'
        '"code":"ESP_ERR_NVS_NOT_ENOUGH_SPACE","rebooting":false}\n'
    ]
    post = [
        READY_STORAGE,
        *retained_readback_lines(token, fp),
        health_line(2),
    ]
    pre_serial = FakeSerial(pre)
    reboot_serial = FakeSerial(reboot)
    post_serial = FakeSerial(post)
    serials = [pre_serial, reboot_serial, post_serial]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert report["reboot_command_passed"] is False
    assert report["reboot_proven"] is False
    assert report["post_reboot_readbacks_ok"] is False
    assert report["storage_file_gate_ready_after"] is False
    assert report["ok"] is False
    reboot_result = next(row for row in report["results"] if row.get("cmd") == "reboot")
    assert reboot_result["code"] == "ESP_ERR_NVS_NOT_ENOUGH_SPACE"
    assert post_serial.writes == []
    assert "storage status" not in report["commands"][report["commands"].index("reboot") + 1 :]


@pytest.mark.parametrize("post_nonce", [1, None])
def test_successful_reboot_requires_changed_valid_nonce(monkeypatch, post_nonce):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    pre = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fp),
        *retained_readback_lines(token, fp),
        READY_STORAGE,
        health_line(1),
    ]
    reboot = ['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']
    post = [
        READY_STORAGE,
        *retained_readback_lines(token, fp),
        health_line(post_nonce),
    ]
    serials = [FakeSerial(pre), FakeSerial(reboot), FakeSerial(post)]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert report["reboot_command_passed"] is True
    assert report["reboot_proven"] is False
    assert report["post_reboot_readbacks_ok"] is False
    assert report["storage_file_gate_ready_after"] is False
    assert report["ok"] is False


def test_acceptance_rejects_nvs_readbacks_when_post_reboot_sd_is_missing(monkeypatch):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    no_card_storage = (
        '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"no_card",'
        '"present":false,"mounted":false,"data_root_ready":false,'
        '"rp2040_protocol_supported":true,"file_ops":false,"atomic_rename":false,'
        '"status_stale":false,"presence_stale":false,"refresh_failures":0},'
        '"data_enabled":true,"data_backend":"nvs","message_store_backend":"nvs",'
        '"dm_store_backend":"nvs","route_store_backend":"nvs","packet_log_backend":"nvs",'
        '"stores":{"messages":"nvs","dm":"nvs","routes":"nvs","packets":"nvs"}}\n'
    )
    responses = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fp),
        *retained_readback_lines(token, fp),
        READY_STORAGE,
        health_line(1),
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n',
        no_card_storage,
        *retained_readback_lines(token, fp),
        health_line(2),
    ]
    serials = [FakeSerial(responses[:9]), FakeSerial(responses[9:10]), FakeSerial(responses[10:])]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert report["pre_reboot_readbacks_ok"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["reboot_proven"] is True
    assert report["storage_file_gate_ready_before"] is True
    assert report["storage_file_gate_ready_after"] is False
    assert report["storage_after"]["sd"]["state"] == "no_card"
    assert report["ok"] is False


def test_acceptance_rejects_nvs_backends_even_when_post_reboot_file_gate_is_ready(monkeypatch):
    token = "sdToken1"
    fp = retained_accept.fingerprint_for_token(token)
    post_nvs_payload = json.loads(READY_STORAGE)
    post_nvs_payload["data_backend"] = "nvs"
    for field in (
        "message_store_backend",
        "dm_store_backend",
        "route_store_backend",
        "packet_log_backend",
    ):
        post_nvs_payload[field] = "nvs"
    post_nvs_payload["stores"] = {
        name: "nvs" for name in ("messages", "dm", "routes", "packets")
    }
    post_nvs = json.dumps(post_nvs_payload) + "\n"
    pre = [
        READY_STORAGE,
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        retained_canary_line(token, fp),
        *retained_readback_lines(token, fp),
        READY_STORAGE,
        health_line(1),
    ]
    post = [
        post_nvs,
        *retained_readback_lines(token, fp),
        health_line(2),
    ]
    serials = [
        FakeSerial(pre),
        FakeSerial(['{"schema":1,"ok":true,"cmd":"reboot","rebooting":true,"route_flush":"ESP_OK"}\n']),
        FakeSerial(post),
    ]

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: serials.pop(0)

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(retained_accept.time, "sleep", lambda _seconds: None)
    report = retained_accept.run_acceptance(
        "COM12",
        115200,
        1.0,
        token,
        include_reboot=True,
        reboot_settle_sec=0.0,
        mount_wait_sec=0.0,
        allow_unavailable=False,
    )

    assert report["storage_file_gate_ready_after"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["retained_history_sd_ready_after"] is False
    assert report["ok"] is False
