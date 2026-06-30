from scripts import soak_d1l


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


def test_dry_run_reports_soak_commands():
    report = soak_d1l.dry_run_report(
        duration_sec=60,
        sample_interval_sec=10,
        active_public_text="test",
        active_interval_sec=30,
        require_rx_delta=True,
        min_rx_delta=1,
        min_tx_delta=1,
        clear_crashlog_before_start=True,
        command_retries=1,
        retry_delay_sec=0.5,
    )

    assert report["ok"] is True
    assert report["commands"] == soak_d1l.SOAK_COMMANDS
    assert report["active_public_text"] == "test"
    assert report["require_rx_delta"] is True
    assert report["clear_crashlog_before_start"] is True
    assert report["command_retries"] == 1


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
            "text": "test",
            "result": {"schema": 1, "ok": True, "cmd": "mesh send public"},
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
    assert summary["lvgl_used_pct_peak"] == 54
    assert summary["signal_sample_count_peak"] == 6
    assert summary["command_retry_count"] == 0


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
        {"schema": 1, "ok": False, "cmd": "health", "code": "TIMEOUT"}
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
