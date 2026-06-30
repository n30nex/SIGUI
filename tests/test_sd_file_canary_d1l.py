from scripts import sd_file_canary_d1l


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


def test_sd_file_canary_dry_run_is_serial_only():
    report = sd_file_canary_d1l.dry_run_report()

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["commands"] == [
        "storage status",
        "storage filecanary",
        "storage status",
        "packets",
        "health",
    ]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("confirm" in command for command in report["commands"])


def test_sd_file_canary_allows_pre_flash_unavailable_state(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"data_backend":"nvs","packet_log_backend":"nvs"}\n',
            '{"schema":1,"ok":false,"cmd":"storage filecanary","code":"ESP_ERR_NOT_SUPPORTED","step":"preflight"}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"data_backend":"nvs","packet_log_backend":"nvs"}\n',
            '{"schema":1,"ok":true,"cmd":"packets","count":0,"persisted":true}\n',
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_file_canary_d1l.run_canary("COM12", 115200, 1.0, allow_unavailable=True)

    assert report["ok"] is True
    assert report["canary_passed"] is False
    assert report["canary_unavailable_ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert ser.reset_count == 1
    assert ser.writes == [
        "storage status\n",
        "storage filecanary\n",
        "storage status\n",
        "packets\n",
        "health\n",
    ]


def test_sd_file_canary_success_requires_canary_ok(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"ready","file_ops":true,"atomic_rename":true},"data_backend":"mixed","packet_log_backend":"sd"}\n',
            '{"schema":1,"ok":true,"cmd":"storage filecanary","rename_replace":true,"delete_final":true}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"ready","file_ops":true,"atomic_rename":true},"data_backend":"mixed","packet_log_backend":"sd"}\n',
            '{"schema":1,"ok":true,"cmd":"packets","count":1,"persisted":true}\n',
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_file_canary_d1l.run_canary("COM12", 115200, 1.0, allow_unavailable=False)

    assert report["ok"] is True
    assert report["canary_passed"] is True
    assert report["canary_unavailable_ok"] is False
