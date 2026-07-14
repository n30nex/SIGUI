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


def test_verified_advert_upsert_preserves_user_preferences_and_is_not_rx_bound_yet():
    body = _verified_upsert_body()
    service = (ROOT / "main/mesh/meshcore_service.c").read_text(encoding="utf-8")

    for user_preference in (
        "entry->favorite =",
        "entry->muted =",
        "entry->out_path_valid =",
        "entry->out_path_len =",
        "entry->out_path_updated_ms =",
    ):
        assert user_preference not in body
    assert "d1l_contact_store_upsert_verified_advert" not in service
