from scripts import sd_map_tile_canary_d1l


TOKEN = "mapTest1"


def ready_storage_status(*, map_tile_backend="sd_map_tiles_ready") -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"ready","present":true,"mounted":true,'
        '"data_root_ready":true,"rp2040_protocol_supported":true,'
        '"file_ops":true,"atomic_rename":true,'
        '"file_line_max":512,"file_chunk_max":192,"path_max":96},'
        '"data_enabled":true,"data_backend":"mixed",'
        f'"map_tile_backend":"{map_tile_backend}",'
        '"stores":{"messages":"sd","dm":"sd","routes":"sd","packets":"sd",'
        f'"map_tiles":"{map_tile_backend}"'
        '}}\n'
    )


def map_tile_canary_success(token: str = TOKEN) -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage map-tile-canary",'
        f'"token":"{token}",'
        f'"path":"map/tiles/z12/x1/y2-{token}.tile",'
        f'"tmp_path":"map/tiles/z12/x1/y2-{token}.tmp",'
        '"z":12,"x":1,"y":2,"bytes":128,'
        '"write_tmp":true,"read_tmp":true,'
        '"rename_replace":true,"stat_final":true,"read_final":true,'
        '"public_rf_tx":false,"formats_sd":false,'
        '"map_tile_backend":"sd_map_tiles_ready"}\n'
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


def test_sd_map_tile_canary_dry_run_is_serial_only():
    report = sd_map_tile_canary_d1l.dry_run_report(TOKEN)

    assert report["ok"] is True
    assert report["token"] == TOKEN
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["commands"] == [
        "storage status",
        "storage remount",
        f"storage map-tile-canary {TOKEN}",
        "storage status",
        "health",
    ]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("setup confirm" in command for command in report["commands"])


def test_sd_map_tile_canary_allows_pre_flash_unavailable_state(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"map_tile_backend":"unavailable"}\n',
            '{"schema":1,"ok":true,"cmd":"storage remount","public_rf_tx":false,"formats_sd":false}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"map_tile_backend":"unavailable"}\n',
            '{"schema":1,"ok":false,"cmd":"storage map-tile-canary","code":"ESP_ERR_NOT_SUPPORTED","step":"preflight","public_rf_tx":false,"formats_sd":false}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"map_tile_backend":"unavailable"}\n',
            '{"schema":1,"ok":true,"cmd":"storage remount","public_rf_tx":false,"formats_sd":false}\n',
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"map_tile_backend":"unavailable"}\n',
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(sd_map_tile_canary_d1l.time, "sleep", lambda _seconds: None)
    report = sd_map_tile_canary_d1l.run_canary(
        "COM12",
        115200,
        1.0,
        TOKEN,
        allow_unavailable=True,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["canary_passed"] is False
    assert report["canary_unavailable_ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert ser.reset_count == 1
    assert ser.writes == [
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        f"storage map-tile-canary {TOKEN}\n",
        "storage status\n",
        "storage remount\n",
        "storage status\n",
        "health\n",
    ]


def test_sd_map_tile_canary_success_requires_map_tile_backend_ready(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_status(),
            map_tile_canary_success(),
            ready_storage_status(),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    report = sd_map_tile_canary_d1l.run_canary("COM12", 115200, 1.0, TOKEN, allow_unavailable=False)

    assert report["ok"] is True
    assert report["canary_passed"] is True
    assert report["canary_unavailable_ok"] is False
    assert report["storage_file_gate_ready_before"] is True
    assert report["storage_file_gate_ready_after"] is True
    assert report["map_tile_backend_ready_before"] is True
    assert report["map_tile_backend_ready_after"] is True


def test_sd_map_tile_canary_success_fails_when_backend_not_ready(monkeypatch):
    ser = FakeSerial(
        [
            ready_storage_status(map_tile_backend="unavailable"),
            '{"schema":1,"ok":true,"cmd":"storage remount","public_rf_tx":false,"formats_sd":false}\n',
            ready_storage_status(map_tile_backend="unavailable"),
            map_tile_canary_success(),
            ready_storage_status(map_tile_backend="unavailable"),
            '{"schema":1,"ok":true,"cmd":"storage remount","public_rf_tx":false,"formats_sd":false}\n',
            ready_storage_status(map_tile_backend="unavailable"),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(sd_map_tile_canary_d1l.time, "sleep", lambda _seconds: None)
    report = sd_map_tile_canary_d1l.run_canary(
        "COM12",
        115200,
        1.0,
        TOKEN,
        allow_unavailable=False,
        mount_poll_attempts=1,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is False
    assert report["canary_passed"] is True
    assert report["map_tile_backend_ready_before"] is False
    assert report["map_tile_backend_ready_after"] is False


def test_sd_map_tile_canary_polls_until_ready_before_canary(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"protocol_pending"},"map_tile_backend":"unavailable"}\n',
            '{"schema":1,"ok":true,"cmd":"storage remount","public_rf_tx":false,"formats_sd":false}\n',
            ready_storage_status(),
            map_tile_canary_success(),
            ready_storage_status(),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(sd_map_tile_canary_d1l.time, "sleep", lambda _seconds: None)
    report = sd_map_tile_canary_d1l.run_canary(
        "COM12",
        115200,
        1.0,
        TOKEN,
        allow_unavailable=False,
        mount_poll_attempts=2,
        mount_poll_interval_sec=0.0,
    )

    assert report["ok"] is True
    assert report["storage_file_gate_ready_before"] is True
    assert report["map_tile_backend_ready_before"] is True
    assert ser.writes[:3] == [
        "storage status\n",
        "storage remount\n",
        "storage status\n",
    ]
