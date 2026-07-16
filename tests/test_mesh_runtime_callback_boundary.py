from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source: str, signature: str, next_signature: str) -> str:
    return source.split(signature, 1)[1].split(next_signature, 1)[0]


def test_sx1262_callbacks_only_copy_timestamp_enqueue_and_return():
    source = read("main/mesh/meshcore_service.c")
    callback_region = source.split("/* SX1262 callbacks are deliberately bounded", 1)[1].split(
        "static RadioEvents_t s_radio_events", 1
    )[0]

    for callback in (
        "on_tx_done",
        "on_tx_timeout",
        "on_rx_done",
        "on_rx_timeout",
        "on_rx_error",
    ):
        assert callback in callback_region
    assert callback_region.count("enqueue_radio_event(") == 5
    assert callback_region.count(
        "capture_callback_tx_operation(origin, &identity)"
    ) == 2
    for forbidden in (
        "parse_rx_",
        "d1l_node_store_",
        "d1l_contact_store_",
        "d1l_dm_store_",
        "d1l_message_store_",
        "append_packet_log",
        "meshcore_service_request_rx_async",
        "Radio.",
    ):
        assert forbidden not in callback_region

    enqueue = function_body(
        source,
        "static void enqueue_radio_event",
        "/* SX1262 callbacks are deliberately bounded",
    )
    assert "esp_timer_get_time()" in enqueue
    assert "memcpy(event.raw, payload, size)" in enqueue
    assert "size > D1L_MESHCORE_MAX_RAW_PACKET" in enqueue
    assert "xQueueSend(s_radio_event_queue, &event, 0)" in enqueue
    assert "runtime_note_queue_drop(true)" in enqueue
    assert enqueue.count(
        "meshcore_service_latch_radio_recovery(type, tx_operation)"
    ) == 3
    terminal_latch = enqueue.index(
        "meshcore_service_latch_radio_recovery(type, tx_operation)"
    )
    assert enqueue.index("D1L_MESHCORE_SERVICE_EVENT_TX_DONE") < terminal_latch
    assert enqueue.index("return;", terminal_latch) < enqueue.index(
        "xQueueSend(s_radio_event_queue, &event, 0)"
    )
    assert "meshcore_service_wake()" in enqueue


def test_mesh_runtime_task_owns_radio_event_processing_and_telemetry():
    source = read("main/mesh/meshcore_service.c")
    header = read("main/mesh/meshcore_service.h")
    guard = read("main/mesh/meshcore_runtime_guard.h")
    console = read("main/comms/usb_console.c")
    task = function_body(
        source,
        "static void meshcore_service_task(void *arg)",
        "static esp_err_t meshcore_service_start_task(void)",
    )

    assert "xQueueReceive(s_radio_event_queue, &cmd, 0)" in task
    assert "xQueueReceive(s_service_queue, &cmd, 0)" in task
    assert "xQueueReceive(\n                    s_priority_command_queue" in task
    assert task.index("s_radio_event_queue") < task.index("s_service_queue")
    assert "D1L_MESHCORE_OWNER_POLL_MS 250U" in source
    assert "pdMS_TO_TICKS(D1L_MESHCORE_OWNER_POLL_MS)" in task
    assert "ulTaskNotifyTake(pdTRUE, portMAX_DELAY)" not in task
    assert task.count("meshcore_service_run_owner_maintenance();") >= 3
    for handler in (
        "meshcore_service_handle_radio_rx_done",
        "meshcore_service_handle_radio_rx_timeout",
        "meshcore_service_handle_radio_rx_error",
    ):
        assert handler in task
    assert "meshcore_service_handle_latched_radio_terminal" in task
    latched = function_body(
        source,
        "static void meshcore_service_handle_latched_radio_terminal",
        "static void enqueue_radio_event",
    )
    assert "meshcore_service_handle_radio_tx_done(event)" in latched
    assert "meshcore_service_handle_radio_tx_timeout(event)" in latched
    assert "d1l_mesh_runtime_counter_increment_saturating(" in task
    assert "&s_status.runtime_task_heartbeat" in task
    assert "uxTaskGetStackHighWaterMark(NULL)" in task
    assert "runtime_last_event_monotonic_us = cmd.monotonic_us" in task

    assert "D1L_MESHCORE_RADIO_EVENT_QUEUE_LEN 8U" in source
    assert "D1L_MESHCORE_PRIORITY_QUEUE_LEN 4U" in source
    assert "D1L_MESHCORE_SERVICE_TASK_STACK_BYTES 8192U" in source
    assert "D1L_MESH_OWNER_RADIO_EVENT_BURST_MAX 4U" in guard
    assert "D1L_MESH_OWNER_PRIORITY_COMMAND_BURST_MAX 2U" in guard
    assert "d1l_mesh_owner_scheduler_choose(" in task
    assert "xQueueCreate(\n            D1L_MESHCORE_RADIO_EVENT_QUEUE_LEN" in source
    for field in (
        "runtime_command_queue_depth",
        "runtime_command_queue_high_water",
        "runtime_priority_queue_depth",
        "runtime_priority_queue_high_water",
        "runtime_event_queue_depth",
        "runtime_event_queue_high_water",
        "runtime_queue_drops",
        "runtime_callback_event_drops",
        "runtime_command_queue_saturation",
        "runtime_priority_queue_saturation",
        "runtime_fairness_forced_commands",
        "runtime_priority_burst_high_water",
        "runtime_priority_burst_bound",
        "runtime_owner_maintenance_runs",
        "runtime_terminal_recovery_dispatches",
        "runtime_task_heartbeat",
        "runtime_task_stack_free_words",
        "runtime_last_event_monotonic_us",
    ):
        assert field in header
        assert field in console
    assert '\\\"callback_boundary\\\":\\\"copy_timestamp_enqueue\\\"' in console


def test_all_exact_terminals_use_the_preemptive_owner_mailbox():
    source = read("main/mesh/meshcore_service.c")
    task = function_body(
        source,
        "static void meshcore_service_task(void *arg)",
        "static esp_err_t meshcore_service_start_task(void)",
    )
    latch = function_body(
        source,
        "static void meshcore_service_latch_radio_recovery",
        "static bool meshcore_service_take_latched_terminal",
    )
    take = function_body(
        source,
        "static bool meshcore_service_take_latched_terminal",
        "static void status_lock",
    )
    handlers = source.split(
        "static void meshcore_service_handle_radio_tx_done", 1
    )[1].split("/* SX1262 callbacks are deliberately bounded", 1)[0]

    assert "D1L_CALLBACK_TX_TERMINAL_WRITING" in latch
    assert "D1L_CALLBACK_TX_TERMINAL_DONE" in latch
    assert "D1L_CALLBACK_TX_TERMINAL_TIMEOUT" in latch
    assert "d1l_mesh_terminal_lane_publish_slot(" in latch
    assert "&s_terminal_lane, terminal_slot" in latch
    assert latch.index("d1l_mesh_terminal_lane_publish_slot(") < latch.index(
        "__atomic_compare_exchange_n("
    )
    assert "for (" not in latch
    assert "while (" not in latch
    assert "d1l_mesh_terminal_lane_take_pending(" in take
    assert "capture_callback_tx_operation(origin, &identity)" in take
    assert "out_event->tx_operation = best_identity" in take
    assert "meshcore_service_take_latched_terminal(&recovery_event)" in task
    assert task.index("meshcore_service_take_latched_terminal") < task.index(
        "xQueueReceive"
    )
    assert "meshcore_service_handle_latched_radio_terminal(&recovery_event)" in (
        "".join(task.split())
    )
    assert "meshcore_service_handle_radio_tx_timeout(event)" in handlers
    assert "meshcore_service_handle_radio_tx_done(event)" in handlers
    assert "d1l_mesh_rx_recovery_take(" in task
    assert "d1l_mesh_terminal_lane_has_pending(" in task
    assert "d1l_mesh_terminal_lane_try_reserve_owner(" in task
    assert "d1l_mesh_terminal_lane_owner_still_reserved(" in task
    assert "meshcore_service_handle_radio_rx_error()" in task
    assert "s_tx_busy && meshcore_service_command_requires_idle_tx" in task

    # Recovery executes directly on the sole radio owner. A full command queue
    # cannot suppress exact terminal handling or accumulate START_RX feedback.
    assert handlers.count("d1l_meshcore_start_rx();") == 5
    assert "meshcore_service_request_rx_async" not in source


def test_radio_parsers_preserve_the_callback_copy_as_const():
    source = read("main/mesh/meshcore_service.c")
    for parser in (
        "parse_rx_channel_packet",
        "parse_rx_dm_packet",
        "parse_rx_ack_packet",
        "parse_rx_path_packet",
        "parse_rx_trace_packet",
        "parse_rx_advert_packet",
    ):
        signature = source.split(f"{parser}(", 1)[1].split(")", 1)[0]
        assert "const uint8_t *payload" in signature
