import pytest

from scripts import soak_d1l


def base_health(uptime_ms=1000):
    return {
        "uptime_ms": uptime_ms,
        "heap_free": 100000,
        "heap_min_free": 99000,
        "psram_free": 200000,
        "psram_min_free": 199000,
        "current_task_stack_free_words": 1200,
        "ui_task_stack_free_words": 1300,
        "retained_task_stack_free_bytes": 5000,
        "lvgl_used_pct": 50,
    }


def sample(label, elapsed, health, mesh, packets=None, signal=None):
    return {
        "label": label,
        "elapsed_sec": elapsed,
        "results": [
            {
                "schema": 1,
                "ok": True,
                "cmd": "health",
                "uptime_ms": health["uptime_ms"],
                "heap_free": health["heap_free"],
                "heap_min_free": health["heap_min_free"],
                "psram_free": health["psram_free"],
                "psram_min_free": health["psram_min_free"],
                "current_task_stack_free_words": health["current_task_stack_free_words"],
                "ui_task_stack_free_words": health["ui_task_stack_free_words"],
                "retained_task_stack_free_bytes": health.get(
                    "retained_task_stack_free_bytes", 5000
                ),
                "lvgl_used_pct": health["lvgl_used_pct"],
                "board_ready": True,
                "ui_ready": True,
            },
            {
                "schema": 1,
                "ok": True,
                "cmd": "mesh status",
                "state": "ready",
                "identity_ready": True,
                "radio_ready": True,
                "rx_packets": mesh["rx_packets"],
                "tx_packets": mesh["tx_packets"],
            },
            {
                "schema": 1,
                "ok": True,
                "cmd": "signal",
                "sample_count": 0 if signal is None else signal["sample_count"],
            },
            {"schema": 1, "ok": True, "cmd": "messages unread"},
            {
                "schema": 1,
                "ok": True,
                "cmd": "packets",
                "total_written": 0 if packets is None else packets["total_written"],
            },
            {"schema": 1, "ok": True, "cmd": "crashlog"},
        ],
    }


def storage_status(
    *,
    state="ready",
    filesystem="fat32",
    protocol_supported=True,
    present=True,
    mounted=True,
    data_root_ready=True,
    file_ops=True,
    atomic_rename=True,
    status_stale=False,
    presence_stale=False,
    refresh_failures=0,
    data_enabled=True,
    data_backend="mixed",
    message_store_backend="nvs",
    dm_store_backend="nvs",
    packet_log_backend="sd",
    route_store_backend="nvs",
):
    return {
        "schema": 1,
        "ok": True,
        "cmd": soak_d1l.STORAGE_STATUS_COMMAND,
        "sd": {
            "state": state,
            "filesystem": filesystem,
            "interface": "rp2040",
            "rp2040_protocol_supported": protocol_supported,
            "present": present,
            "mounted": mounted,
            "data_root_ready": data_root_ready,
            "file_ops": file_ops,
            "atomic_rename": atomic_rename,
            "status_stale": status_stale,
            "presence_stale": presence_stale,
            "refresh_failures": refresh_failures,
            "file_line_max": 512 if file_ops else 0,
            "file_chunk_max": 192 if file_ops else 0,
            "path_max": 96 if file_ops else 0,
        },
        "data_enabled": data_enabled,
        "data_backend": data_backend,
        "message_store_backend": message_store_backend,
        "dm_store_backend": dm_store_backend,
        "packet_log_backend": packet_log_backend,
        "route_store_backend": route_store_backend,
        "map_tile_backend": "sd_pending_store_migration" if file_ops else "unavailable",
        "export_backend": "sd_diagnostic_exports_ready" if file_ops else "serial",
        "stores": {
            "settings": "nvs",
            "identity": "nvs",
            "messages": message_store_backend,
            "dm": dm_store_backend,
            "packets": packet_log_backend,
            "routes": route_store_backend,
            "contacts": "nvs",
            "read_state": "nvs",
            "crashlog": "nvs",
            "map_tiles": "sd_pending_store_migration" if file_ops else "unavailable",
            "exports": "sd_diagnostic_exports_ready" if file_ops else "serial",
        },
    }


def test_file_ops_ready_requires_fat32_filesystem():
    assert soak_d1l.file_ops_ready(storage_status()) is True
    assert soak_d1l.file_ops_ready(storage_status(filesystem="exfat")) is False


def test_dry_run_reports_soak_commands():
    report = soak_d1l.dry_run_report(
        duration_sec=60,
        sample_interval_sec=10,
        active_public_text=None,
        active_interval_sec=30,
        require_rx_delta=True,
        min_rx_delta=1,
        min_tx_delta=1,
        clear_crashlog_before_start=True,
        command_retries=1,
        retry_delay_sec=0.5,
        active_dm_fingerprint="0BF0A701D5AE2DB6",
        active_dm_text="test",
    )

    assert report["ok"] is True
    assert report["commands"] == soak_d1l.SOAK_COMMANDS
    assert report["active_public_text"] is None
    assert report["public_rf_tx"] is False
    assert report["dm_rf_tx"] is True
    assert report["active_command"] == "mesh send dm 0BF0A701D5AE2DB6 test"
    assert report["require_rx_delta"] is True
    assert report["clear_crashlog_before_start"] is True
    assert report["command_retries"] == 1


def test_dry_run_rejects_automated_public_tx():
    with pytest.raises(ValueError, match="automated Public TX is disabled"):
        soak_d1l.dry_run_report(
            duration_sec=60,
            sample_interval_sec=10,
            active_public_text="test",
            active_interval_sec=30,
            require_rx_delta=True,
            min_rx_delta=1,
            min_tx_delta=1,
            clear_crashlog_before_start=False,
            command_retries=1,
            retry_delay_sec=0.5,
        )


def test_dry_run_defaults_do_not_send_public_rf_or_touch_sd_format():
    report = soak_d1l.dry_run_report(
        duration_sec=60,
        sample_interval_sec=10,
        active_public_text=None,
        active_interval_sec=30,
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        clear_crashlog_before_start=False,
        command_retries=1,
        retry_delay_sec=0.5,
    )

    assert soak_d1l.SD_FILE_CANARY_COMMAND not in report["commands"]
    assert soak_d1l.STORAGE_STATUS_COMMAND not in report["commands"]
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False


def test_dry_run_sd_canary_is_serial_only_and_non_formatting():
    report = soak_d1l.dry_run_report(
        duration_sec=60,
        sample_interval_sec=10,
        active_public_text=None,
        active_interval_sec=30,
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        clear_crashlog_before_start=False,
        command_retries=1,
        retry_delay_sec=0.5,
        sample_storage=True,
        sd_file_canary=True,
        allow_sd_unavailable=True,
    )

    assert report["commands"] == [
        *soak_d1l.SOAK_COMMANDS,
        soak_d1l.STORAGE_STATUS_COMMAND,
        soak_d1l.SD_FILE_CANARY_COMMAND,
    ]
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["sample_storage"] is True
    assert report["sd_file_canary"] is True


def test_summarize_soak_tracks_deltas_and_watermarks():
    samples = [
        sample(
            "start",
            0,
            {
                "uptime_ms": 1000,
                "heap_free": 100000,
                "heap_min_free": 99000,
                "psram_free": 200000,
                "psram_min_free": 199000,
                "current_task_stack_free_words": 1200,
                "ui_task_stack_free_words": 1300,
                "lvgl_used_pct": 50,
            },
            {"rx_packets": 5, "tx_packets": 7},
            {"total_written": 20},
            {"sample_count": 3},
        ),
        sample(
            "final",
            60,
            {
                "uptime_ms": 61000,
                "heap_free": 99500,
                "heap_min_free": 98000,
                "psram_free": 199500,
                "psram_min_free": 198000,
                "current_task_stack_free_words": 1180,
                "ui_task_stack_free_words": 1280,
                "lvgl_used_pct": 54,
            },
            {"rx_packets": 8, "tx_packets": 9},
            {"total_written": 24},
            {"sample_count": 6},
        ),
    ]
    active_events = [
        {
            "elapsed_sec": 2,
            "command": "mesh send dm 0BF0A701D5AE2DB6 test",
            "fingerprint": "0BF0A701D5AE2DB6",
            "text": "test",
            "result": {"schema": 1, "ok": True, "cmd": "mesh send dm"},
        }
    ]

    summary = soak_d1l.summarize_soak(
        samples=samples,
        active_events=active_events,
        require_rx_delta=True,
        min_rx_delta=1,
        min_tx_delta=1,
    )

    assert summary["ok"] is True
    assert summary["mesh_rx_packet_delta"] == 3
    assert summary["mesh_tx_packet_delta"] == 2
    assert summary["packet_total_written_delta"] == 4
    assert summary["heap_free_delta"] == -500
    assert summary["current_task_stack_free_words_floor"] == 1180
    assert summary["retained_task_stack_free_bytes_floor"] == 5000
    assert summary["lvgl_used_pct_peak"] == 54
    assert summary["signal_sample_count_peak"] == 6
    assert summary["command_retry_count"] == 0


def test_summarize_soak_rejects_low_retained_worker_stack_margin():
    health = base_health()
    health["retained_task_stack_free_bytes"] = 2048
    samples = [
        sample("start", 0, health, {"rx_packets": 0, "tx_packets": 0}),
        sample("final", 1, health, {"rx_packets": 0, "tx_packets": 0}),
    ]

    summary = soak_d1l.summarize_soak(
        samples=samples,
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=0,
        min_tx_delta=0,
    )

    assert summary["ok"] is False
    assert "retained_task_stack_margin_below_4096_bytes" in summary[
        "threshold_failures"
    ]


def test_summarize_soak_rejects_missing_retained_worker_stack_metric():
    row = sample(
        "start", 0, base_health(), {"rx_packets": 0, "tx_packets": 0}
    )
    del row["results"][0]["retained_task_stack_free_bytes"]
    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=0,
        min_tx_delta=0,
    )

    assert summary["ok"] is False
    assert "retained_task_stack_watermark_missing" in summary[
        "threshold_failures"
    ]


def test_summarize_soak_detects_reboot_and_missing_rx_delta():
    samples = [
        sample(
            "start",
            0,
            {
                "uptime_ms": 5000,
                "heap_free": 100000,
                "heap_min_free": 99000,
                "psram_free": 200000,
                "psram_min_free": 199000,
                "current_task_stack_free_words": 1200,
                "ui_task_stack_free_words": 1300,
                "lvgl_used_pct": 50,
            },
            {"rx_packets": 5, "tx_packets": 7},
        ),
        sample(
            "final",
            60,
            {
                "uptime_ms": 1000,
                "heap_free": 99500,
                "heap_min_free": 98000,
                "psram_free": 199500,
                "psram_min_free": 198000,
                "current_task_stack_free_words": 1180,
                "ui_task_stack_free_words": 1280,
                "lvgl_used_pct": 54,
            },
            {"rx_packets": 5, "tx_packets": 7},
        ),
    ]

    summary = soak_d1l.summarize_soak(
        samples=samples,
        active_events=[],
        require_rx_delta=True,
        min_rx_delta=1,
        min_tx_delta=0,
    )

    assert summary["ok"] is False
    assert "uptime_reset_or_reboot_seen" in summary["threshold_failures"]
    assert "rx_delta_below_minimum" in summary["threshold_failures"]


def test_summarize_soak_detects_crash_like_reset_entries():
    row = sample(
        "start",
        0,
        {
            "uptime_ms": 1000,
            "heap_free": 100000,
            "heap_min_free": 99000,
            "psram_free": 200000,
            "psram_min_free": 199000,
            "current_task_stack_free_words": 1200,
            "ui_task_stack_free_words": 1300,
            "lvgl_used_pct": 50,
        },
        {"rx_packets": 5, "tx_packets": 7},
    )
    row["results"][-1] = {
        "schema": 1,
        "ok": True,
        "cmd": "crashlog",
        "count": 1,
        "total_written": 1,
        "entries": [{"seq": 1, "reset_reason": "PANIC", "crash_like": True}],
    }

    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
    )

    assert summary["ok"] is False
    assert summary["crashlog_crash_like_count"] == 1
    assert summary["crashlog_count_peak"] == 1
    assert "crash_like_reset_seen" in summary["threshold_failures"]


def test_summarize_soak_counts_recovered_command_retries():
    row = sample(
        "start",
        0,
        {
            "uptime_ms": 1000,
            "heap_free": 100000,
            "heap_min_free": 99000,
            "psram_free": 200000,
            "psram_min_free": 199000,
            "current_task_stack_free_words": 1200,
            "ui_task_stack_free_words": 1300,
            "lvgl_used_pct": 50,
        },
        {"rx_packets": 5, "tx_packets": 7},
    )
    row["results"][0]["attempts"] = 2
    row["results"][0]["recovered_after_retry"] = True
    row["results"][0]["retry_failures"] = [
        {"schema": 1, "ok": False, "cmd": "health", "code": "ESP_ERR_TIMEOUT"}
    ]

    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
    )

    assert summary["ok"] is True
    assert summary["command_retry_count"] == 1
    assert summary["command_recovered_after_retry_count"] == 1
    assert summary["command_retry_failure_count"] == 0


def test_summarize_soak_rejects_legacy_recovered_host_timeout():
    row = sample(
        "start", 0, base_health(), {"rx_packets": 0, "tx_packets": 0}
    )
    row["results"][0]["attempts"] = 2
    row["results"][0]["recovered_after_retry"] = True
    row["results"][0]["retry_failures"] = [
        {"schema": 1, "ok": False, "cmd": "health", "code": "TIMEOUT"}
    ]

    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=0,
        min_tx_delta=0,
    )

    assert summary["ok"] is False
    assert summary["command_timeout_seen"] is True
    assert "command_timeout_seen" in summary["threshold_failures"]


def test_summarize_soak_rejects_ignored_boot_marker():
    samples = [
        sample("start", 0, base_health(), {"rx_packets": 0, "tx_packets": 0}),
        sample("final", 1, base_health(2000), {"rx_packets": 0, "tx_packets": 0}),
    ]
    samples[0]["results"][0]["ignored_json"] = [
        {"cmd": "help", "ok": True}
    ]

    summary = soak_d1l.summarize_soak(
        samples=samples,
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=0,
        min_tx_delta=0,
    )

    assert summary["unexpected_console_restart_seen"] is True
    assert "unexpected_console_restart_seen" in summary["threshold_failures"]
    assert summary["ok"] is False


def test_summarize_soak_tracks_stable_sd_file_op_gate_and_store_backends():
    rows = [
        sample("start", 0, base_health(1000), {"rx_packets": 5, "tx_packets": 7}),
        sample("final", 60, base_health(61000), {"rx_packets": 8, "tx_packets": 7}),
    ]
    for row in rows:
        row["results"].append(storage_status())

    summary = soak_d1l.summarize_soak(
        samples=rows,
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        sample_storage=True,
    )

    assert summary["ok"] is True
    assert summary["storage_status_count"] == 2
    assert summary["storage_states"] == ["ready"]
    assert summary["storage_data_backends"] == ["mixed"]
    assert summary["storage_packet_log_backends"] == ["sd"]
    assert summary["storage_file_capability_ready_all"] is True
    assert summary["storage_file_ops_ready_all"] is True
    assert summary["storage_store_backends"]["packets"] == ["sd"]
    assert summary["storage_store_backends"]["exports"] == ["sd_diagnostic_exports_ready"]
    assert summary["storage_store_backend_stable_all"] is True


def test_summarize_soak_accepts_stable_retained_history_sd_backends():
    rows = [
        sample("start", 0, base_health(1000), {"rx_packets": 5, "tx_packets": 7}),
        sample("final", 60, base_health(61000), {"rx_packets": 8, "tx_packets": 7}),
    ]
    for row in rows:
        row["results"].append(
            storage_status(
                message_store_backend="sd",
                dm_store_backend="sd",
                packet_log_backend="sd",
                route_store_backend="sd",
            )
        )

    summary = soak_d1l.summarize_soak(
        samples=rows,
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        sample_storage=True,
    )

    assert summary["ok"] is True
    assert summary["storage_store_backends"]["messages"] == ["sd"]
    assert summary["storage_store_backends"]["dm"] == ["sd"]
    assert summary["storage_store_backends"]["packets"] == ["sd"]
    assert summary["storage_store_backends"]["routes"] == ["sd"]
    assert summary["storage_store_backend_stable_all"] is True


def test_summarize_soak_rejects_stale_storage_telemetry():
    row = sample("start", 0, base_health(), {"rx_packets": 5, "tx_packets": 7})
    row["results"].append(storage_status(status_stale=True, refresh_failures=3))

    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        sample_storage=True,
    )

    assert summary["ok"] is False
    assert summary["storage_status_stale_count"] == 1
    assert summary["storage_refresh_failures_max"] == 3
    assert "storage_status_stale" in summary["threshold_failures"]
    assert "storage_refresh_failures" in summary["threshold_failures"]


def test_summarize_soak_allows_pre_flash_sd_filecanary_unavailable():
    row = sample("start", 0, base_health(), {"rx_packets": 5, "tx_packets": 7})
    row["results"].append(
        storage_status(
            state="protocol_pending",
            protocol_supported=False,
            present=False,
            mounted=False,
            data_root_ready=False,
            file_ops=False,
            atomic_rename=False,
            data_enabled=False,
            data_backend="nvs",
            packet_log_backend="nvs",
        )
    )
    row["results"].append(
        {
            "schema": 1,
            "ok": False,
            "cmd": soak_d1l.SD_FILE_CANARY_COMMAND,
            "code": "ESP_ERR_NOT_SUPPORTED",
            "step": "preflight",
        }
    )

    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        sample_storage=True,
        sd_file_canary=True,
        allow_sd_unavailable=True,
    )

    assert summary["ok"] is True
    assert summary["command_failure_count"] == 0
    assert summary["sd_file_canary_unavailable_count"] == 1
    assert summary["storage_file_ops_ready_all"] is False


def test_summarize_soak_fails_store_backend_flip():
    start = sample("start", 0, base_health(1000), {"rx_packets": 5, "tx_packets": 7})
    final = sample("final", 60, base_health(61000), {"rx_packets": 8, "tx_packets": 7})
    start["results"].append(storage_status())
    changed = storage_status()
    changed["stores"] = dict(changed["stores"])
    changed["stores"]["messages"] = "sd"
    final["results"].append(changed)

    summary = soak_d1l.summarize_soak(
        samples=[start, final],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        sample_storage=True,
    )

    assert summary["ok"] is False
    assert summary["storage_store_backend_stable"]["messages"] is False
    assert "storage_store_backend_changed" in summary["threshold_failures"]


def test_summarize_soak_fails_pre_flash_sd_filecanary_without_allow_flag():
    row = sample("start", 0, base_health(), {"rx_packets": 5, "tx_packets": 7})
    row["results"].append(storage_status(state="protocol_pending", file_ops=False))
    row["results"].append(
        {
            "schema": 1,
            "ok": False,
            "cmd": soak_d1l.SD_FILE_CANARY_COMMAND,
            "code": "ESP_ERR_NOT_SUPPORTED",
            "step": "preflight",
        }
    )

    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        sample_storage=True,
        sd_file_canary=True,
        allow_sd_unavailable=False,
    )

    assert summary["ok"] is False
    assert summary["command_failure_count"] == 1
    assert "sd_file_canary_failed" in summary["threshold_failures"]


def test_summarize_soak_fails_malformed_storage_status_when_requested():
    row = sample("start", 0, base_health(), {"rx_packets": 5, "tx_packets": 7})
    row["results"].append(
        {
            "schema": 1,
            "ok": True,
            "cmd": soak_d1l.STORAGE_STATUS_COMMAND,
            "sd": {"state": "ready"},
            "data_backend": "mixed",
        }
    )

    summary = soak_d1l.summarize_soak(
        samples=[row],
        active_events=[],
        require_rx_delta=False,
        min_rx_delta=1,
        min_tx_delta=0,
        sample_storage=True,
    )

    assert summary["ok"] is False
    assert summary["storage_status_malformed_count"] == 1
    assert "storage_status_malformed" in summary["threshold_failures"]


def test_send_soak_command_does_not_retry_allowed_sd_unavailable(monkeypatch):
    calls = []

    def fake_send_console_command(_ser, command, _timeout):
        calls.append(command)
        return {
            "schema": 1,
            "ok": False,
            "cmd": soak_d1l.SD_FILE_CANARY_COMMAND,
            "code": "ESP_ERR_NOT_SUPPORTED",
            "step": "preflight",
        }

    monkeypatch.setattr(soak_d1l, "send_console_command", fake_send_console_command)

    result = soak_d1l.send_soak_command(
        None,
        soak_d1l.SD_FILE_CANARY_COMMAND,
        timeout=1.0,
        retries=3,
        retry_delay_sec=0.0,
        terminal_failure_ok=lambda row: soak_d1l.allowed_sd_unavailable(row, True),
    )

    assert len(calls) == 1
    assert result["allowed_failure"] is True
    assert "attempts" not in result


def test_sd_filecanary_timeout_uses_long_window_and_is_not_retried(monkeypatch):
    calls = []

    def fake_send_console_command(_ser, command, timeout):
        calls.append((command, timeout))
        return {
            "schema": 1,
            "ok": False,
            "cmd": command,
            "code": "TIMEOUT",
        }

    monkeypatch.setattr(soak_d1l, "send_console_command", fake_send_console_command)
    result = soak_d1l.send_soak_command(
        None,
        soak_d1l.SD_FILE_CANARY_COMMAND,
        timeout=1.0,
        retries=3,
        retry_delay_sec=0.0,
    )

    assert calls == [(soak_d1l.SD_FILE_CANARY_COMMAND, 120.0)]
    assert result["code"] == "TIMEOUT"
    assert result["attempts"] == 1
    assert result["retry_failures"] == []


def test_send_soak_command_stops_on_ignored_boot_marker(monkeypatch):
    calls = []

    def fake_send_console_command(_ser, command, timeout):
        calls.append((command, timeout))
        return {
            "schema": 1,
            "ok": True,
            "cmd": command,
            "ignored_boot_help_seen": True,
            "ignored_json": [{"cmd": "noise-5", "ok": True}],
        }

    monkeypatch.setattr(soak_d1l, "send_console_command", fake_send_console_command)
    result = soak_d1l.send_soak_command(
        None,
        "health",
        timeout=1.0,
        retries=3,
        retry_delay_sec=0.0,
    )

    assert calls == [("health", 1.0)]
    assert result["ok"] is False
    assert result["code"] == "UNEXPECTED_RESTART"
    assert result["unexpected_console_restart"] is True


def test_collect_sample_aborts_after_timeout_without_followup_commands(monkeypatch):
    calls = []

    def fake_send_soak_command(_ser, command, *_args, **_kwargs):
        calls.append(command)
        return {
            "schema": 1,
            "ok": False,
            "cmd": command,
            "code": "TIMEOUT",
        }

    monkeypatch.setattr(soak_d1l, "send_soak_command", fake_send_soak_command)
    row = soak_d1l.collect_sample(
        None,
        timeout=1.0,
        label="start",
        elapsed_sec=0.0,
        command_retries=3,
        retry_delay_sec=0.0,
        commands=["health", "storage status", "crashlog"],
    )

    assert calls == ["health"]
    assert row["aborted_after_timeout"] == "health"
    assert len(row["results"]) == 1


class FakeSoakPort:
    def reset_input_buffer(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


def run_soak_for_timeout_test(**overrides):
    args = {
        "port": "COM12",
        "baud": 115200,
        "timeout": 1.0,
        "duration_sec": 10.0,
        "sample_interval_sec": 60.0,
        "active_public_text": None,
        "active_dm_fingerprint": None,
        "active_dm_text": None,
        "active_interval_sec": 60.0,
        "startup_settle_sec": 0.0,
        "require_rx_delta": False,
        "min_rx_delta": 0,
        "min_tx_delta": 0,
        "clear_crashlog_before_start": False,
        "command_retries": 1,
        "retry_delay_sec": 0.0,
        "sample_storage": False,
        "sd_file_canary": False,
        "allow_sd_unavailable": False,
    }
    args.update(overrides)
    return soak_d1l.run_serial_soak(**args)


def test_soak_stops_after_crashlog_clear_timeout(monkeypatch):
    calls = []

    def fake_send(_ser, command, *_args, **_kwargs):
        calls.append(command)
        return {"schema": 1, "ok": False, "cmd": command, "code": "TIMEOUT"}

    class FakeSerialModule:
        pass

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(soak_d1l, "open_d1l_serial", lambda *_args, **_kwargs: FakeSoakPort())
    monkeypatch.setattr(soak_d1l, "send_soak_command", fake_send)
    monkeypatch.setattr(soak_d1l.time, "sleep", lambda _seconds: None)
    report = run_soak_for_timeout_test(clear_crashlog_before_start=True)

    assert calls == ["crashlog clear"]
    assert report["aborted_after_timeout"] == "crashlog clear"
    assert report["samples"] == []
    assert report["ok"] is False
    assert report["commands"] == soak_d1l.SOAK_COMMANDS


def test_soak_stops_after_active_command_timeout(monkeypatch):
    calls = []
    collected = []

    def fake_send(_ser, command, *_args, **_kwargs):
        calls.append(command)
        return {"schema": 1, "ok": False, "cmd": command, "code": "TIMEOUT"}

    def fake_collect(*_args, **_kwargs):
        collected.append(True)
        row = sample(
            "start", 0, base_health(), {"rx_packets": 0, "tx_packets": 0}
        )
        row["aborted_after_timeout"] = None
        return row

    class FakeSerialModule:
        pass

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(soak_d1l, "open_d1l_serial", lambda *_args, **_kwargs: FakeSoakPort())
    monkeypatch.setattr(soak_d1l, "send_soak_command", fake_send)
    monkeypatch.setattr(soak_d1l, "collect_sample", fake_collect)
    monkeypatch.setattr(soak_d1l.time, "sleep", lambda _seconds: None)
    report = run_soak_for_timeout_test(
        active_dm_fingerprint="0BF0A701D5AE2DB6",
        active_dm_text="test",
    )

    assert len(collected) == 1
    assert calls == ["mesh send dm 0BF0A701D5AE2DB6 test"]
    assert report["aborted_after_timeout"] == "mesh send dm 0BF0A701D5AE2DB6 test"
    assert len(report["samples"]) == 1
    assert report["ok"] is False


def test_final_sample_timeout_is_reported_at_top_level(monkeypatch):
    collected = []

    def fake_collect(_ser, _timeout, label, elapsed_sec, *_args, **_kwargs):
        collected.append(label)
        if label == "final":
            return {
                "label": label,
                "elapsed_sec": elapsed_sec,
                "aborted_after_timeout": "health",
                "results": [
                    {
                        "schema": 1,
                        "ok": False,
                        "cmd": "health",
                        "code": "TIMEOUT",
                    }
                ],
            }
        row = sample(
            label,
            elapsed_sec,
            base_health(),
            {"rx_packets": 0, "tx_packets": 0},
        )
        row["aborted_after_timeout"] = None
        return row

    class FakeSerialModule:
        pass

    monkeypatch.setitem(__import__("sys").modules, "serial", FakeSerialModule())
    monkeypatch.setattr(
        soak_d1l, "open_d1l_serial", lambda *_args, **_kwargs: FakeSoakPort()
    )
    monkeypatch.setattr(soak_d1l, "collect_sample", fake_collect)
    monkeypatch.setattr(soak_d1l.time, "sleep", lambda _seconds: None)
    report = run_soak_for_timeout_test(duration_sec=0.001)

    assert collected == ["start", "final"]
    assert report["aborted_after_timeout"] == "health"
    assert report["ok"] is False
