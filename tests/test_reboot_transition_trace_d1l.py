import json

import pytest

from scripts import reboot_transition_trace_d1l as trace


COMMIT = "cae7a3a3a9f496be45c68646bca0571b2f87341d"
_DEFAULT_RESET_LINE = object()


class FakeSerial:
    def __init__(
        self,
        *,
        reset_reason="SW",
        raw_reset_line=_DEFAULT_RESET_LINE,
        extra_boot_lines=(),
    ):
        self.port = None
        self.timeout = 0.1
        self.dtr = True
        self.rts = True
        self.is_open = False
        self.rebooted = False
        self.reset_reason = reset_reason
        if raw_reset_line is _DEFAULT_RESET_LINE:
            raw_reset_line = (
                b"rst:0x7 (TG0WDT_SYS_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
                if reset_reason == "WDT"
                else b"rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
            )
        self.raw_reset_line = raw_reset_line
        self.extra_boot_lines = list(extra_boot_lines)
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
            boot_lines = [b"ESP-ROM:esp32s3-20210327\r\n"]
            if self.raw_reset_line is not None:
                boot_lines.append(self.raw_reset_line)
            boot_lines.extend(self.extra_boot_lines)
            boot_lines.append(b'{"schema":1,"ok":true,"cmd":"help"}\r\n')
            self.pending.extend(boot_lines)
        else:  # pragma: no cover - allowlist should make this unreachable
            raise AssertionError(command)
        self.pending.insert(0, (json.dumps(result) + "\n").encode("ascii"))

    def readline(self, _size):
        return self.pending.pop(0) if self.pending else b""


class FakeSerialModule:
    def __init__(
        self,
        *,
        reset_reason="SW",
        raw_reset_line=_DEFAULT_RESET_LINE,
        extra_boot_lines=(),
    ):
        self.instance = FakeSerial(
            reset_reason=reset_reason,
            raw_reset_line=raw_reset_line,
            extra_boot_lines=extra_boot_lines,
        )
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


@pytest.mark.parametrize("number", ["8", "11", "16", "29"])
def test_forbidden_ports_are_rejected_without_literal_defaults(number):
    with pytest.raises(ValueError, match="forbidden port"):
        trace.enforce_port_guard("COM" + number)


def test_trace_rejects_1200_baud_without_opening_serial(tmp_path):
    serial_module = FakeSerialModule()
    trace_args = args(tmp_path)
    trace_args.baud = 1200

    with pytest.raises(ValueError, match="--baud must be 115200"):
        trace.run_trace(trace_args, serial_module=serial_module)

    assert serial_module.open_count == 0


@pytest.mark.parametrize(
    "command",
    ["factory-reset-confirm", "format", "rp2040 uf2", "storage sd prepare"],
)
def test_trace_command_allowlist_rejects_mutating_or_rp2040_commands(command):
    with pytest.raises(ValueError, match="outside reboot trace allowlist"):
        trace.enforce_safe_command(command)


def test_hardware_trace_requires_exact_expected_commit_before_serial_open(tmp_path):
    serial_module = FakeSerialModule()
    trace_args = args(tmp_path)
    trace_args.expected_firmware_commit = None

    with pytest.raises(ValueError, match="required for hardware evidence"):
        trace.run_trace(trace_args, serial_module=serial_module)

    assert serial_module.open_count == 0


def test_trace_keeps_one_handle_open_and_proves_one_sw_reboot(tmp_path):
    serial_module = FakeSerialModule()

    report = trace.run_trace(args(tmp_path), serial_module=serial_module)

    assert report["ok"] is True
    assert report["classification"] == "single_sw_transition"
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["serial_handle_reused_across_reboot"] is True
    assert serial_module.open_count == 1
    assert serial_module.instance.dtr is False
    assert serial_module.instance.rts is False
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
    assert report["checks"]["raw_transition_complete"] is True
    assert report["checks"]["raw_reset_reason_sw"] is True
    assert report["transition"]["transition_marker_counts"][
        "sw_reset_banner"
    ] == 1


def test_trace_fails_closed_on_wdt(tmp_path):
    report = trace.run_trace(
        args(tmp_path), serial_module=FakeSerialModule(reset_reason="WDT")
    )

    assert report["ok"] is False
    assert report["classification"] == "wdt_reset"
    assert report["checks"]["post_reset_reason_sw"] is False
    assert report["checks"]["crashlog_transition"]["ok"] is False
    assert report["checks"]["raw_wdt_reset_seen"] is True


def test_wdt_classification_precedes_duplicate_boot_banner(tmp_path):
    report = trace.run_trace(
        args(tmp_path),
        serial_module=FakeSerialModule(
            reset_reason="WDT",
            extra_boot_lines=(
                b"rst:0x7 (TG0WDT_SYS_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n",
            ),
        ),
    )

    assert report["ok"] is False
    assert report["checks"]["multiple_boot_banners"] is True
    assert report["classification"] == "wdt_reset"


def test_trace_rejects_raw_poweron_banner_even_if_device_json_claims_sw(tmp_path):
    report = trace.run_trace(
        args(tmp_path),
        serial_module=FakeSerialModule(
            raw_reset_line=(
                b"rst:0x1 (POWERON_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
            )
        ),
    )

    assert report["ok"] is False
    assert report["classification"] == "raw_reset_banner_mismatch"
    assert report["checks"]["raw_reset_reason_sw"] is False
    assert report["transition"]["transition_marker_counts"][
        "non_sw_reset_banner"
    ] == 1


def test_trace_requires_one_raw_reset_banner(tmp_path):
    report = trace.run_trace(
        args(tmp_path), serial_module=FakeSerialModule(raw_reset_line=None)
    )

    assert report["ok"] is False
    assert report["classification"] == "raw_reset_banner_mismatch"
    assert report["checks"]["raw_reset_reason_sw"] is False
    assert report["transition"]["transition_marker_counts"]["reset_banner"] == 0


def test_dropped_crash_marker_is_counted_and_fails_closed(tmp_path):
    trace_args = args(tmp_path)
    trace_args.max_raw_lines = 1
    report = trace.run_trace(
        trace_args,
        serial_module=FakeSerialModule(
            extra_boot_lines=(b"late TG0WDT_SYS_RESET panic marker\r\n",)
        ),
    )

    assert report["ok"] is False
    assert report["classification"] == "raw_transition_incomplete"
    assert report["checks"]["raw_transition_complete"] is False
    assert report["checks"]["raw_crash_marker_seen"] is True
    assert report["transition"]["crash_markers"] == []
    assert report["transition"]["raw_lines_dropped_by_phase"]["transition"] > 0


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
