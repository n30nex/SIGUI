from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _verified_upsert_body() -> str:
    source = (ROOT / "main/mesh/contact_store.c").read_text(encoding="utf-8")
    start = source.index("esp_err_t d1l_contact_store_upsert_verified_advert")
    end = source.index("esp_err_t d1l_contact_store_update_path", start)
    return source[start:end]


def test_verified_advert_upsert_is_full_key_authoritative_and_non_evicting():
    body = _verified_upsert_body()

    assert "find_unique_index_by_public_key_hex" in body
    assert "find_unique_index_by_fingerprint_hex" in body
    assert "D1L_CONTACT_VERIFIED_ADVERT_COLLISION" in body
    assert "D1L_CONTACT_VERIFIED_ADVERT_FULL" in body
    assert "oldest_index(" not in body
    assert "s_dropped_oldest++" not in body


def test_verified_advert_upsert_preserves_user_preferences():
    body = _verified_upsert_body()

    for user_preference in (
        "entry->favorite =",
        "entry->muted =",
        "entry->out_path_valid =",
        "entry->out_path_len =",
        "entry->out_path_updated_ms =",
    ):
        assert user_preference not in body


def test_signed_advert_rx_binds_exact_full_key_to_truthful_retained_contact_result():
    service = (ROOT / "main/mesh/meshcore_service.c").read_text(encoding="utf-8")
    receiver = service.split("static void parse_rx_advert_packet", 1)[1].split(
        "static void on_tx_done", 1
    )[0]

    signature = receiver.index("verify_advert_signature")
    self_filter = receiver.index("memcmp(pub_key, settings->identity_public_key")
    node_upsert = receiver.index("d1l_node_store_upsert_advert")
    stale_filter = receiver.index("if (advert_stale)")
    node_error = receiver.index('append_packet_log("rx", "advert_store_error"')
    node_readback = receiver.index("d1l_node_store_find_by_fingerprint")
    full_key_match = receiver.index(
        "strcmp(verified_node.public_key_hex, pub_key_hex) == 0"
    )
    contact_upsert = receiver.index("d1l_contact_store_upsert_verified_advert")
    accepted = receiver.index("s_status.rx_adverts++")
    assert (
        signature
        < self_filter
        < node_upsert
        < stale_filter
        < node_error
        < node_readback
        < full_key_match
        < contact_upsert
        < accepted
    )

    assert "D1L_CONTACT_VERIFIED_ADVERT_CREATED" in service
    assert "D1L_CONTACT_VERIFIED_ADVERT_UPDATED" in service
    assert "D1L_CONTACT_VERIFIED_ADVERT_PROMOTED_PLACEHOLDER" in service
    assert "D1L_CONTACT_VERIFIED_ADVERT_COLLISION" in receiver
    assert "D1L_CONTACT_VERIFIED_ADVERT_FULL" in service
    assert 'return "new";' in service
    assert 'return "updated";' in service
    assert 'return "promoted";' in service
    assert 'return "collision";' in service
    assert 'return "full";' in service
    assert "is_new" not in receiver
    stale_body = receiver[stale_filter:node_error]
    assert "retry_verified_contact_promotion" in stale_body
    assert '"advert_contact_retry"' in stale_body
    assert '"advert_contact_retry_error"' in stale_body
    assert '"advert_key_collision"' in receiver


def test_heard_node_rejects_full_key_prefix_collision_before_replay_or_mutation():
    source = (ROOT / "main/mesh/node_store.c").read_text(encoding="utf-8")
    upsert = source.split("esp_err_t d1l_node_store_upsert_advert", 1)[1].split(
        "d1l_node_store_stats_t d1l_node_store_stats", 1
    )[0]

    collision = upsert.index("!public_keys_equal")
    replay = upsert.index("advert_timestamp <=")
    mutation = upsert.index("entry->seq =")
    assert collision < replay < mutation
    assert "return ESP_ERR_INVALID_STATE;" in upsert[collision:replay]
