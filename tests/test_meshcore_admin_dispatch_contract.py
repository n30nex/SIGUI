from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def body(source: str, start: str, end: str) -> str:
    return source[source.index(start):source.index(end, source.index(start))]


def test_admin_protocol_and_runtime_are_separate_production_modules() -> None:
    cmake = read("main/CMakeLists.txt")
    dispatch_h = read("main/mesh/meshcore_admin_dispatch.h")
    dispatch_c = read("main/mesh/meshcore_admin_dispatch.c")
    runtime = read("main/mesh/meshcore_admin_runtime.c")
    for source in (
        "mesh/meshcore_admin_dispatch.c",
        "mesh/meshcore_admin_runtime.c",
        "mesh/meshcore_identity_exchange.c",
    ):
        assert f'"{source}"' in cmake
    assert "D1L_MESHCORE_ADMIN_REPLAY_PEER_CAPACITY 8U" in dispatch_h
    assert "D1L_MESHCORE_ADMIN_REPLAY_RESPONSES_PER_PEER 4U" in dispatch_h
    assert "request_deadline_us" in dispatch_h
    assert "idle_deadline_us" in dispatch_h
    assert "absolute_deadline_us" in dispatch_h
    assert "now_us >= deadline_us" in dispatch_c
    assert "refresh_idle_deadline(session, now_us)" in dispatch_c
    assert "replay_cache_contains" in dispatch_c
    assert "entry->responses[response_index]" in dispatch_c
    assert "entry->next_response" in dispatch_c
    assert "d1l_meshcore_admin_timeout(session);" in dispatch_c
    assert "role != D1L_MESHCORE_ADMIN_ROLE_REPEATER" in dispatch_c
    assert "no-history" in runtime


def test_runtime_snapshot_redacts_all_authority_material() -> None:
    runtime_h = read("main/mesh/meshcore_admin_runtime.h")
    snapshot = body(
        runtime_h,
        "typedef struct {\n    d1l_meshcore_admin_state_t state;",
        "} d1l_meshcore_admin_runtime_snapshot_t;",
    )
    for forbidden in (
        "session_secret", "peer_public_key", "local_public_key", "password"
    ):
        assert forbidden not in snapshot


def test_service_revalidates_every_live_admin_binding_and_zeroizes_copies() -> None:
    service = read("main/mesh/meshcore_service.c")
    direct = body(
        service,
        "static bool parse_rx_admin_response_packet",
        "static void parse_rx_channel_packet",
    )
    for required in (
        "d1l_meshcore_admin_runtime_capture_pending",
        "d1l_contact_store_find_by_fingerprint",
        "d1l_meshcore_admin_role_for_contact",
        "derive_local_identity_shared_secret",
        "d1l_meshcore_admin_runtime_validate_binding",
        "meshcore_decrypt_after_mac",
        "d1l_meshcore_admin_binding_wipe",
        "d1l_meshcore_admin_context_wipe",
        "secure_zero_bytes(plaintext, sizeof(plaintext))",
    ):
        assert required in direct
    derive_failure = body(
        direct,
        "const esp_err_t derive_ret = derive_local_identity_shared_secret(",
        "if (!d1l_meshcore_admin_runtime_validate_binding(",
    )
    assert "if (derive_ret != ESP_OK)" in derive_failure
    assert "d1l_meshcore_admin_runtime_invalidate(ESP_ERR_INVALID_STATE)" in \
        derive_failure
    assert derive_failure.index("d1l_meshcore_admin_runtime_invalidate") < \
        derive_failure.index("d1l_meshcore_admin_binding_wipe")

    path_rx = body(service, "static void parse_rx_path_packet",
                   "static void parse_rx_trace_packet")
    dispatch = path_rx[path_rx.index("admin_dispatch_plain_response("):
                       path_rx.index("admin_dispatch_plain_response(") + 350]
    assert "settings->identity_public_key" in dispatch
    assert "secret" in dispatch
    assert "if (!admin_response_considered)" in path_rx
    decrypted = path_rx[path_rx.index("uint8_t plain["):]
    before_cleanup, cleanup = decrypted.split("path_candidate_cleanup:", 1)
    assert before_cleanup.count("goto path_candidate_cleanup;") == 3
    assert "continue;" not in before_cleanup
    assert "return;" not in before_cleanup
    assert cleanup.count("secure_zero_bytes(plain, sizeof(plain));") == 1
    assert cleanup.index("secure_zero_bytes(plain, sizeof(plain));") < \
        cleanup.index("if (continue_after_cleanup)") < cleanup.index("return;")

    status = body(
        service,
        "esp_err_t d1l_meshcore_service_admin_request_status",
        "void d1l_meshcore_service_admin_logout",
    )
    assert status.index("d1l_meshcore_admin_runtime_validate_binding") < \
        status.index("d1l_meshcore_admin_build_status_packet")
    assert status.index("d1l_meshcore_admin_runtime_begin_status") < \
        status.index("meshcore_service_send_raw")


def test_outbound_admin_uses_canonical_wire_crypto_and_reserved_state() -> None:
    runtime = read("main/mesh/meshcore_admin_runtime.c")
    service = read("main/mesh/meshcore_service.c")
    login_builder = body(
        runtime, "esp_err_t d1l_meshcore_admin_build_login_packet",
        "esp_err_t d1l_meshcore_admin_build_status_packet",
    )
    for required in (
        "d1l_meshcore_wire_write_prefix", "derive_secret(",
        "d1l_meshcore_admin_encode_login_request", "encrypt(",
        "d1l_meshcore_admin_secure_zero(plaintext",
    ):
        assert required in login_builder
    status_builder = body(
        runtime, "esp_err_t d1l_meshcore_admin_build_status_packet",
        "static void clear_session_locked",
    )
    assert "d1l_meshcore_admin_encode_status_request" in status_builder
    assert "d1l_meshcore_wire_write_prefix" in status_builder
    assert "encrypt(" in status_builder

    login = body(service, "esp_err_t d1l_meshcore_service_admin_login",
                 "esp_err_t d1l_meshcore_service_admin_request_status")
    assert login.index("d1l_meshcore_admin_runtime_begin_login") < \
        login.index("meshcore_service_send_raw")
    assert "d1l_meshcore_admin_runtime_note_login_tx" in login
    assert "append_packet_log" not in login


def test_owner_maintenance_expires_idle_absolute_and_request_deadlines() -> None:
    service = read("main/mesh/meshcore_service.c")
    maintenance = body(
        service, "static void meshcore_service_run_owner_maintenance",
        "static void meshcore_service_handle_radio_rx_done",
    )
    assert "d1l_meshcore_admin_runtime_expire(now_us)" in maintenance
