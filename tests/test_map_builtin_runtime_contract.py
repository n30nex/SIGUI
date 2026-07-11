from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def body(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_builtin_openstreetmap_source_is_fixed_identified_and_attributed():
    header = read("main/storage/map_tile_store.h")
    console = read("main/comms/usb_console.c")

    assert 'D1L_MAP_TILE_SOURCE_ID "openstreetmap-standard"' in header
    assert (
        'D1L_MAP_TILE_SOURCE_URL_TEMPLATE '
        '"https://tile.openstreetmap.org/{z}/{x}/{y}.png"'
    ) in header
    assert (
        'D1L_MAP_TILE_USER_AGENT '
        '"MeshCore-DeskOS-D1L/1.0 (+https://github.com/n30nex/SIGUI)"'
    ) in header
    assert 'D1L_MAP_TILE_LICENSE_URL "https://www.openstreetmap.org/copyright"' in header
    assert 'D1L_MAP_TILE_ATTRIBUTION "\\xC2\\xA9 OpenStreetMap contributors"' in header
    assert "D1L_MAP_TILE_MIN_CACHE_DAYS 7U" in header
    assert "D1L_MAP_TILE_DOWNLOAD_MAX_BYTES (196U * 1024U)" in header

    assert "provider_saved" not in console
    assert "map-provider" not in console
    assert "storage map-tile-download" not in console
    assert "<url-template>" not in console
    assert "cmd_map_tiles_status" in console
    assert 'strcmp(line, "map tiles status")' in console


def test_current_view_planner_is_one_zoom_center_first_and_at_most_three_by_three():
    header = read("main/map/map_math.h")
    source = read("main/map/map_math.c")

    assert "D1L_MAP_VIEW_FIXED_ZOOM 12U" in header
    assert "D1L_MAP_VIEW_MAX_TILES 9U" in header
    assert "(max_unwrapped_x - min_unwrapped_x + 1LL) > 3LL" in source
    assert "(max_y - min_y + 1LL) > 3LL" in source
    assert "planned_count >= D1L_MAP_VIEW_MAX_TILES" in source
    assert "wrap_tile_x(raw_x, tile_count)" in source
    assert "raw_y < 0 || raw_y >= (int64_t)tile_count" in source
    assert "already_planned(planned, planned_count, x, y)" in source
    assert "sort_center_first(planned, planned_count)" in source
    assert "distance_sq" in source


def test_worker_is_sequential_cancelable_and_never_fetches_without_persistent_cache():
    service = read("main/map/map_view_service.c")
    store = read("main/storage/map_tile_store.c")
    run = body(service, "static void run_generation", "static void map_worker")

    assert run.count("for (uint8_t i = 0;") == 1
    assert run.index("d1l_map_tile_store_read(") < run.index("d1l_map_tile_store_fetch(")
    assert "if (!sd_ready || !publish_initial_frame" in run
    assert "generation_continue(&generation)" in run
    assert "generation_continue, &generation" in run
    assert "wait_for_wifi(generation)" in run
    assert "++s_map.status.network_requests" in run
    assert run.index("++s_map.status.network_requests") < run.index(
        "d1l_map_tile_store_fetch("
    )
    assert "tile_result.status_code == 429 || tile_result.status_code == 503" in run
    assert "D1L_MAP_DEFAULT_RETRY_AFTER_SEC" in run
    assert "tile_result.retry_after_sec > 0U" in run
    assert "tile_result.status_code != 0 && tile_result.status_code != 200" in run
    systemic_http = run.split(
        "tile_result.status_code != 0 && tile_result.status_code != 200", 1
    )[1].split("continue;", 1)[0]
    assert "break;" in systemic_http

    fetch = body(
        store,
        "esp_err_t d1l_map_tile_store_fetch",
        "esp_err_t d1l_map_tile_store_write_canary",
    )
    assert fetch.index("if (!wifi_connected)") < fetch.index("esp_http_client_init")
    assert fetch.index("if (!result.sd_ready)") < fetch.index("esp_http_client_init")
    assert "continue_allowed(should_continue, continue_context)" in fetch
    assert ".user_agent = D1L_MAP_TILE_USER_AGENT" in fetch
    assert ".crt_bundle_attach = esp_crt_bundle_attach" in fetch
    assert "result.status_code == 429" in fetch
    assert "png_content_type(headers.content_type)" in fetch
    assert "d1l_map_tile_png_valid(buffer, result.bytes)" in fetch


def test_http_header_length_contract_handles_errors_chunking_and_hard_bounds():
    store = read("main/storage/map_tile_store.c")
    fetch = body(
        store,
        "esp_err_t d1l_map_tile_store_fetch",
        "esp_err_t d1l_map_tile_store_write_canary",
    )

    header_fetch = fetch.split("esp_http_client_fetch_headers(client)", 1)[1].split(
        "result.status_code =", 1
    )[0]
    assert "if (content_length < 0)" in header_fetch
    assert 'download_step(&result, "fetch_headers", ESP_FAIL' in header_fetch
    assert "esp_http_client_is_chunked_response(client)" in header_fetch
    assert "content_length_known = !chunked && content_length > 0" in header_fetch
    assert "buffer_size < D1L_MAP_TILE_DOWNLOAD_MAX_BYTES" in header_fetch

    assert "content_length_known && content_length > (int64_t)download_limit" in fetch
    assert "const size_t remaining = download_limit - result.bytes" in fetch
    assert "(size_t)read_len > want" in fetch
    assert "content_length_known && result.bytes != (size_t)content_length" in fetch
    assert "content_length >= 0 && result.bytes" not in fetch


def test_https_download_waits_for_valid_sntp_time_and_remains_cancelable():
    store = read("main/storage/map_tile_store.c")
    service = read("main/map/map_view_service.c")
    ui = read("main/ui/ui_map.c")
    defaults = read("sdkconfig.defaults")
    fetch = body(
        store,
        "esp_err_t d1l_map_tile_store_fetch",
        "esp_err_t d1l_map_tile_store_write_canary",
    )
    clock = body(store, "static esp_err_t ensure_tls_clock", "static void init_download_result")
    run = body(service, "static void run_generation", "static void map_worker")

    assert 'ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org")' in clock
    assert "esp_netif_sntp_init(&config)" in clock
    assert "esp_netif_sntp_sync_wait(wait_ticks)" in clock
    assert "s_map_sntp_can_sync_wait = init_ret == ESP_OK" in clock
    assert "wait_ret == ESP_ERR_INVALID_STATE" in clock
    assert "vTaskDelay(wait_ticks)" in clock
    assert "continue_allowed(should_continue, continue_context)" in clock
    assert "D1L_MAP_TILE_TIME_SYNC_SLICE_MS" in clock
    assert fetch.index("ensure_tls_clock(should_continue, continue_context)") < fetch.index(
        "esp_http_client_init(&config)"
    )
    assert 'download_step(&result, "time_sync"' in fetch
    assert 'strcmp(tile_result.step, "time_sync") == 0' in run
    assert 'strcmp(status->phase, "time_sync") == 0' in ui
    assert 'title = "Secure time needed"' in ui
    assert "CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y" in defaults
    assert "CONFIG_MBEDTLS_HAVE_TIME_DATE=y" in defaults


def test_cache_commit_requires_valid_png_and_attribution_metadata_atomically():
    source = read("main/storage/map_tile_store.c")
    read_cache = body(
        source,
        "esp_err_t d1l_map_tile_store_read",
        "typedef struct {\n    char content_type",
    )
    fetch = body(
        source,
        "esp_err_t d1l_map_tile_store_fetch",
        "esp_err_t d1l_map_tile_store_write_canary",
    )

    assert "attribution_metadata_present()" in read_cache
    assert "d1l_map_tile_png_valid(buffer, result.bytes)" in read_cache
    assert "result.cache_hit = true" in read_cache
    assert "const uint32_t expected_size = file.size" in read_cache
    assert "while (result.bytes < expected_size)" in read_cache
    assert "d1l_rp2040_file_result_t read_result = {0}" in read_cache
    assert "read_result.eof !=" in read_cache
    assert "result.bytes != (size_t)expected_size || !saw_eof" in read_cache
    assert fetch.index("d1l_map_tile_png_valid(buffer, result.bytes)") < fetch.index(
        "d1l_rp2040_bridge_file_rename(result.tmp_path, result.path, true"
    )
    assert fetch.index("d1l_rp2040_bridge_file_rename(result.tmp_path, result.path, true") < fetch.index(
        "write_attribution_metadata(&result)"
    )
    metadata_failure = fetch.split("ret = write_attribution_metadata(&result);", 1)[1].split(
        'download_step(&result, "ok"', 1
    )[0]
    assert "d1l_rp2040_bridge_file_delete(result.path" in metadata_failure
    assert "d1l_rp2040_bridge_file_delete(result.attribution_tmp_path" in metadata_failure


def test_worker_publishes_immutable_psram_frames_without_lvgl_calls_or_replay():
    service = read("main/map/map_view_service.c")
    worker = body(service, "static void map_worker", "esp_err_t d1l_map_view_service_init")
    release = service.split("void d1l_map_view_service_release_frame", 1)[1]

    assert "uint16_t *frames[2]" in service
    assert "uint8_t frame_readers[2]" in service
    assert service.count("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") >= 4
    assert "wait_for_frame_slot(work, generation)" in service
    assert "++s_map.frame_readers[slot]" in service
    assert "--s_map.frame_readers[frame->slot]" in service
    assert "xSemaphoreTake(s_map.lock, portMAX_DELAY)" in release
    assert release.index("--s_map.frame_readers[frame->slot]") < release.index(
        "memset(frame, 0, sizeof(*frame))"
    )
    assert "lv_" not in service
    assert "#include \"lvgl" not in service
    assert "ulTaskNotifyTake(pdTRUE, 0U)" in worker
    assert "s_map.status.generation != generation" in worker
    assert "s_map.status.frame_ready = true" in service
    assert service.index("s_map.status.frame_ready = true") < service.index(
        "++s_map.status.frame_revision"
    )


def test_same_visible_plan_reuses_generation_and_does_not_notify_worker_again():
    service = read("main/map/map_view_service.c")
    acquire = body(
        service,
        "esp_err_t d1l_map_view_service_acquire_visible",
        "void d1l_map_view_service_release_visible",
    )
    identical = acquire.split("if (s_map.status.visible", 1)[1].split(
        "uint32_t generation", 1
    )[0]

    assert "*out_generation = s_map.status.generation" in identical
    assert "xTaskNotifyGive" not in identical
    assert "xTaskNotifyGive(s_map.worker)" in acquire
    assert acquire.index("uint32_t generation") < acquire.index("xTaskNotifyGive(s_map.worker)")


def test_visible_lease_revocation_cannot_time_out_before_worker_notification():
    service = read("main/map/map_view_service.c")
    release = body(
        service,
        "void d1l_map_view_service_release_visible",
        "void d1l_map_view_service_status",
    )

    assert "xSemaphoreTake(s_map.lock, portMAX_DELAY)" in release
    assert "pdMS_TO_TICKS" not in release
    assert release.index("s_map.status.visible = false") < release.index(
        "xTaskNotifyGive(s_map.worker)"
    )
