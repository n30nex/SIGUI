from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_route_persistence_has_a_dedicated_bounded_worker():
    worker = read("main/mesh/route_store_worker.c")
    header = read("main/mesh/route_store_worker.h")
    cmake = read("main/CMakeLists.txt")
    app = read("main/app_main.c")
    service = read("main/mesh/meshcore_service.c")
    console = read("main/comms/usb_console.c")

    assert "d1l_route_store_worker_start" in header
    assert "d1l_route_store_worker_force_flush" in header
    assert "D1L_ROUTE_STORE_WORKER_INTERVAL_MS 1000U" in worker
    assert "D1L_ROUTE_STORE_WORKER_STACK_BYTES 12288U" in worker
    assert "D1L_ROUTE_STORE_WORKER_PRIORITY (tskIDLE_PRIORITY + 1U)" in worker
    assert "d1l_health_monitor_register_retained_task(created_task)" in worker
    assert "flush_retained_stores" in worker
    assert "d1l_message_store_flush_if_due()" in worker
    assert "d1l_dm_store_flush_if_due()" in worker
    assert "d1l_packet_log_flush_if_due()" in worker
    assert "d1l_route_store_flush_if_due()" in worker
    assert "d1l_message_store_flush()" in worker
    assert "d1l_dm_store_flush()" in worker
    assert "d1l_packet_log_flush()" in worker
    assert "d1l_route_store_flush()" in worker
    assert "request_id" in worker
    assert "s_result_request_id" in worker
    assert "elapsed_us >= (int64_t)timeout_ms * 1000LL" in worker
    assert "xQueueSend(s_request_queue, &request, 0)" in worker
    assert "s_worker_starting" in worker
    assert "already_starting" in worker
    assert '"mesh/route_store_worker.c"' in cmake
    assert "d1l_route_store_worker_start()" in app
    assert "xQueueReceive(s_service_queue, &cmd, portMAX_DELAY)" in service
    assert "d1l_route_store_flush_if_due()" not in service
    assert "d1l_route_store_worker_force_flush(" in console
    assert "D1L_REBOOT_QUIESCE_TIMEOUT_MS = 15000U" in console
    assert "retained storage flush failed; reboot cancelled" in console
    assert r'\"retained_flush\":\"ESP_OK\"' in console


def test_reboot_quiesces_storage_and_uart_under_one_deadline():
    console = read("main/comms/usb_console.c")
    storage = read("main/storage/storage_status.c")
    storage_header = read("main/storage/storage_status.h")
    bridge = read("main/hal/rp2040_bridge.c")
    bridge_header = read("main/hal/rp2040_bridge.h")

    assert "d1l_storage_manager_quiesce_begin" in storage_header
    assert "d1l_storage_manager_quiesce_end" in storage_header
    assert "d1l_rp2040_bridge_quiesce_begin" in bridge_header
    assert "d1l_rp2040_bridge_quiesce_end" in bridge_header

    manager_quiesce = storage.split(
        "esp_err_t d1l_storage_manager_quiesce_begin", 1
    )[1].split("void d1l_storage_manager_quiesce_end", 1)[0]
    manager_quiesce_end = storage.split(
        "void d1l_storage_manager_quiesce_end", 1
    )[1].split("void d1l_storage_manager_force_nvs", 1)[0]
    assert "return ESP_ERR_INVALID_ARG" in manager_quiesce
    assert "return ESP_ERR_INVALID_STATE" in manager_quiesce
    assert manager_quiesce.index("d1l_storage_manager_pause") < manager_quiesce.index(
        "xSemaphoreTake(s_manager_sequence_mutex, ticks)"
    )
    assert "d1l_storage_manager_resume();\n        return ESP_ERR_TIMEOUT;" in manager_quiesce
    assert manager_quiesce.index("xSemaphoreTake(s_manager_sequence_mutex, ticks)") < manager_quiesce.index(
        "s_manager_quiesce_owner = xTaskGetCurrentTaskHandle()"
    )
    assert "s_manager_quiesce_owner != current" in manager_quiesce_end
    assert manager_quiesce_end.index("s_manager_quiesce_owner = NULL") < manager_quiesce_end.index(
        "manager_sequence_give()"
    ) < manager_quiesce_end.index("d1l_storage_manager_resume()")

    bridge_quiesce = bridge.split(
        "esp_err_t d1l_rp2040_bridge_quiesce_begin", 1
    )[1].split("void d1l_rp2040_bridge_quiesce_end", 1)[0]
    bridge_quiesce_end = bridge.split(
        "void d1l_rp2040_bridge_quiesce_end", 1
    )[1].split("static esp_err_t pulse_rp2040_reset", 1)[0]
    assert "return ESP_ERR_INVALID_ARG" in bridge_quiesce
    assert "return ESP_ERR_INVALID_STATE" in bridge_quiesce
    assert "xSemaphoreTake(s_bridge_mutex, ticks)" in bridge_quiesce
    assert "D1L_RP2040_BRIDGE_LOCK_GRACE_MS" not in bridge_quiesce
    assert bridge_quiesce.index("xSemaphoreTake(s_bridge_mutex, ticks)") < bridge_quiesce.index(
        "s_bridge_quiesce_owner = xTaskGetCurrentTaskHandle()"
    )
    assert "s_bridge_quiesce_owner != current" in bridge_quiesce_end
    assert bridge_quiesce_end.index("s_bridge_quiesce_owner = NULL") < bridge_quiesce_end.index(
        "give_bridge_lock()"
    )

    reboot = console.split(
        '} else if (strcmp(line, "reboot") == 0) {', 1
    )[1].split('} else if (strcmp(line, "factory-reset-confirm") == 0) {', 1)[0]
    assert reboot.count("reboot_deadline_remaining_ms(reboot_started_us)") == 4
    assert reboot.index("d1l_storage_manager_quiesce_begin") < reboot.index(
        "d1l_route_store_worker_force_flush"
    )
    assert reboot.index("d1l_route_store_worker_force_flush") < reboot.index(
        "d1l_rp2040_bridge_quiesce_begin"
    )
    assert reboot.index("d1l_rp2040_bridge_quiesce_begin") < reboot.index(
        "reboot_deadline_remaining_ms(reboot_started_us) == 0U"
    ) < reboot.index(
        "ok_begin(\"reboot\")"
    ) < reboot.index(
        "fflush(stdout)"
    ) < reboot.index(
        "esp_restart()"
    )
    assert reboot.count("d1l_storage_manager_quiesce_end()") == 3
    assert reboot.count("d1l_rp2040_bridge_quiesce_end()") == 1
    final_deadline = reboot.index("reboot quiesce deadline expired")
    assert reboot.rfind("d1l_rp2040_bridge_quiesce_end()", 0, final_deadline) < reboot.rfind(
        "d1l_storage_manager_quiesce_end()", 0, final_deadline
    )
    assert final_deadline < reboot.index("ok_begin(\"reboot\")")
    flush_failure = reboot.index("retained storage flush failed")
    assert reboot.rfind("d1l_storage_manager_quiesce_end()", 0, flush_failure) != -1
    bridge_failure = reboot.index("RP2040 bridge quiesce failed")
    assert reboot.rfind("d1l_storage_manager_quiesce_end()", flush_failure, bridge_failure) != -1
    assert "storage manager quiesce failed; reboot cancelled" in reboot
    assert "RP2040 bridge quiesce failed; reboot cancelled" in reboot
    assert r'\"storage_manager_quiesced\":true' in reboot
    assert r'\"rp2040_bridge_quiesced\":true' in reboot
