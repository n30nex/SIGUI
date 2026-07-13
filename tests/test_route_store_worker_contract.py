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
    assert "d1l_route_store_flush_if_due()" in worker
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
    assert "D1L_ROUTE_REBOOT_FLUSH_TIMEOUT_MS" in console
    assert "D1L_ROUTE_REBOOT_FLUSH_TIMEOUT_MS = 15000U" in console
