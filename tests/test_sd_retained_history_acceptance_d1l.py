from scripts import sd_retained_history_acceptance_d1l as retained_accept


READY_STORAGE = (
    '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"ready","filesystem":"fat32",'
    '"present":true,"mounted":true,"data_root_ready":true,'
    '"rp2040_protocol_supported":true,"file_ops":true,"atomic_rename":true,'
    '"file_line_max":512,"file_chunk_max":192,"path_max":96,'
    '"status_stale":false,"presence_stale":false,"refresh_failures":0},'
    '"data_enabled":true,"data_backend":"mixed","message_store_backend":"sd",'
    '"dm_store_backend":"sd","route_store_backend":"sd","packet_log_backend":"sd",'
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


def test_dry_run_is_serial_only_and_has_no_format_or_public_rf():
    report = retained_accept.dry_run_report("sdToken1")

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert "storage retained-canary sdToken1" in report["commands"]
    assert "reboot" in report["commands"]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("setup confirm" in command for command in report["commands"])
    assert not any("COM11" in command or "COM29" in command for command in report["commands"])


def test_fingerprint_for_token_is_stable_hex():
    first = retained_accept.fingerprint_for_token("sdToken1")
    second = retained_accept.fingerprint_for_token("sdToken1")

    assert first == second
    assert len(first) == 16
    assert all(ch in "0123456789ABCDEF" for ch in first)


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
        ],
        5.0,
    )

    assert calls == [
        ("storage filecanary", 5.0),
        ("storage retained-canary sdToken1", 20.0),
        ("messages public search sdToken1", 5.0),
    ]


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
        f'{{"schema":1,"ok":true,"cmd":"storage retained-canary","token":"{token}","fingerprint":"{fp}"}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages public","entries":[{{"text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages dm","entries":[{{"fingerprint":"{fp}","text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"routes trace","fingerprint":"{fp}","entries":[{{"target":"{fp}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"packets search","entries":[{{"note":"sd-canary {token}"}}]}}\n',
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true}\n',
        READY_STORAGE,
        f'{{"schema":1,"ok":true,"cmd":"messages public","entries":[{{"text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages dm","entries":[{{"fingerprint":"{fp}","text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"routes trace","fingerprint":"{fp}","entries":[{{"target":"{fp}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"packets search","entries":[{{"note":"sd-canary {token}"}}]}}\n',
        '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
    ]
    serials = [FakeSerial(responses[:7]), FakeSerial(responses[7:8]), FakeSerial(responses[8:])]

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
    assert report["retained_canary_passed"] is True
    assert report["pre_reboot_readbacks_ok"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["storage_file_gate_ready_before"] is True
    assert report["storage_file_gate_ready_after"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False


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
        f'{{"schema":1,"ok":true,"cmd":"storage retained-canary","token":"{token}","fingerprint":"{fp}"}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages public","entries":[{{"text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages dm","entries":[{{"fingerprint":"{fp}","text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"routes trace","fingerprint":"{fp}","entries":[{{"target":"{fp}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"packets search","entries":[{{"note":"sd-canary {token}"}}]}}\n',
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true}\n',
        no_card_storage,
        f'{{"schema":1,"ok":true,"cmd":"messages public","entries":[{{"text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages dm","entries":[{{"fingerprint":"{fp}","text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"routes trace","fingerprint":"{fp}","entries":[{{"target":"{fp}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"packets search","entries":[{{"note":"sd-canary {token}"}}]}}\n',
        '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
    ]
    serials = [FakeSerial(responses[:7]), FakeSerial(responses[7:8]), FakeSerial(responses[8:])]

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
    assert report["storage_file_gate_ready_before"] is True
    assert report["storage_file_gate_ready_after"] is False
    assert report["storage_after"]["sd"]["state"] == "no_card"
    assert report["ok"] is False
