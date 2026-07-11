from scripts import sd_export_canary_d1l


TOKEN = "exportTest1"


def ready_storage_status(*, export_backend="sd_diagnostic_exports_ready") -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"ready","filesystem":"fat32","present":true,"mounted":true,'
        '"data_root_ready":true,"rp2040_protocol_supported":true,'
        '"file_ops":true,"atomic_rename":true,'
        '"file_line_max":512,"file_chunk_max":192,"path_max":96,'
        '"status_stale":false,"presence_stale":false,"refresh_failures":0},'
        '"data_enabled":false,"data_backend":"nvs",'
        '"message_store_backend":"nvs","dm_store_backend":"nvs",'
        '"route_store_backend":"nvs","packet_log_backend":"nvs",'
        f'"export_backend":"{export_backend}",'
        '"stores":{"messages":"nvs","dm":"nvs","routes":"nvs","packets":"nvs",'
        f'"exports":"{export_backend}"}}}}\n'
    )


def export_canary_success(token: str = TOKEN) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage export-canary",'
        f'"token":"{token}",'
        f'"path":"exports/diagnostics/export-canary-{token}.json",'
        f'"tmp_path":"exports/diagnostics/export-canary-{token}.tmp",'
        '"bytes":96,"write_tmp":true,"read_tmp":true,'
        '"rename_replace":true,"stat_final":true,"read_final":true,'
        '"public_rf_tx":false,"formats_sd":false,'
        '"export_backend":"sd_diagnostic_exports_ready"}\n'
    )


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


def test_sd_export_canary_dry_run_is_serial_only():
    report = sd_export_canary_d1l.dry_run_report(TOKEN)

    assert report["ok"] is True
    assert report["token"] == TOKEN
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["commands"] == [
        "storage status",
        f"storage export-canary {TOKEN}",
        "storage status",
        "health",
    ]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("setup confirm" in command for command in report["commands"])


def test_sd_export_canary_allows_pre_flash_unavailable_state(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"export_backend":"serial"}\n',
            '{"schema":1,"ok":false,"cmd":"storage export-canary","code":"ESP_ERR_NOT_SUPPORTED","step":"preflight","public_rf_tx":false,"formats_sd":false}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"export_backend":"serial"}\n',
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_export_canary_d1l.run_canary("COM12", 115200, 1.0, TOKEN, allow_unavailable=True)

    assert report["ok"] is True
    assert report["canary_passed"] is False
    assert report["canary_unavailable_ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert ser.reset_count == 1
    assert ser.writes == [
        "storage status\n",
        f"storage export-canary {TOKEN}\n",
        "storage status\n",
        "health\n",
    ]


def test_sd_export_canary_success_does_not_require_retained_history_sd_backends(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_status(),
            export_canary_success(),
            ready_storage_status(),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_export_canary_d1l.run_canary("COM12", 115200, 1.0, TOKEN, allow_unavailable=False)

    assert report["ok"] is True
    assert report["canary_passed"] is True
    assert report["canary_unavailable_ok"] is False
    assert report["storage_file_gate_ready_before"] is True
    assert report["storage_file_gate_ready_after"] is True
    assert report["export_backend_ready_before"] is True
    assert report["export_backend_ready_after"] is True
    assert report["storage_before"]["message_store_backend"] == "nvs"
    assert report["storage_before"]["packet_log_backend"] == "nvs"


def test_sd_export_canary_success_requires_export_backend_ready(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_status(export_backend="serial"),
            export_canary_success(),
            ready_storage_status(export_backend="serial"),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_export_canary_d1l.run_canary("COM12", 115200, 1.0, TOKEN, allow_unavailable=False)

    assert report["ok"] is False
    assert report["canary_passed"] is True
    assert report["export_backend_ready_before"] is False
    assert report["export_backend_ready_after"] is False
