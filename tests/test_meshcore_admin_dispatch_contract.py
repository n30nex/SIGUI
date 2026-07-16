import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def canonical_sha256(relative: str) -> str:
    content = (ROOT / relative).read_text(encoding="utf-8")
    canonical = content.replace("\r\n", "\n").encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


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
    cleanup_branches = (
        ("if (!d1l_meshcore_path_plain_decode", "uint8_t packet_hash["),
        ("if (packet_hash_ready && d1l_meshcore_packet_hash_cache_contains", "uint8_t replay_identity["),
        ("esp_err_t ret = calc_path_replay_identity", "if (!d1l_meshcore_path_replay_take"),
        ("if (!d1l_meshcore_path_replay_take", "d1l_contact_entry_t learned_contact"),
        ("ret = d1l_contact_store_update_path_from_source", "} else {"),
    )
    for start, end in cleanup_branches:
        branch = before_cleanup[before_cleanup.index(start):]
        branch = branch[:branch.index(end)]
        assert "goto path_candidate_cleanup;" in branch
    assert before_cleanup.count("goto path_candidate_cleanup;") == len(cleanup_branches)
    assert "continue;" not in before_cleanup
    assert "return;" not in before_cleanup
    assert cleanup.count("secure_zero_bytes(plain, sizeof(plain));") == 1
    assert cleanup.index("secure_zero_bytes(plain, sizeof(plain));") < \
        cleanup.index("if (continue_after_cleanup)") < cleanup.index("return;")

    status = body(
        service,
        "static esp_err_t meshcore_service_handle_admin_request_status",
        "static esp_err_t meshcore_service_handle_admin_logout",
    )
    assert status.index("d1l_meshcore_admin_runtime_validate_binding") < \
        status.index("d1l_meshcore_admin_build_status_packet")
    assert status.index("d1l_meshcore_admin_runtime_begin_status") < \
        status.index("meshcore_service_handle_send_raw")
    assert status.index("d1l_meshcore_admin_runtime_begin_status") < \
        status.index("d1l_meshcore_admin_binding_wipe(&current)") < \
        status.index("meshcore_service_handle_send_raw")
    assert status.index("d1l_meshcore_admin_runtime_begin_status") < \
        status.index("d1l_meshcore_admin_context_wipe(&context)") < \
        status.index("meshcore_service_handle_send_raw")
    assert "meshcore_service_send_raw(" not in status


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

    login = body(
        service,
        "static esp_err_t meshcore_service_handle_admin_login",
        "static esp_err_t meshcore_service_handle_admin_request_status",
    )
    assert login.index("d1l_meshcore_admin_runtime_begin_login") < \
        login.index("meshcore_service_handle_send_raw")
    assert login.index("d1l_meshcore_admin_runtime_begin_login") < \
        login.index("d1l_meshcore_admin_binding_wipe(&binding)") < \
        login.index("meshcore_service_handle_send_raw")
    assert login.index("d1l_meshcore_admin_build_login_packet") < \
        login.index("secure_zero_bytes(password, sizeof(password))") < \
        login.index("d1l_meshcore_admin_runtime_begin_login")
    assert "d1l_meshcore_admin_runtime_note_login_tx" in login
    assert "append_packet_log" not in login
    assert "meshcore_service_send_raw(" not in login


def test_admin_commands_and_secrets_are_owned_by_the_mesh_runtime_task() -> None:
    service = read("main/mesh/meshcore_service.c")
    header = read("main/mesh/meshcore_service.h")
    command_guard = read("main/mesh/meshcore_command_guard.h")
    command = body(
        service,
        "typedef struct {\n    d1l_meshcore_service_cmd_type_t type;",
        "} d1l_meshcore_service_cmd_t;",
    )
    slots = body(
        service,
        "typedef struct {\n    d1l_mesh_command_request_t request;",
        "} d1l_meshcore_request_slot_t;",
    )
    task = body(service, "static void meshcore_service_task",
                "static esp_err_t meshcore_service_start_task")
    public_login = body(
        service,
        "esp_err_t d1l_meshcore_service_admin_login",
        "esp_err_t d1l_meshcore_service_admin_request_status",
    )
    public_status = body(
        service,
        "esp_err_t d1l_meshcore_service_admin_request_status",
        "esp_err_t d1l_meshcore_service_admin_logout",
    )
    public_logout = body(
        service,
        "esp_err_t d1l_meshcore_service_admin_logout",
        "esp_err_t d1l_meshcore_service_request_advert",
    )

    for command_name in (
        "D1L_MESHCORE_SERVICE_CMD_ADMIN_LOGIN",
        "D1L_MESHCORE_SERVICE_CMD_ADMIN_REQUEST_STATUS",
        "D1L_MESHCORE_SERVICE_CMD_ADMIN_LOGOUT",
    ):
        assert command_name in task
    admission_drop = task[
        task.index("if (!meshcore_request_admit(&cmd))"):
        task.index("esp_err_t ret = ESP_ERR_INVALID_ARG;")
    ]
    assert admission_drop.index("meshcore_service_command_wipe(&cmd);") < \
        admission_drop.index("continue;")
    assert task.rindex("meshcore_service_command_wipe(&cmd);") > \
        task.index("meshcore_service_reply(&cmd, ret);")
    assert "admin_password" not in command
    assert "d1l_mesh_command_request_t request;" in slots
    assert "admin_password[D1L_MESH_COMMAND_ADMIN_PASSWORD_CAPACITY]" in \
        command_guard
    assert "admin_password_present" in command_guard
    assert '#include "mesh/meshcore_command_guard.h"' in service
    assert "d1l_meshcore_admin_runtime_init();" in task
    assert "d1l_meshcore_admin_runtime_init();" not in body(
        service, "void d1l_meshcore_service_init",
        "esp_err_t d1l_meshcore_service_ensure_identity",
    )

    assert "meshcore_service_send_admin_login_command" in public_login
    assert "d1l_meshcore_admin_runtime_begin_login" not in public_login
    assert "d1l_settings_next_mesh_timestamp" not in public_login
    assert "meshcore_service_send_command" in public_status
    assert "d1l_meshcore_admin_runtime_begin_status" not in public_status
    assert "meshcore_service_send_command" in public_logout
    assert "d1l_meshcore_admin_runtime_logout" not in public_logout
    assert "esp_err_t d1l_meshcore_service_admin_logout(void);" in header


def test_admin_request_timeout_saturation_and_zeroization_fail_closed() -> None:
    service = read("main/mesh/meshcore_service.c")
    command_guard = read("main/mesh/meshcore_command_guard.h")
    secret_store = body(
        service,
        "static esp_err_t meshcore_request_store_admin_password",
        "static bool meshcore_request_take_admin_password",
    )
    secret_take = body(
        service,
        "static bool meshcore_request_take_admin_password",
        "static esp_err_t meshcore_request_consume",
    )
    wait = body(
        service,
        "static esp_err_t meshcore_request_wait",
        "static void meshcore_request_abort_unqueued",
    )
    admit = body(service, "static bool meshcore_request_admit",
                 "static void meshcore_request_complete")
    send_claimed = body(
        service,
        "static esp_err_t meshcore_service_send_claimed_command",
        "static esp_err_t meshcore_service_send_command",
    )
    enqueue = body(
        service,
        "static bool meshcore_service_enqueue_claimed",
        "static esp_err_t meshcore_service_send_claimed_command",
    )
    send_login = body(
        service,
        "static esp_err_t meshcore_service_send_admin_login_command",
        "esp_err_t d1l_meshcore_service_start_rx_async",
    )
    login_handler = body(
        service,
        "static esp_err_t meshcore_service_handle_admin_login",
        "static esp_err_t meshcore_service_handle_admin_request_status",
    )

    assert "password_len > D1L_MESHCORE_ADMIN_MAX_PASSWORD_BYTES" in secret_store
    assert "d1l_mesh_command_request_store_admin_password" in secret_store
    assert "d1l_mesh_command_request_take_admin_password" in secret_take
    assert "d1l_mesh_command_request_expire" in admit
    assert "d1l_mesh_command_request_admit" in admit
    assert "d1l_mesh_request_guard_release(" not in admit
    assert "d1l_mesh_command_request_enqueue" in send_claimed
    assert "xQueueSend(enqueue_context->queue, command" in enqueue
    assert "runtime_note_command_saturation(false);" in send_claimed
    assert "D1L_MESH_COMMAND_ENQUEUE_SATURATED" in send_claimed
    assert "meshcore_service_called_from_owner()" in send_login
    assert "meshcore_service_command_wipe(cmd);" in send_login
    assert "d1l_mesh_command_request_cancel_and_release" in wait
    assert "meshcore_request_take_admin_password" in login_handler
    assert "secure_zero_bytes(password, sizeof(password));" in login_handler
    assert "d1l_meshcore_admin_binding_wipe(&binding);" in login_handler
    assert "secure_zero_bytes(&raw_cmd, sizeof(raw_cmd));" in login_handler

    cancel_guard = body(
        command_guard,
        "static inline bool d1l_mesh_command_request_cancel_and_release",
        "static inline bool d1l_mesh_command_request_expire",
    )
    expire_guard = body(
        command_guard,
        "static inline bool d1l_mesh_command_request_expire",
        "static inline bool d1l_mesh_command_request_admit",
    )
    take_guard = body(
        command_guard,
        "static inline bool d1l_mesh_command_request_take_admin_password",
        "static inline bool d1l_mesh_command_request_cancel_and_release",
    )
    queue_guard = body(
        command_guard,
        "d1l_mesh_command_request_enqueue(",
        "static inline bool d1l_mesh_command_sync_wait_allowed",
    )
    assert cancel_guard.index("D1L_MESH_REQUEST_CALLER_CANCELLED") < \
        cancel_guard.index("d1l_mesh_command_request_wipe_admin_password") < \
        cancel_guard.index("d1l_mesh_request_guard_release")
    assert expire_guard.index("D1L_MESH_REQUEST_OWNER_EXPIRED") < \
        expire_guard.index("d1l_mesh_command_request_wipe_admin_password")
    assert take_guard.index("memcpy(out_password") < \
        take_guard.index("d1l_mesh_command_request_wipe_admin_password")
    assert "d1l_mesh_command_request_cancel_and_release" in queue_guard


def test_owner_maintenance_expires_idle_absolute_and_request_deadlines() -> None:
    service = read("main/mesh/meshcore_service.c")
    maintenance = body(
        service, "static void meshcore_service_run_owner_maintenance",
        "static void meshcore_service_handle_radio_rx_done",
    )
    assert "d1l_meshcore_admin_runtime_expire(now_us)" in maintenance


def test_admin_command_guards_are_exact_production_binding_sources() -> None:
    manifest = json.loads(read("tests/meshcore_oracle/manifest.json"))
    runner = read("scripts/meshcore_conformance_d1l.py")
    allowlist = runner.split(
        "EXPECTED_ORACLE_PRODUCTION_BINDING_SOURCE_PATHS = {", 1
    )[1].split("}", 1)[0]

    for relative in (
        "main/mesh/meshcore_command_guard.h",
        "main/mesh/meshcore_runtime_guard.h",
        "main/mesh/meshcore_service.c",
        "main/mesh/meshcore_service.h",
    ):
        assert manifest["production_binding_sources"][relative] == \
            canonical_sha256(relative)
        assert f'"{relative}"' in allowlist
