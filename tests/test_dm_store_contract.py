from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_dm_store_is_bounded_and_retained_blob_store_backed():
    header = read("main/mesh/dm_store.h")
    source = read("main/mesh/dm_store.c")
    blob_store = read("main/storage/retained_blob_store.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")
    assert "D1L_DM_STORE_CAPACITY 16U" in header
    assert "contact_fingerprint" in header
    assert "ack_hash" in header
    assert "d1l_dm_store_mark_acked" in header
    assert "d1l_dm_store_copy_recent_page" in header
    assert "d1l_dm_store_copy_thread_page" in header
    assert "d1l_dm_store_copy_thread" in header
    assert 'D1L_RETAINED_DM_MESSAGE_NAMESPACE "d1l_dms"' in blob_store
    assert 'D1L_RETAINED_DM_MESSAGE_SD_DIR "stores/messages/dm"' in blob_store
    assert "D1L_DM_STORE_ID D1L_RETAINED_BLOB_STORE_DM_MESSAGES" in source
    assert "D1L_DM_STORE_SCHEMA 6U" in source
    assert "D1L_DM_STORE_SCHEMA_V5 5U" in source
    assert "D1L_DM_STORE_SCHEMA_V4 4U" in source
    assert "D1L_DM_STORE_SCHEMA_V3 3U" in source
    assert "D1L_DM_STORE_SCHEMA_V2 2U" in source
    assert "D1L_DM_STORE_SCHEMA_V1 1U" in source
    assert "convert_v2_blob" in source
    assert "convert_v1_blob" in source
    assert "convert_v3_blob" in source
    assert "convert_v4_blob" in source
    assert "convert_v5_blob" in source
    assert "d1l_dm_entry_v5_t" in source
    assert "d1l_dm_store_blob_v5_t" in source
    assert "d1l_dm_entry_v4_t" in source
    assert "d1l_dm_store_blob_v4_t" in source
    assert "D1L_DM_IDENTITY_DIGEST_BYTES 32U" in header
    assert "identity_digest_valid" in header
    assert "ack_dispatch_count" in header
    assert "ack_dispatch_kind" in header
    assert "ack_state" in header
    assert "ack_last_error" in header
    assert "d1l_dm_store_append_rx_identity" in header
    assert "d1l_dm_store_reserve_ack_dispatch" in header
    assert "d1l_dm_store_complete_ack_dispatch" in header
    assert "s_clear_lineage" in source
    assert "new_clear_lineage" in source
    assert "esp_random()" in source
    assert "s_device_lineage_authoritative" in source
    assert "clear_lineage" in header
    assert "bool loaded" in header
    assert ".loaded = s_loaded" in source
    assert 'D1L_DM_STORE_KEY "threads"' in source
    assert '#include "storage/retained_blob_store.h"' in source
    assert "d1l_retained_blob_store_backend_state(D1L_DM_STORE_ID" in source
    assert "d1l_retained_blob_store_read_sd_primary(" in source
    assert "d1l_retained_blob_store_read_nvs_fallback(" in source
    assert "d1l_retained_blob_store_write_sd_primary_guarded(" in source
    assert "d1l_retained_blob_store_write_nvs_fallback(" in source
    assert "d1l_retained_blob_store_write(D1L_DM_STORE_ID" not in source
    assert "d1l_retained_blob_store_erase(D1L_DM_STORE_ID" not in source
    assert "d1l_retained_blob_store_erase_sd_primary" not in source
    assert "d1l_retained_blob_store_erase_nvs_fallback" not in source
    assert "s_last_sd_backend_generation" in source
    assert "backend_generation_matches" in source
    assert "reconcile_sd_primary" in source
    assert "s_sd_reconcile_pending" in source
    assert "s_sd_primary_dirty" in source
    assert "s_nvs_fallback_dirty" in source
    assert "const bool same_revision" in source
    assert "d1l_dm_store_flush" in header
    assert "d1l_dm_store_flush_if_due" in header
    assert "static d1l_store_lock_t s_init_recovery_lock" in source
    assert "d1l_store_lock_take(&s_init_recovery_lock)" in source
    assert "persistence_dirty" in header
    assert "sd_primary_reconcile_pending" in header
    assert "nvs_fallback_dirty" in header
    assert "nvs_get_blob" not in source
    assert "nvs_set_blob" not in source
    assert "entry->acked = next_state == D1L_DM_DELIVERY_ACKNOWLEDGED" in source
    assert "path_hash_bytes > 3U" in source
    assert "path_hops > 63U" in source
    assert "(uint16_t)path_hash_bytes * path_hops > 64U" in source
    assert "blob->head != blob->count % D1L_DM_STORE_CAPACITY" in source
    assert "blob->entries[i].seq <= previous_seq" in source
    assert "skip_newest" in source
    assert "out_total_matches" in source
    assert "d1l_dm_store_copy_thread" in source
    assert "strncmp(entry->contact_fingerprint, contact_fingerprint" in source
    assert "static d1l_dm_store_raw_blob_t s_raw_scratch" in source
    assert "static d1l_dm_persist_snapshot_t s_persist_snapshot" in source
    assert "static d1l_dm_entry_t s_merge_entries" in source
    assert "static d1l_dm_entry_t s_overlay_entries" in source
    assert '"mesh/dm_store.c"' in cmake
    assert "d1l_dm_store_init()" in app_main


def test_outbound_delivery_state_is_persisted_cas_guarded_and_reboot_safe():
    header = read("main/mesh/dm_store.h")
    source = read("main/mesh/dm_store.c")
    state_h = read("main/mesh/dm_delivery_state.h")
    state_c = read("main/mesh/dm_delivery_state.c")
    cmake = read("main/CMakeLists.txt")

    for state in (
        "QUEUED",
        "WAITING_RADIO",
        "TX_ACTIVE",
        "TX_DONE",
        "AWAITING_ACK",
        "ACKNOWLEDGED",
        "RETRY_WAIT",
        "RETRY_TX",
        "FAILED_RADIO",
        "FAILED_TIMEOUT",
        "FAILED_QUEUE",
        "INTERRUPTED_BY_REBOOT",
        "CANCELLED",
    ):
        assert f"D1L_DM_DELIVERY_{state}" in state_h

    assert "d1l_dm_delivery_transition_allowed" in state_h
    assert "d1l_dm_delivery_interrupted_by_reboot" in state_h
    assert "from == to" in state_c
    assert "from != D1L_DM_DELIVERY_FAILED_QUEUE" in state_c
    assert "delivery_state" in header
    assert "uint64_t delivery_session_id" in header
    assert "delivery_reason" in header
    assert "delivery_last_error" in header
    assert "delivery_retry_count" in header
    assert "delivery_revision" in header
    assert "d1l_dm_store_transition_delivery" in header
    assert "d1l_dm_store_append_tx" in header
    assert "uint32_t expected_delivery_revision" in header
    assert "entry->delivery_state != expected_state" in source
    assert "entry->delivery_revision != expected_delivery_revision" in source
    assert "apply_delivery_transition_locked" in source
    assert "normalize_interrupted_delivery_locked" in source
    assert "D1L_DM_DELIVERY_REASON_REBOOT_RECOVERY" in source
    assert "initialize_legacy_delivery_metadata" in source
    assert "D1L_DM_DELIVERY_REASON_LEGACY_IMPORT" in source
    assert "entry.delivered = acked;" in source
    assert "migration_delivery_session_id" in source
    migration = source.split("static uint64_t migration_delivery_session_id", 1)[
        1
    ].split("static bool delivery_reason_matches_target", 1)[0]
    assert "esp_random" not in migration
    transition = source.split("static esp_err_t transition_delivery_internal(", 1)[
        1
    ].split("esp_err_t d1l_dm_store_mark_acked", 1)[0]
    assert "s_entries[i].delivery_session_id == delivery_session_id" in transition
    assert "s_entries[i].seq ==" not in transition
    assert "outcome->row_seq = entry->seq" in transition
    assert "outcome->delivery_revision = entry->delivery_revision" in transition
    assert "entry->delivered = entry->acked" in source
    assert "entry->attempt++" in source
    assert "entry->attempt == UINT8_MAX" in source
    assert "d1l_dm_store_transition_delivery_retry" in header
    assert "entry->ack_hash = retry_ack_hash" in transition
    assert "blob->entries[previous].delivery_session_id" in source
    assert '"mesh/dm_delivery_state.c"' in cmake


def test_full_ring_never_evicts_a_nonterminal_outbound_delivery():
    source = read("main/mesh/dm_store.c")
    append = source.split("static esp_err_t append_internal(", 1)[1].split(
        "esp_err_t d1l_dm_store_append(", 1
    )[0]

    full = append.index("s_count == D1L_DM_STORE_CAPACITY")
    head = append.index("&s_entries[s_head]", full)
    outbound = append.index('strncmp(oldest->direction, "tx"', head)
    terminal = append.index("d1l_dm_delivery_state_terminal", outbound)
    no_mem = append.index("return ESP_ERR_NO_MEM", terminal)
    session = append.index("new_delivery_session_id_locked", no_mem)
    insert = append.index("s_entries[s_head] = entry", session)
    assert full < head < outbound < terminal < no_mem < session < insert
    assert "outcome->error = ESP_ERR_NO_MEM" in append[terminal:no_mem]


def test_meshcore_service_builds_private_text_packets_from_contacts():
    source = read("main/mesh/meshcore_service.c")
    wire = read("main/mesh/meshcore_wire.h")
    cmake = read("main/CMakeLists.txt")
    assert "D1L_MESHCORE_PAYLOAD_TEXT 0x02U" in wire
    assert "D1L_MESHCORE_PAYLOAD_ACK 0x03U" in wire
    assert "D1L_MESHCORE_PAYLOAD_PATH 0x08U" in wire
    assert "D1L_MESHCORE_PAYLOAD_MULTIPART 0x0AU" in wire
    assert "D1L_MESHCORE_HEADER_DM_TEXT_FLOOD" in source
    assert "D1L_MESHCORE_HEADER_DM_TEXT_DIRECT" in source
    assert "ed25519_key_exchange(secret, dest_pub, settings->identity_private_key)" in source
    assert "ed25519_key_exchange(secret, sender_pub, settings->identity_private_key)" in source
    assert "build_dm_text_packet" in source
    assert "calc_dm_ack_hash" in source
    assert "parse_rx_ack_packet" in source
    assert "parse_rx_path_packet" in source
    assert "record_dm_ack" in source
    assert "d1l_contact_store_find_by_fingerprint" in source
    assert "d1l_contact_store_update_path" in source
    assert "d1l_dm_store_append" in source
    assert "d1l_dm_store_transition_delivery" in source
    assert "contact.out_path_valid" in source
    assert '"tx", "dm_text", 0, 0' in source
    assert 'append_packet_log("rx", "dm_text"' in source
    assert 'append_packet_log("rx", "dm_ack"' in source
    assert 'append_packet_log("rx", "path_return"' in source
    assert "../third_party/MeshCore/lib/ed25519/key_exchange.c" in cmake


def test_meshcore_service_durably_enqueues_dm_before_radio_start():
    source = read("main/mesh/meshcore_service.c")
    send_start = source.index("static esp_err_t meshcore_service_handle_send_dm")
    send_body = source[
        send_start:source.index("static void meshcore_service_reply", send_start)
    ]

    append_at = send_body.index("d1l_dm_store_append_tx(")
    begin_at = send_body.index("begin_pending_dm_tx(")
    waiting_at = send_body.index("D1L_DM_DELIVERY_WAITING_RADIO", begin_at)
    active_at = send_body.index("D1L_DM_DELIVERY_TX_ACTIVE", waiting_at)
    radio_at = send_body.index("Radio.SendWithOrigin(")

    assert append_at < begin_at < waiting_at < active_at < radio_at
    assert "append_outcome->durable" in source
    assert "record_detached_dm_queue_failure" in send_body
    assert "d1l_dm_store_append(" not in send_body
    assert "meshcore_service_send_raw(" not in send_body
    assert "static void clear_pending_dm_tx" in source


def test_inbound_dm_ack_is_correlated_and_dispatched_by_service_queue():
    source = read("main/mesh/meshcore_service.c")
    header = read("main/mesh/meshcore_service.h")
    planner = read("main/mesh/meshcore_ack_dispatch.h")
    console = read("main/comms/usb_console.c")
    identity_body = source.split("static esp_err_t calc_dm_identity_digest", 1)[1].split(
        "static esp_err_t calc_dm_ack_hash", 1
    )[0]
    dm_parse_body = source.split("static bool parse_rx_dm_packet", 1)[1].split(
        "static void record_dm_ack", 1
    )[0]

    assert '#include "mesh/meshcore_ack_dispatch.h"' in source
    assert "D1L_MESHCORE_DM_ACK_WIRE_BYTES 6U" in planner
    assert "D1L_MESHCORE_DM_ACK_DELAY_MS 200U" in planner
    assert "D1L_MESHCORE_DM_ACK_MAX_DISPATCHES 2U" in planner
    assert "D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH" in planner
    assert "D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK" in planner
    assert "D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK" in planner
    assert "build_dm_ack_response" in source
    assert "meshcore_service_send_ack_async" in source
    assert "xQueueSendToFront(s_service_queue, &cmd, 0)" in source
    assert ".ack_response = true" in source
    assert "vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms))" in source
    assert "const bool ack_queued = parse_rx_dm_packet" in source
    assert "if (!ack_queued)" in source
    assert "complete_pending_ack_tx(true, ESP_OK)" in source
    assert "complete_pending_ack_tx(false, ESP_ERR_TIMEOUT)" in source
    assert (
        "event->tx_operation.kind == D1L_MESH_TX_OPERATION_ACK_RESPONSE"
        in source
    )
    assert "const bool ack_response = s_active_tx_ack_response" not in source
    assert "s_active_tx_ack_response = cmd->ack_response" in source
    assert "d1l_meshcore_dm_identity_material" in planner
    assert "out[index++] = (uint8_t)(plain[4] >> 2U)" in planner
    assert "calc_dm_identity_digest" in source
    assert "d1l_meshcore_dm_identity_material" in identity_body
    assert "extended_attempt" not in identity_body
    assert "d1l_meshcore_ack_dedupe_contains" in source
    assert "d1l_meshcore_ack_dedupe_remember" in source
    assert "dispatch_bounded_dm_ack" in source
    assert 'append_packet_log("rx", "dm_text_duplicate"' in source
    assert dm_parse_body.count("record_dm_ack_failure(ack_hash, store_ret);") == 1
    assert "d1l_dm_store_append_rx_identity(" in dm_parse_body
    assert "store_outcome.inserted" in dm_parse_body
    assert "remember_ack_identity_state(&retained_identity, false)" in dm_parse_body
    assert "d1l_dm_store_find_rx_identity(identity_digest" in dm_parse_body
    assert "uint32_t ack_tx_queued;" in header
    assert "uint32_t ack_tx_done;" in header
    assert "uint32_t ack_tx_failed;" in header
    assert "uint32_t ack_tx_duplicate_rows_suppressed;" in header
    assert "ack_tx_duplicate_suppressed" not in header
    assert '\\\"ack_tx\\\":{' in console
    assert '\\\"duplicate_rows_suppressed\\\":%lu' in console
    assert '\\\"duplicate_suppressed\\\"' not in console


def test_inbound_dm_ack_identity_and_dispatch_state_are_retained_across_reboot():
    store_h = read("main/mesh/dm_store.h")
    store_c = read("main/mesh/dm_store.c")
    service = read("main/mesh/meshcore_service.c")
    console = read("main/comms/usb_console.c")
    ui = read("main/ui/ui_phase1.c")
    messages_ui = read("main/ui/ui_messages.c")
    limitations = read("docs/KNOWN_LIMITATIONS.md")

    dispatch = service.split("static bool dispatch_bounded_dm_ack", 1)[1].split(
        "static void parse_rx_public_packet", 1
    )[0]
    completion = service.split("static void complete_pending_ack_tx", 1)[1].split(
        "static void complete_unqueued_ack_reservation", 1
    )[0]
    initialization = service.split("void d1l_meshcore_service_init", 1)[1].split(
        "esp_err_t d1l_meshcore_service_ensure_identity", 1
    )[0]

    assert "D1L_DM_STORE_SCHEMA 6U" in store_c
    assert "D1L_DM_STORE_SCHEMA_V5 5U" in store_c
    assert "D1L_DM_STORE_SCHEMA_V4 4U" in store_c
    assert "d1l_dm_entry_v4_t" in store_c
    assert "d1l_dm_store_blob_v4_t" in store_c
    assert "convert_v4_blob" in store_c
    assert "convert_v5_blob" in store_c
    assert "D1L_DM_ACK_STATE_LEGACY_UNVERIFIED" in store_h
    assert "D1L_DM_ACK_STATE_PENDING" in store_h
    assert "D1L_DM_ACK_STATE_SENT" in store_h
    assert "D1L_DM_ACK_STATE_RETRYABLE" in store_h
    assert "D1L_DM_ACK_STATE_TERMINAL" in store_h
    assert "normalize_interrupted_ack_locked" in store_c
    assert "D1L_DM_ACK_INTERRUPTED_ERROR" in store_c
    assert "identity_digest[D1L_DM_IDENTITY_DIGEST_BYTES]" in store_h
    assert "d1l_dm_store_append_outcome_t" in store_h
    assert "outcome->inserted = true" in store_c
    assert "outcome->durable = ret == ESP_OK" in store_c
    assert "A split SD/NVS write can partially commit" in store_c
    assert "rolling it\n         * back" in store_c
    assert "d1l_dm_ack_state_t previous_state" not in store_c

    reserve_at = dispatch.index("d1l_dm_store_reserve_ack_dispatch(")
    queue_at = dispatch.index("meshcore_service_send_ack_async(")
    assert reserve_at < queue_at
    assert "retained.ack_state == D1L_DM_ACK_STATE_PENDING" in dispatch
    assert "d1l_dm_store_flush()" in dispatch
    assert "reservation.reserved" in dispatch
    assert "reservation.reserved || reserve_ret != ESP_ERR_INVALID_STATE" in dispatch
    assert "d1l_meshcore_ack_dedupe_mark_durable" in dispatch
    rebind_at = dispatch.index("d1l_dm_store_rebind_pending_ack_dispatch(")
    assert rebind_at < queue_at
    assert "retained.ack_dispatch_kind != (uint8_t)plan->kind" in dispatch
    assert "record_dm_ack_failure(ack_hash, rebind_ret)" in dispatch
    assert "valid_identity_digest_equal" in store_c
    assert "identity_match_set" in store_c
    assert "d1l_dm_store_complete_ack_dispatch(" in completion
    assert completion.index("d1l_dm_store_complete_ack_dispatch(") < completion.index(
        "clear_pending_ack_tx();"
    )
    assert "s_pending_ack_tx.row_seq" in completion
    assert "s_pending_ack_tx.identity_digest" in completion
    assert "find_rx_identity_locked(identity_digest, 0U)" in store_c
    assert "row_seq is only a stale-safe" in store_c
    assert "pending_ack_identity_matches(digest)" in dispatch
    assert "s_pending_ack_tx.row_seq == retained.seq" not in dispatch
    assert "restore_ack_dedupe_from_store();" in initialization

    assert '\\\"ack_response\\\":{' in console
    assert '\\\"identity_valid\\\":%s' in console
    assert '\\\"state\\\":\\\"%s\\\"' in console
    assert '\\\"dispatch_count\\\":%u' in console
    assert '\\\"last_kind\\\":\\\"%s\\\"' in console
    assert '\\\"last_error\\\":\\\"%s\\\"' in console
    assert "d1l_dm_ack_state_name(entry->ack_state)" in messages_ui
    assert "legacy_unverified" in limitations
    assert "never hydrated into the ACK cache" in limitations


def test_console_and_smoke_expose_dm_workflow():
    console = read("main/comms/usb_console.c")
    assert 'ok_begin("messages dm")' in console
    assert 'strcmp(line, "messages dm")' in console
    assert 'strncmp(line, "messages dm ", 12)' in console
    assert "d1l_dm_store_copy_recent_page(entries, D1L_CONSOLE_MESSAGE_PAGE_SIZE" in console
    assert "d1l_dm_store_copy_thread_page(thread_fingerprint, entries" in console
    assert 'strcmp(line, "messages dm clear")' in console
    assert 'strncmp(line, "mesh send dm ", 13)' in console
    assert "d1l_meshcore_service_send_dm(fingerprint, text)" in console
    assert "messages dm [offset <n>]" in console
    assert "messages dm <fingerprint> [offset <n>]" in console
    assert '\\"page_size\\"' in console
    assert '\\"total_matches\\"' in console
    assert '\\"has_older\\"' in console
    assert '\\"next_offset\\"' in console
    assert "optional fingerprint filters one retained thread and offset pages older rows" in console
    assert "MeshCore direct-message rows are kept in bounded retained storage" in console
    assert "MeshCore direct-message rows are kept in a bounded NVS store" not in console
    assert "messages dm" in SMOKE_COMMANDS


def test_app_model_and_ui_preview_recent_dms():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    messages_ui = read("main/ui/ui_messages.c")
    messages_header = read("main/ui/ui_messages.h")
    thread_render = messages_ui.split("bool d1l_ui_messages_render_thread(", 1)[1].split(
        "bool d1l_ui_messages_expand_thread", 1
    )[0]
    thread_bridge = ui.split("static bool render_dm_thread_sheet(void)", 1)[1].split(
        "static void show_dm_thread_for", 1
    )[0]
    thread_open = ui.split("static void show_dm_thread_for", 1)[1].split(
        "static void open_home_dm_preview_event_cb", 1
    )[0]
    assert "D1L_APP_SNAPSHOT_DM_PREVIEW 5U" in header
    assert "recent_dms" in header
    assert "dm_total_written" in header
    assert "dm_content_revision" in header
    assert "snapshot->dm_content_revision = dms.content_revision" in source
    assert "d1l_dm_store_copy_recent" in source
    assert "d1l_app_model_copy_dm_thread_page" in header
    assert "d1l_app_model_copy_dm_thread" in header
    assert "d1l_dm_store_copy_thread_page(fingerprint, out_entries" in source
    assert "d1l_read_state_dm_entry_is_unread(&out_entries[i])" in source
    assert "d1l_app_model_send_dm_text" in source
    assert "messages_render_dm_row" in messages_ui
    assert "load_dm_thread_rows" in thread_bridge
    assert "d1l_app_model_copy_dm_thread_page(" in ui
    assert "thread_entries[D1L_DM_STORE_CAPACITY]" in messages_header
    assert "d1l_ui_messages_expand_thread" in messages_ui
    assert 'body, "Load Older", 0, 0, 424, 48' in thread_render
    assert "lv_obj_scroll_to_y(body, LV_COORD_MAX, LV_ANIM_OFF)" in thread_render
    assert "d1l_app_model_mark_dm_thread_read(fingerprint)" in thread_open
    assert thread_open.index("d1l_app_model_mark_dm_thread_read(fingerprint)") < thread_open.index(
        "render_dm_thread_sheet()"
    )
    assert 'sheet, "Read"' not in thread_render
    assert "read_dm_thread_event_cb" not in ui
    assert '"DM"' in ui
    assert "No direct messages" in messages_ui
