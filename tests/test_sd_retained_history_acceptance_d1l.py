from scripts import sd_retained_history_acceptance_d1l as retained_accept


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
    assert not any("FORMAT-DESKOS-SD" in command for command in report["commands"])
    assert not any("COM11" in command or "COM29" in command for command in report["commands"])


def test_fingerprint_for_token_is_stable_hex():
    first = retained_accept.fingerprint_for_token("sdToken1")
    second = retained_accept.fingerprint_for_token("sdToken1")

    assert first == second
    assert len(first) == 16
    assert all(ch in "0123456789ABCDEF" for ch in first)


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
        '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"ready"}}\n',
        '{"schema":1,"ok":true,"cmd":"storage filecanary"}\n',
        f'{{"schema":1,"ok":true,"cmd":"storage retained-canary","token":"{token}","fingerprint":"{fp}"}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages public","entries":[{{"text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"messages dm","entries":[{{"fingerprint":"{fp}","text":"sd-retained-canary {token}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"routes trace","fingerprint":"{fp}","entries":[{{"target":"{fp}"}}]}}\n',
        f'{{"schema":1,"ok":true,"cmd":"packets search","entries":[{{"note":"sd-canary {token}"}}]}}\n',
        '{"schema":1,"ok":true,"cmd":"reboot","rebooting":true}\n',
        '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"ready"}}\n',
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
        allow_unavailable=False,
    )

    assert report["ok"] is True
    assert report["retained_canary_passed"] is True
    assert report["pre_reboot_readbacks_ok"] is True
    assert report["post_reboot_readbacks_ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
