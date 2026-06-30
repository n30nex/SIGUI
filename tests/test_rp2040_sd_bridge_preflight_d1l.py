from scripts import rp2040_sd_bridge_preflight_d1l as preflight


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


class CommandAwareSerial:
    def __init__(self, responses_by_command):
        self.responses_by_command = {
            command: [line.encode("utf-8") for line in responses]
            for command, responses in responses_by_command.items()
        }
        self.current_command = None
        self.writes = []
        self.reset_count = 0

    def write(self, data):
        command = data.decode("utf-8")
        self.writes.append(command)
        self.current_command = command.strip()

    def readline(self):
        responses = self.responses_by_command.get(self.current_command, [])
        return responses.pop(0) if responses else b""

    def reset_input_buffer(self):
        self.reset_count += 1

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


def protocol_pending_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"protocol_pending","rp2040_bridge_ready":true,'
        '"rp2040_protocol_supported":false,"last_error":"ESP_ERR_TIMEOUT"},'
        '"data_backend":"nvs","export_backend":"serial"}\n'
    )


def ready_storage_line() -> str:
    return (
        '{"schema":1,"ok":true,"cmd":"storage status",'
        '"sd":{"state":"ready","present":true,"mounted":true,'
        '"data_root_ready":true,"rp2040_bridge_ready":true,'
        '"rp2040_protocol_supported":true,"file_ops":true,'
        '"atomic_rename":true,"file_line_max":512,'
        '"file_chunk_max":192,"path_max":96},'
        '"data_backend":"mixed","export_backend":"sd_diagnostic_exports_ready"}\n'
    )


def test_preflight_dry_run_is_non_destructive():
    report = preflight.dry_run_report("artifact-dir")

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["copies_uf2"] is False
    assert report["commands"] == ["rp2040 status", "storage status", "health"]
    assert not any(command.startswith("mesh send public") for command in report["commands"])
    assert not any("FORMAT-DESKOS-SD" in command for command in report["commands"])


def test_preflight_classifies_protocol_pending_without_uf2_volume():
    report = preflight.classify_preflight(
        {"ok": True, "cmd": "rp2040 status", "uart_ready": True},
        {
            "ok": True,
            "cmd": "storage status",
            "sd": {
                "state": "protocol_pending",
                "rp2040_bridge_ready": True,
                "rp2040_protocol_supported": False,
                "last_error": "ESP_ERR_TIMEOUT",
            },
        },
        [],
        {"ok": True},
    )

    assert report["state"] == "rp2040_protocol_pending"
    assert report["next_action"] == "put_rp2040_in_uf2_bootloader"
    assert report["storage_file_gate_ready"] is False
    assert report["uf2_volume_available"] is False


def test_preflight_classifies_protocol_pending_with_uf2_volume():
    report = preflight.classify_preflight(
        {"ok": True, "cmd": "rp2040 status", "uart_ready": True},
        {
            "ok": True,
            "cmd": "storage status",
            "sd": {
                "state": "protocol_pending",
                "rp2040_bridge_ready": True,
                "rp2040_protocol_supported": False,
            },
        },
        [{"path": "R:\\"}],
        {"ok": True},
    )

    assert report["state"] == "rp2040_protocol_pending"
    assert report["next_action"] == "dry_run_then_copy_rp2040_uf2"
    assert report["uf2_volume_available"] is True


def test_preflight_classifies_ready_storage_gate():
    report = preflight.classify_preflight(
        {"ok": True, "cmd": "rp2040 status", "uart_ready": True},
        {
            "ok": True,
            "cmd": "storage status",
            "sd": {
                "state": "ready",
                "present": True,
                "mounted": True,
                "data_root_ready": True,
                "rp2040_protocol_supported": True,
                "file_ops": True,
                "atomic_rename": True,
                "file_line_max": 512,
                "file_chunk_max": 192,
                "path_max": 96,
            },
        },
        [],
        {"ok": True},
    )

    assert report["state"] == "sd_bridge_ready"
    assert report["next_action"] == "run_sd_file_and_export_acceptance"
    assert report["storage_file_gate_ready"] is True


def test_run_preflight_queries_only_safe_serial_commands(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"rp2040 status","uart_ready":true}\n',
            protocol_pending_storage_line(),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(preflight.time, "sleep", lambda _seconds: None)
    monkeypatch.setattr(preflight, "verify_optional_artifact", lambda *_args: {"ok": True})
    monkeypatch.setattr(preflight, "candidate_volumes", lambda: [])

    report = preflight.run_preflight(
        port="COM12",
        baud=115200,
        timeout=1.0,
        artifact_dir="artifact",
        expected_sha256=None,
    )

    assert report["ok"] is True
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["copies_uf2"] is False
    assert report["classification"]["state"] == "rp2040_protocol_pending"
    assert ser.reset_count == 1
    assert ser.writes == [
        "rp2040 status\n",
        "storage status\n",
        "health\n",
    ]


def test_run_preflight_tolerates_rp2040_status_timeout_when_storage_proves_bridge(monkeypatch):
    ser = CommandAwareSerial(
        {
            "storage status": [protocol_pending_storage_line()],
            "health": ['{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n'],
        }
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(preflight.time, "sleep", lambda _seconds: None)
    monkeypatch.setattr(preflight, "verify_optional_artifact", lambda *_args: {"ok": True})
    monkeypatch.setattr(preflight, "candidate_volumes", lambda: [])

    report = preflight.run_preflight(
        port="COM12",
        baud=115200,
        timeout=0.01,
        artifact_dir="artifact",
        expected_sha256=None,
    )

    assert report["ok"] is True
    assert report["rp2040_status_ok"] is False
    assert report["rp2040_status_optional_ok"] is True
    assert report["classification"]["rp2040_uart_ready"] is True
    assert report["classification"]["state"] == "rp2040_protocol_pending"


def test_run_preflight_reports_ready_for_sd_acceptance(monkeypatch):
    ser = FakeSerial(
        [
            '{"schema":1,"ok":true,"cmd":"rp2040 status","uart_ready":true}\n',
            ready_storage_line(),
            '{"schema":1,"ok":true,"cmd":"health","board_ready":true,"ui_ready":true}\n',
        ]
    )

    class FakeSerialModule:
        Serial = lambda self, **_kwargs: ser

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(preflight.time, "sleep", lambda _seconds: None)
    monkeypatch.setattr(preflight, "verify_optional_artifact", lambda *_args: {"ok": True})
    monkeypatch.setattr(preflight, "candidate_volumes", lambda: [])

    report = preflight.run_preflight(
        port="COM12",
        baud=115200,
        timeout=1.0,
        artifact_dir="artifact",
        expected_sha256=None,
    )

    assert report["ok"] is True
    assert report["ready_for_sd_acceptance"] is True
    assert report["classification"]["state"] == "sd_bridge_ready"
