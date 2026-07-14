import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def canonical_sha256(relative: str) -> str:
    payload = (ROOT / relative).read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(payload).hexdigest()


def test_trace_helper_is_flags_zero_direct_and_terminal_only() -> None:
    trace = read("main/mesh/meshcore_trace.h")
    wire = read("main/mesh/meshcore_wire.h")

    assert "D1L_MESHCORE_PAYLOAD_TRACE 0x09U" in wire
    assert "D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS 30000U" in trace
    assert "D1L_MESHCORE_TRACE_DUPLICATE_WINDOW_MS 60000U" in trace
    assert 'D1L_MESHCORE_TRACE_RETAINED_TARGET "trace_last"' in trace
    assert "D1L_MESHCORE_ROUTE_DIRECT" in trace
    assert "source.raw[raw_len++] = 0U" in trace
    assert "D1L_MESHCORE_TRACE_FRAME_SOURCE" in trace
    assert "D1L_MESHCORE_TRACE_FRAME_IN_FLIGHT" in trace
    assert "D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED" in trace
    assert "D1L_MESHCORE_TRACE_FRAME_TERMINAL" in trace
    assert "packet.path_hops > explicit_hops" in trace
    assert "if (flags != 0U)" in trace
    assert "explicit_path_bytes == 0U" in trace
    assert "return D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED" in trace
    assert "d1l_meshcore_trace_classify(raw, raw_len, out_terminal) ==" in trace
    assert "D1L_MESHCORE_ROUTE_FLOOD" not in trace
    assert "D1L_MESHCORE_ROUTE_TRANSPORT" not in trace
    assert "*out_source = source" in trace
    assert "*out_terminal = terminal" in trace


def test_trace_tracker_correlates_immutable_tag_auth_path_and_bounds_age() -> None:
    trace = read("main/mesh/meshcore_trace.h")

    assert "tracker->pending_tag = tag" in trace
    assert "tracker->pending_auth_code = auth_code" in trace
    assert "memcpy(tracker->pending_path_hashes, path_hashes, path_hops)" in trace
    assert "D1L_MESHCORE_TRACE_CORRELATION_AUTH_MISMATCH" in trace
    assert "D1L_MESHCORE_TRACE_CORRELATION_PATH_MISMATCH" in trace
    assert "D1L_MESHCORE_TRACE_CORRELATION_DUPLICATE" in trace
    assert "D1L_MESHCORE_TRACE_CORRELATION_EXPIRED" in trace
    assert "tracker->last_result = *terminal" in trace
    assert "(uint32_t)(now_ms - tracker->pending_started_ms)" in trace
    assert "(uint32_t)(now_ms - tracker->completed_at_ms)" in trace


def test_service_retains_only_a_matched_trace_result() -> None:
    service = read("main/mesh/meshcore_service.c")
    receive = service.split("static void parse_rx_trace_packet", 1)[1].split(
        "static bool verify_advert_signature", 1
    )[0]

    assert "d1l_meshcore_trace_classify" in receive
    assert "d1l_meshcore_trace_tracker_consume" in receive
    assert "s_status.trace_rx_malformed++" in receive
    assert "s_status.trace_rx_source_ignored++" in receive
    assert "s_status.trace_rx_in_flight_ignored++" in receive
    assert "s_status.trace_rx_unsupported++" in receive
    assert "s_status.trace_pending_expired++" in receive
    assert "s_status.trace_rx_duplicates++" in receive
    assert "s_status.trace_rx_expired++" in receive
    assert "s_status.trace_rx_unmatched++" in receive
    assert "s_status.trace_rx_auth_mismatch++" in receive
    assert "s_status.trace_rx_path_mismatch++" in receive
    matched_guard = (
        "if (correlation != D1L_MESHCORE_TRACE_CORRELATION_MATCHED)"
    )
    assert matched_guard in receive
    assert receive.index(matched_guard) < receive.index(
        "d1l_route_store_upsert_observation("
    )
    assert receive.index(matched_guard) < receive.index(
        "const bool packet_retained = append_packet_log("
    )
    terminal_guard = "if (frame_kind != D1L_MESHCORE_TRACE_FRAME_TERMINAL)"
    assert terminal_guard in receive
    assert receive.index(terminal_guard) < receive.index(
        "d1l_meshcore_trace_tracker_consume"
    )
    assert receive.index(terminal_guard) < receive.index(
        "d1l_route_store_upsert_observation("
    )
    assert "s_trace_last_retention_attempted = true" in receive
    assert "s_trace_last_route_summary_accepted = route_ret == ESP_OK" in receive
    assert "s_trace_last_packet_preview_retained = packet_retained" in receive
    assert "d1l_meshcore_trace_retained_target_for_tag(terminal.tag)" in receive
    assert '"trace_%08lX"' not in receive
    assert '"trace_reply"' in receive
    assert '"Explicit TRACE"' in receive


def test_service_sends_only_an_explicit_nonempty_loop_without_contact_lookup() -> None:
    service = read("main/mesh/meshcore_service.c")
    send = service.split(
        "esp_err_t d1l_meshcore_service_send_trace_loop", 1
    )[1].split("const char *d1l_meshcore_service_state_name", 1)[0]

    assert "!path_hashes || path_hops == 0U" in send
    assert "d1l_meshcore_trace_build_source" in send
    assert "d1l_meshcore_trace_tracker_expire_pending" in send
    assert "d1l_meshcore_trace_tracker_begin" in send
    assert "d1l_meshcore_trace_tracker_cancel" in send
    assert "meshcore_service_send_raw" in send
    assert "d1l_route_store_upsert_observation_volatile" in send
    assert '"trace_request"' in send
    assert "d1l_contact_store_find_by_fingerprint" not in send
    assert "D1L_MESHCORE_ROUTE_FLOOD" not in send
    assert send.index("d1l_meshcore_trace_tracker_begin") < send.index(
        "meshcore_service_send_raw"
    )
    assert send.index("meshcore_service_send_raw") < send.index(
        "s_status.trace_tx_queued++"
    )


def test_trace_snapshot_is_passive_and_does_not_expose_auth_code() -> None:
    service = read("main/mesh/meshcore_service.c")
    header = read("main/mesh/meshcore_service.h")
    snapshot = service.split(
        "void d1l_meshcore_service_trace_snapshot", 1
    )[1].split("esp_err_t d1l_meshcore_service_request_advert", 1)[0]

    assert "d1l_meshcore_trace_tracker_expire_pending" not in snapshot
    assert "snapshot.pending_expired" in snapshot
    assert "D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS" in snapshot
    assert "snapshot.last_retention_attempted" in snapshot
    assert "snapshot.last_route_summary_accepted" in snapshot
    assert "snapshot.last_packet_preview_retained" in snapshot
    snapshot_type = header.split("typedef struct {", 2)[2].split(
        "} d1l_meshcore_trace_snapshot_t", 1
    )[0]
    assert "auth_code" not in snapshot_type


def test_console_discloses_trace_boundary_and_keeps_dm_path_probe_truthful() -> None:
    console = read("main/comms/usb_console.c")
    app = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")

    assert 'ok_begin("routes trace send")' in console
    assert 'ok_begin("routes trace status")' in console
    assert "routes trace send <loop-path-hex>" in console
    assert "routes trace status" in console
    assert "requires_explicit_loop" in console
    assert "contact_trace_supported" in console
    assert "hardware_verified" in console
    assert "per_hop_complete" in console
    assert "route_summary_durable_verified" in console
    assert "packet_preview_retained" in console
    assert "trace_rx_source_ignored" in console
    assert "trace_rx_in_flight_ignored" in console
    assert "trace_rx_unsupported" in console
    assert "trace_pending_expired" in console
    assert 'retained_summary\\\":true' not in console
    assert "boot_local" in console
    assert "tag_auth_immutable_explicit_loop" in console
    assert "pending_auth" not in console
    assert console.index('strncmp(line, "routes trace send ", 18)') < console.index(
        'strncmp(line, "routes trace ", 13)'
    )
    assert console.index('strcmp(line, "routes trace status")') < console.index(
        'strncmp(line, "routes trace ", 13)'
    )
    assert "d1l_app_model_request_path_discovery_probe" in console
    assert "real_trace_packet" in console
    assert "DM/PATH discovery probe queued" in console
    assert "d1l_meshcore_service_request_path_discovery_probe" in app
    assert "DM/PATH discovery; not TRACE; no Public RF" in ui


def test_trace_helper_and_service_are_exact_production_binding_sources() -> None:
    manifest = json.loads(read("tests/meshcore_oracle/manifest.json"))
    runner = read("scripts/meshcore_conformance_d1l.py")
    allowlist = runner.split(
        "EXPECTED_ORACLE_PRODUCTION_BINDING_SOURCE_PATHS = {", 1
    )[1].split("}", 1)[0]

    for relative in (
        "main/mesh/meshcore_trace.h",
        "main/mesh/meshcore_service.c",
    ):
        assert relative in manifest["production_binding_sources"]
        assert manifest["production_binding_sources"][relative] == canonical_sha256(
            relative
        )
        assert f'"{relative}"' in allowlist
