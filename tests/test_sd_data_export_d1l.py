from scripts import sd_data_export_d1l


TOKEN = "dataTest1"


def ready_storage_status(*, export_backend="sd_diagnostic_exports_ready") -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"ready","present":true,"mounted":true,'
        '"data_root_ready":true,"rp2040_protocol_supported":true,'
        '"file_ops":true,"atomic_rename":true,'
        '"file_line_max":512,"file_chunk_max":192,"path_max":96},'
        '"data_enabled":true,"data_backend":"mixed",'
        f'"export_backend":"{export_backend}",'
        '"stores":{'
        f'"exports":"{export_backend}","messages":"sd","dm":"sd",'
        '"routes":"sd","packets":"sd","map_tiles":"sd_map_tiles_ready"'
        '}}\n'
    )


def data_export_success(token: str = TOKEN) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage export-data",'
        '"kind":"data_export",'
        f'"token":"{token}",'
        f'"path":"exports/data/data-export-{token}.json",'
        f'"tmp_path":"exports/data/data-export-{token}.tmp",'
        '"bytes":1024,"chunks_written":6,"chunks_verified_tmp":6,'
        '"chunks_verified_final":6,"tmp_verified_bytes":1024,'
        '"final_verified_bytes":1024,"write_tmp":true,"read_tmp":true,'
        '"rename_replace":true,"stat_final":true,"read_final":true,'
        '"sampled":true,"private_identity_exported":false,'
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

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


def test_sd_data_export_dry_run_is_serial_only():
    report = sd_data_export_d1l.dry_run_report(TOKEN)

    assert report["ok"] is True
    assert report["token"] == TOKEN
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["commands"] == [
        "storage status",
        f"storage export-data {TOKEN}",
        "storage status",
        "health",
    ]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("setup confirm" in command for command in report["commands"])


def test_sd_data_export_allows_pre_flash_unavailable_state(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"export_backend":"serial"}\n',
            '{"schema":1,"ok":false,"cmd":"storage export-data","code":"ESP_ERR_NOT_SUPPORTED","step":"preflight","public_rf_tx":false,"formats_sd":false}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"export_backend":"serial"}\n',
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_data_export_d1l.run_export("COM12", 115200, 1.0, TOKEN, allow_unavailable=True)

    assert report["ok"] is True
    assert report["data_export_passed"] is False
    assert report["data_export_unavailable_ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert ser.reset_count == 1
    assert ser.writes == [
        "storage status\n",
        f"storage export-data {TOKEN}\n",
        "storage status\n",
        "health\n",
    ]


def test_sd_data_export_success_requires_multichunk_export(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_status(),
            data_export_success(),
            ready_storage_status(),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_data_export_d1l.run_export("COM12", 115200, 1.0, TOKEN, allow_unavailable=False)

    assert report["ok"] is True
    assert report["data_export_passed"] is True
    assert report["data_export_unavailable_ok"] is False
    assert report["storage_file_gate_ready_before"] is True
    assert report["storage_file_gate_ready_after"] is True
    assert report["export_backend_ready_before"] is True
    assert report["export_backend_ready_after"] is True


def test_sd_data_export_success_requires_export_backend_ready(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_status(export_backend="serial"),
            data_export_success(),
            ready_storage_status(export_backend="serial"),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_data_export_d1l.run_export("COM12", 115200, 1.0, TOKEN, allow_unavailable=False)

    assert report["ok"] is False
    assert report["data_export_passed"] is True
    assert report["export_backend_ready_before"] is False
    assert report["export_backend_ready_after"] is False
