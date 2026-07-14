import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def canonical_sha256(relative: str) -> str:
    payload = (ROOT / relative).read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(payload).hexdigest()


def test_production_dm_send_consumes_one_boot_proven_fail_closed_route_plan():
    service = read("main/mesh/meshcore_service.c")
    selector = read("main/mesh/meshcore_route_selection.h")
    send_dm = service.split(
        "static esp_err_t meshcore_service_send_dm_with_result", 1
    )[1].split("esp_err_t d1l_meshcore_service_send_dm", 1)[0]
    wrapper = service.split("esp_err_t d1l_meshcore_service_send_dm", 1)[1].split(
        "esp_err_t d1l_meshcore_service_request_path_discovery_probe", 1
    )[0]
    path_rx = service.split("static void parse_rx_path_packet", 1)[1].split(
        "static bool verify_advert_signature", 1
    )[0]
    initialization = service.split("void d1l_meshcore_service_init", 1)[1].split(
        "esp_err_t d1l_meshcore_service_ensure_identity", 1
    )[0]

    assert '#include "mesh/meshcore_route_selection.h"' in service
    assert "D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS (30U * 60U * 1000U)" in selector
    assert "bool learned_this_boot" in selector
    assert "D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH" in selector
    assert "d1l_meshcore_wire_path_len_valid(learned_path_len)" in selector
    assert "selection.path_age_ms >" in selector
    assert "*out_selection = selection;" in selector
    assert "lookup_boot_route(contact.fingerprint, contact.out_path" in send_dm
    assert "contact.out_path_updated_ms" not in send_dm
    assert "route_learned_this_boot" in send_dm
    assert "d1l_meshcore_route_select(" in send_dm
    assert "build_dm_text_packet(settings, &contact, text, &selection" in send_dm
    assert "const bool use_direct = contact.out_path_valid" not in send_dm
    assert "route_name(selection.route)" in send_dm
    assert '"dm_text"' in send_dm
    assert "record_dm_route_selection(&selection)" in send_dm
    assert "d1l_meshcore_route_selection_reason_name(selection.reason)" in send_dm
    assert send_dm.index("record_dm_route_selection(&selection)") > send_dm.index(
        "ret = meshcore_service_send_raw"
    )
    assert "meshcore_service_send_dm_with_result(fingerprint, text, NULL)" in wrapper

    assert "d1l_contact_store_update_path(" in path_rx
    assert "remember_boot_route(" in path_rx
    assert path_rx.index("remember_boot_route(") > path_rx.index(
        "d1l_contact_store_update_path("
    )
    assert "clear_boot_routes();" in initialization


def test_path_probe_records_only_the_immutable_actual_send_result():
    service = read("main/mesh/meshcore_service.c")
    trace = service.split(
        "esp_err_t d1l_meshcore_service_request_path_discovery_probe", 1
    )[1].split("esp_err_t d1l_meshcore_service_send_trace_loop", 1)[0]

    assert "d1l_dm_send_result_t send_result" in trace
    assert "meshcore_service_send_dm_with_result(" in trace
    assert "send_result.selection.route" in trace
    assert "send_result.selection.path_hash_bytes" in trace
    assert "send_result.selection.path_hops" in trace
    assert "send_result.raw_len" in trace
    assert "d1l_contact_store_find_by_fingerprint" not in trace
    assert "contact.out_path_valid" not in trace
    assert trace.index("meshcore_service_send_dm_with_result(") < trace.index(
        "d1l_route_store_upsert_observation("
    )


def test_inbound_dm_ack_consumes_the_same_immutable_route_selection():
    service = read("main/mesh/meshcore_service.c")
    dm_parse = service.split("static bool parse_rx_dm_packet", 1)[1].split(
        "static void record_dm_ack", 1
    )[0]
    ack_planning = dm_parse.split("uint8_t ack_hash_bytes[4]", 1)[1].split(
        "ack_build_ret = planned", 1
    )[0]

    assert "lookup_boot_route(contact->fingerprint, contact->out_path" in ack_planning
    assert "d1l_meshcore_route_select(" in ack_planning
    assert "d1l_meshcore_ack_dispatch_plan(" in ack_planning
    assert "ack_route_selection.route == D1L_MESHCORE_ROUTE_DIRECT" in ack_planning
    assert "ack_direct ? ack_route_selection.path : NULL" in ack_planning
    assert "ack_direct ? ack_route_selection.path_len : 0U" in ack_planning
    assert ack_planning.index("d1l_meshcore_route_select(") < ack_planning.index(
        "d1l_meshcore_ack_dispatch_plan("
    )
    planner_call = ack_planning.split("d1l_meshcore_ack_dispatch_plan(", 1)[1]
    assert "contact->out_path_valid" not in planner_call
    assert "contact->out_path, contact->out_path_len" not in planner_call


def test_route_selection_telemetry_distinguishes_every_fallback_reason():
    service = read("main/mesh/meshcore_service.c")
    header = read("main/mesh/meshcore_service.h")
    console = read("main/comms/usb_console.c")

    for field in (
        "dm_route_direct_selected",
        "dm_route_flood_selected",
        "dm_route_missing_fallback",
        "dm_route_preboot_fallback",
        "dm_route_stale_fallback",
        "dm_route_malformed_fallback",
        "dm_route_last_path_age_ms",
        "dm_route_last_reason",
    ):
        assert field in header
        assert f"s_status.{field}" in service

    assert r'\"dm_route\":{' in console
    assert r'\"direct_selected\":%lu' in console
    assert r'\"flood_selected\":%lu' in console
    assert r'\"missing_fallback\":%lu' in console
    assert r'\"preboot_fallback\":%lu' in console
    assert r'\"stale_fallback\":%lu' in console
    assert r'\"malformed_fallback\":%lu' in console
    assert r'\"last_reason\":\"%s\"' in console
    assert r'\"last_path_age_ms\":%lu' in console


def test_route_selector_is_an_exact_pinned_production_binding_source():
    manifest = json.loads(read("tests/meshcore_oracle/manifest.json"))
    runner = read("scripts/meshcore_conformance_d1l.py")
    relative = "main/mesh/meshcore_route_selection.h"

    assert relative in manifest["production_binding_sources"]
    assert manifest["production_binding_sources"][relative] == canonical_sha256(relative)
    allowlist = runner.split(
        "EXPECTED_ORACLE_PRODUCTION_BINDING_SOURCE_PATHS = {", 1
    )[1].split("}", 1)[0]
    assert f'"{relative}"' in allowlist
