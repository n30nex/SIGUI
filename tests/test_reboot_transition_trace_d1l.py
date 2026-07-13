import json

import pytest

from scripts import reboot_transition_trace_d1l as trace


COMMIT = "cae7a3a3a9f496be45c68646bca0571b2f87341d"


class FakeSerial:
    def __init__(self, *, reset_reason="SW"):
        self.port = None
        self.timeout = 0.1
        self.dtr = True
        self.rts = True
        self.is_open = False
        self.rebooted = False
        self.reset_reason = reset_reason
        self.pending = []
        self.writes = []

    def open(self):
        self.is_open = True

    def close(self):
        self.is_open = False

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()

    def reset_input_buffer(self):
        self.pending.clear()

    def flush(self):
        pass

    def write(self, payload):
        command = payload.decode("ascii").strip()
        self.writes.append(command)
        if command == "version":
            result = {
                "schema": 1,
                "ok": True,
                "cmd": "version",
                "build_commit": COMMIT,
            }
        elif command == "health":
            result = {
                "schema": 1,
                "ok": True,
                "cmd": "health",
                "boot_nonce": 200 if self.rebooted else 100,
                "reset_reason": self.reset_reason if self.rebooted else "SW",
            }
        elif command == "crashlog":
            entries = [
                {"seq": 10, "reset_reason": "SW", "crash_like": False}
            ]
            if self.rebooted:
                entries.append(
                    {
                        "seq": 11,
                        "reset_reason": self.reset_reason,
                        "crash_like": self.reset_reason != "SW",
                    }
                )
            result = {
                "schema": 1,
                "ok": True,
                "cmd": "crashlog",
                "total_written": 11 if self.rebooted else 10,
                "entries": entries,
            }
        elif command == "reboot":
            result = {
                "schema": 1,
                "ok": True,
                "cmd": "reboot",
                "rebooting": True,
                "storage_manager_quiesced": True,
                "retained_worker_quiesced": True,
                "rp2040_bridge_quiesced": True,
                "retained_flush": "ESP_OK",
                "route_flush": "ESP_OK",
            }
            self.rebooted = True
            self.pending.extend(
                [
                    b"ESP-ROM:esp32s3-20210327\r\n",
                    b"rst:0xc (SW_CPU_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n",
                    b'{"schema":1,"ok":true,"cmd":"help"}\r\n',
                ]
            )
        else:  # pragma: no cover - allowlist should make this unreachable
            raise AssertionError(command)
        self.pending.insert(0, (json.dumps(result) + "\n").encode("ascii"))

    def readline(self, _size):
        return self.pending.pop(0) if self.pending else b""


class FakeSerialModule:
    def __init__(self, *, reset_reason="SW"):
        self.instance = FakeSerial(reset_reason=reset_reason)
        self.open_count = 0

    def Serial(self, **_kwargs):
        self.open_count += 1
        return self.instance


def args(tmp_path):
    return trace.parse_args(
        [
            "--port",
            "COM12",
            "--out",
            str(tmp_path / "trace.json"),
            "--expected-firmware-commit",
            COMMIT,
            "--open-settle-sec",
            "0",
            "--post-boot-quiet-sec",
            "0",
        ]
    )


@pytest.mark.parametrize("number", ["8", "11", "29"])
def test_forbidden_ports_are_rejected_without_literal_defaults(number):
    with pytest.raises(ValueError, match="forbidden port"):
        trace.enforce_port_guard("COM" + number)


def test_trace_keeps_one_handle_open_and_proves_one_sw_reboot(tmp_path):
    serial_module = FakeSerialModule()

    report = trace.run_trace(args(tmp_path), serial_module=serial_module)

    assert report["ok"] is True
    assert report["classification"] == "single_sw_transition"
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["serial_handle_reused_across_reboot"] is True
    assert serial_module.open_count == 1
    assert serial_module.instance.writes == [
        "version",
        "health",
        "crashlog",
        "reboot",
        "version",
        "health",
        "crashlog",
    ]
    assert report["transition"]["marker_counts"] == {
        "rom_banner": 1,
        "reset_banner": 1,
        "boot_line": 1,
        "boot_help": 1,
    }
    assert report["checks"]["crashlog_transition"][
        "exactly_one_sw_non_crash"
    ] is True


def test_trace_fails_closed_on_wdt(tmp_path):
    report = trace.run_trace(
        args(tmp_path), serial_module=FakeSerialModule(reset_reason="WDT")
    )

    assert report["ok"] is False
    assert report["classification"] == "wdt_reset"
    assert report["checks"]["post_reset_reason_sw"] is False
    assert report["checks"]["crashlog_transition"]["ok"] is False


def test_raw_capture_is_bounded_and_marks_reset_lines():
    ticks = iter([0.0, 0.001, 0.002, 0.003, 0.004])
    recorder = trace.TraceRecorder(
        clock=lambda: next(ticks),
        now=lambda: "2026-07-13T00:00:00Z",
        max_raw_lines=1,
        max_raw_line_chars=64,
    )

    recorder.observe(
        b"rst:0x7 (TG0WDT_SYS_RESET),boot:0x8 " + b"x" * 100 + b"\n",
        "transition",
    )
    recorder.observe(b"second line is dropped\n", "transition")
    summary = recorder.summary()

    assert summary["raw_line_count"] == 1
    assert summary["raw_lines"][0]["truncated"] is True
    assert set(summary["raw_lines"][0]["tags"]) == {
        "reset_banner",
        "boot_line",
        "crash_marker",
    }
    assert summary["raw_lines_dropped"] == 1
    assert summary["raw_chars_dropped"] > 0
