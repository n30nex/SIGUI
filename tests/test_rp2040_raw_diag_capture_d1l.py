from scripts import rp2040_raw_diag_capture_d1l as raw_diag


RAW_LINE = (
    "DESKOS_SD_DIAG pins=det7-cs13-sck10-mosi11-miso12-pwr18 hz=1000000 "
    "pin_sck=1 pin_mosi=1 pin_miso=1 pin_cs=1 selected_power=high selected_mode=dedicated "
    "hd_p=1 hd_e=0 hd_c0=1 hd_c8=0"
)


class FakeSerial:
    def __init__(self, responses):
        self.responses = [line.encode("utf-8") for line in responses]
        self.writes = []
        self.reset_count = 0
        self.timeout = 99.0

    def write(self, data):
        self.writes.append(data.decode("utf-8"))

    def flush(self):
        pass

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


def test_parse_fields_from_raw_diag_line():
    fields = raw_diag.parse_fields(RAW_LINE)

    assert fields["pins"] == "det7-cs13-sck10-mosi11-miso12-pwr18"
    assert fields["hz"] == "1000000"
    assert fields["hd_c8"] == "0"


def test_dry_run_is_non_rf_and_non_formatting():
    report = raw_diag.dry_run_report("COM12")

    assert report["ok"] is True
    assert report["commands"] == ["storage diag raw", "storage diag raw", "storage status", "rp2040 ping"]
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False


def test_capture_diag_waits_for_worker_then_reads_safe_console_commands(monkeypatch):
    ser = FakeSerial(
        [
            (
                '{"schema":1,"ok":true,"cmd":"storage diag raw",'
                '"response_truncated":false,'
                '"raw_line":"DESKOS_SD_DIAG pins=det7-cs13-sck10-mosi11-miso12-pwr18 '
                'selected_power=pending selected_mode=pending",'
                '"public_rf_tx":false,"formats_sd":false}\n'
            ),
            (
                '{"schema":1,"ok":true,"cmd":"storage diag raw",'
                f'"response_truncated":false,"raw_line":"{RAW_LINE}",'
                '"public_rf_tx":false,"formats_sd":false}\n'
            ),
            (
                '{"schema":1,"ok":true,"cmd":"storage status",'
                '"sd":{"state":"ready","filesystem":"fat32","file_ops":true},'
                '"public_rf_tx":false,"formats_sd":false}\n'
            ),
            (
                '{"schema":1,"ok":true,"cmd":"rp2040 ping",'
                '"file_line_max":512,"sd_touched":false,'
                '"public_rf_tx":false,"formats_sd":false}\n'
            ),
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(raw_diag.time, "sleep", lambda _seconds: None)

    report = raw_diag.capture_diag("COM12", 115200, 1.0)

    assert report["ok"] is True
    assert report["port"] == "COM12"
    assert report["initial_storage_diag"]["raw_line"].endswith("selected_power=pending selected_mode=pending")
    assert report["raw_line"] == RAW_LINE
    assert report["diag_attempt_count"] == 2
    assert report["diag_completed"] is True
    assert report["diag_deadline_exhausted"] is False
    assert report["fields"]["selected_mode"] == "dedicated"
    assert report["storage_status"]["sd"]["state"] == "ready"
    assert report["rp2040_ping"]["file_line_max"] == 512
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert ser.writes == ["storage diag raw\n", "storage diag raw\n", "storage status\n", "rp2040 ping\n"]


def test_capture_diag_retries_timeout_until_worker_result_is_complete(monkeypatch):
    pending = (
        '{"schema":1,"ok":true,"cmd":"storage diag raw",'
        '"response_truncated":false,'
        '"raw_line":"DESKOS_SD_DIAG selected_power=pending selected_mode=pending"}\n'
    )
    ser = FakeSerial(
        [
            pending,
            '{"schema":1,"ok":false,"cmd":"storage diag raw","code":"TIMEOUT"}\n',
            (
                '{"schema":1,"ok":true,"cmd":"storage diag raw",'
                f'"response_truncated":false,"raw_line":"{RAW_LINE}"}}\n'
            ),
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"ready"}}\n',
            '{"schema":1,"ok":true,"cmd":"rp2040 ping","protocol_supported":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(raw_diag.time, "sleep", lambda _seconds: None)

    report = raw_diag.capture_diag(
        "COM12",
        115200,
        1.0,
        diag_settle_seconds=0,
        diag_poll_seconds=0.01,
        diag_deadline_seconds=5.0,
    )

    assert report["ok"] is True
    assert report["diag_attempt_count"] == 3
    assert [attempt.get("code") for attempt in report["diag_attempts"]] == [None, "TIMEOUT", None]
    assert ser.writes[:3] == ["storage diag raw\n"] * 3


def test_capture_diag_fails_closed_when_pending_worker_exceeds_deadline(monkeypatch):
    pending = (
        '{"schema":1,"ok":true,"cmd":"storage diag raw",'
        '"response_truncated":false,'
        '"raw_line":"DESKOS_SD_DIAG selected_power=pending selected_mode=pending"}\n'
    )
    ser = FakeSerial(
        [
            pending,
            pending,
            '{"schema":1,"ok":true,"cmd":"storage status","sd":{"state":"ready"}}\n',
            '{"schema":1,"ok":true,"cmd":"rp2040 ping","protocol_supported":true}\n',
        ]
    )
    clock = {"now": 0.0}

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    def advance(seconds):
        clock["now"] += seconds

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(raw_diag.time, "monotonic", lambda: clock["now"])
    monkeypatch.setattr(raw_diag.time, "sleep", advance)

    report = raw_diag.capture_diag(
        "COM12",
        115200,
        0.1,
        diag_settle_seconds=0.4,
        diag_poll_seconds=0.6,
        diag_deadline_seconds=1.0,
    )

    assert report["ok"] is False
    assert report["diag_completed"] is False
    assert report["diag_deadline_exhausted"] is True
    assert report["diag_attempt_count"] == 2
    assert report["storage_diag"]["raw_line"].endswith("selected_mode=pending")
