from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def body(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_time_service_is_initialized_once_at_the_platform_boundary():
    cmake = read("main/CMakeLists.txt")
    app = read("main/app_main.c")
    assert '"platform/time_service.c"' in cmake
    assert '"platform/time_service_core.c"' in cmake
    assert '#include "platform/time_service.h"' in app
    assert "d1l_time_service_init()" in app
    assert app.index("d1l_time_service_init()") < app.index("d1l_settings_load()")


def test_clock_domains_and_truth_states_are_explicit():
    header = read("main/platform/time_service_core.h")
    source = read("main/platform/time_service_core.c")
    for token in (
        "D1L_TIME_VALIDITY_UNSET",
        "D1L_TIME_VALIDITY_MONOTONIC_ONLY",
        "D1L_TIME_VALIDITY_APPROXIMATE",
        "D1L_TIME_VALIDITY_NETWORK_VALIDATED",
        "D1L_TIME_VALIDITY_COMPANION_VALIDATED",
        "D1L_TIME_SOURCE_BOOT_MONOTONIC",
        "D1L_TIME_SOURCE_RETAINED_AUTHENTICATED",
        "D1L_TIME_SOURCE_SNTP",
        "D1L_TIME_SOURCE_COMPANION_AUTHENTICATED",
    ):
        assert token in header
    assert "boot_monotonic_us" in header
    assert "wall_anchor_monotonic_us" in header
    assert "protocol_reserved_through" in header
    assert "wall_valid = core->wall_set" in source
    assert "certificate_time_valid" in source


def test_protocol_allocator_reserves_before_use_and_has_no_volatile_fallback():
    service = read("main/platform/time_service.c")
    core = read("main/platform/time_service_core.c")
    settings = read("main/app/settings_model.c")
    assert '#define D1L_TIME_NVS_NAMESPACE "d1l_settings"' in service
    assert '#define D1L_TIME_PROTOCOL_LEGACY_KEY "mesh_ts"' in service
    assert '#define D1L_TIME_PROTOCOL_HIGH_WATER_KEY "mesh_hi_v2"' in service
    assert "D1L_TIME_PROTOCOL_RESERVATION_SIZE 64U" in read(
        "main/platform/time_service_core.h"
    )
    allocate = body(
        core,
        "esp_err_t d1l_time_core_next_protocol_timestamp",
        "void d1l_time_core_snapshot",
    )
    assert allocate.index("reserve(reserve_context, new_reserved_through)") < allocate.index(
        "*out_timestamp = candidate"
    )
    assert "nvs_commit(handle)" in service
    assert "mesh_timestamp_can_fallback" not in settings
    assert "next_ram_mesh_timestamp" not in settings
    assert "d1l_settings_next_mesh_timestamp" in service


def test_legacy_protocol_seed_requires_explicit_migration_and_is_preserved():
    service = read("main/platform/time_service.c")
    header = read("main/platform/time_service_core.h")
    load = body(
        service,
        "static esp_err_t load_protocol_seed_locked",
        "static esp_err_t reserve_protocol_range",
    )
    reserve = body(
        service,
        "static esp_err_t reserve_protocol_range",
        "static bool continue_allowed",
    )
    assert "D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED" in header
    assert "D1L_TIME_PROTOCOL_LEGACY_KEY" in load
    assert "d1l_time_core_classify_protocol_seed(" in load
    assert "ret = ESP_ERR_INVALID_STATE" in load
    assert "lower bound, not proof" in load
    assert "D1L_TIME_PROTOCOL_HIGH_WATER_KEY" in reserve
    assert "D1L_TIME_PROTOCOL_LEGACY_KEY" not in reserve
    assert "protocol_persistence_state" in read("main/platform/time_service.h")


def test_wall_jumps_cannot_rewind_protocol_and_approximate_is_not_tls_valid():
    core = read("main/platform/time_service_core.c")
    allocate = body(
        core,
        "esp_err_t d1l_time_core_next_protocol_timestamp",
        "void d1l_time_core_snapshot",
    )
    assert "current_wall > (int64_t)candidate" in allocate
    assert "candidate = (uint32_t)current_wall" in allocate
    certificate = body(
        core,
        "bool d1l_time_core_certificate_validity",
        "const char *d1l_time_validity_name",
    )
    assert "D1L_TIME_VALIDITY_NETWORK_VALIDATED" in certificate
    assert "D1L_TIME_VALIDITY_COMPANION_VALIDATED" in certificate
    assert "D1L_TIME_VALIDITY_APPROXIMATE" not in certificate


def test_sntp_owner_uses_a_wait_semaphore_and_bounded_cancellation():
    service = read("main/platform/time_service.c")
    wait = body(
        service,
        "esp_err_t d1l_time_service_wait_for_certificate_time",
        "esp_err_t d1l_time_service_set_companion_time",
    )
    assert 'ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org")' in service
    assert "config.wait_for_sync = true" in service
    assert "esp_netif_sntp_init(&config)" in service
    assert "esp_netif_sntp_sync_wait(wait_ticks)" in wait
    assert "accept_sntp_system_time(expected_generation)" in wait
    assert wait.count("continue_allowed(should_continue, continue_context)") >= 3
    assert "wait_ms > slice_ms" in wait
    assert "elapsed_ms += wait_ms" in wait
    assert "ESP_ERR_NOT_FINISHED" in wait


def test_sntp_sample_and_commit_are_generation_ordered_under_the_time_lock():
    service = read("main/platform/time_service.c")
    accept = body(
        service,
        "static esp_err_t accept_sntp_system_time",
        "static esp_err_t certificate_state",
    )
    lock_at = accept.index("time_lock()")
    generation_at = accept.index("s_time_core.wall_generation != expected_generation")
    sample_at = accept.index("time(NULL)")
    commit_at = accept.index("d1l_time_core_set_wall_if_generation(")
    assert lock_at < generation_at < sample_at < commit_at
    assert "return ready ? ESP_OK : ESP_ERR_NOT_FINISHED" in accept
    wait = body(
        service,
        "esp_err_t d1l_time_service_wait_for_certificate_time",
        "esp_err_t d1l_time_service_set_companion_time",
    )
    refresh = wait.split("if (accept_ret == ESP_ERR_NOT_FINISHED)", 1)[1]
    assert "certificate_state(" in refresh
    assert "&expected_generation" in refresh


def test_wall_generation_saturation_cannot_reuse_a_stale_cas_token():
    core = read("main/platform/time_service_core.c")
    set_wall = body(
        core,
        "esp_err_t d1l_time_core_set_wall(",
        "esp_err_t d1l_time_core_set_wall_if_generation(",
    )
    assert "core->wall_generation == UINT32_MAX" in set_wall
    assert "return ESP_ERR_INVALID_STATE" in set_wall
    assert "core->wall_generation++;" in set_wall


def test_map_delegates_certificate_time_without_owning_sntp_or_wall_clock():
    store = read("main/storage/map_tile_store.c")
    fetch = body(
        store,
        "esp_err_t d1l_map_tile_store_fetch",
        "esp_err_t d1l_map_tile_store_write_canary",
    )
    assert '#include "platform/time_service.h"' in store
    assert "esp_netif_sntp" not in store
    assert "time(NULL)" not in store
    wait = "d1l_time_service_wait_for_certificate_time("
    assert fetch.index(wait) < fetch.index("esp_http_client_init(&config)")
    assert 'download_step(&result, "time_sync"' in fetch


def test_future_wall_persistence_timezone_and_transport_stay_owned_by_service():
    header = read("main/platform/time_service.h")
    assert "persist validated wall" in header
    assert "authenticated companion" in header
    assert "timezone/display conversion" in header
    assert "d1l_time_service_set_companion_time" in header
    assert "d1l_time_service_note_authenticated_lower_bound" in header


def test_lower_bound_cannot_replace_a_stronger_or_later_clock():
    core = read("main/platform/time_service_core.c")
    lower_bound = body(
        core,
        "esp_err_t d1l_time_core_note_authenticated_lower_bound",
        "esp_err_t d1l_time_core_next_protocol_timestamp",
    )
    assert "d1l_time_core_certificate_validity(core->wall_validity)" in lower_bound
    assert "current_wall >= epoch_sec" in lower_bound
    assert lower_bound.index("current_wall >= epoch_sec") < lower_bound.index(
        "D1L_TIME_VALIDITY_APPROXIMATE"
    )


def test_companion_failure_paths_cannot_mutate_system_time_before_service_lock():
    service = read("main/platform/time_service.c")
    companion = body(
        service,
        "esp_err_t d1l_time_service_set_companion_time",
        "esp_err_t d1l_time_service_note_authenticated_lower_bound",
    )
    init_at = companion.index("d1l_time_service_init()")
    lock_at = companion.index("time_lock()")
    system_clock_at = companion.index("settimeofday(&value, NULL)")
    service_truth_at = companion.index("d1l_time_core_set_wall(")
    assert init_at < lock_at < system_clock_at < service_truth_at
    failure = companion.split("settimeofday(&value, NULL) != 0", 1)[1].split(
        "const esp_err_t ret", 1
    )[0]
    assert "time_unlock();" in failure
