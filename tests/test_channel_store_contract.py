from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_channel_store_is_bounded_persistent_and_redacted_by_default():
    header = read("main/mesh/channel_store.h")
    source = read("main/mesh/channel_store.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")

    assert "D1L_CHANNEL_STORE_CAPACITY 8U" in header
    assert "D1L_CHANNEL_PUBLIC_ID UINT64_C(1)" in header
    assert "uint64_t channel_id" in header
    assert "uint64_t history_key" in header
    assert "uint64_t lineage" in header
    assert "uint64_t generation" in header
    assert "uint32_t unread_count" in header
    assert "D1L_CHANNEL_SOURCE_BUILTIN" in header
    assert "D1L_CHANNEL_SOURCE_URI_IMPORT" in header
    assert "D1L_CHANNEL_MUTATION_NAME_COLLISION" in header
    assert "D1L_CHANNEL_MUTATION_SECRET_COLLISION" in header
    assert "d1l_channel_store_find_unique_hash" in header
    assert "d1l_channel_store_export_share_uri" in header
    assert "d1l_channel_store_select" in header
    assert "d1l_channel_store_snapshot" in header

    redacted = header.split("typedef struct {", 1)[1].split(
        "} d1l_channel_info_t;", 1
    )[0]
    assert "secret" not in redacted
    assert "Deliberately separate, secret-bearing protocol view" in header
    assert "only normal API that emits secret material" in header

    assert 'D1L_CHANNEL_STORE_NAMESPACE "d1l_channels"' in source
    assert 'D1L_CHANNEL_STORE_KEY "channels"' in source
    assert "D1L_CHANNEL_STORE_SCHEMA_V1 1U" in source
    assert "D1L_CHANNEL_STORE_SCHEMA_V2 2U" in source
    assert "D1L_CHANNEL_STORE_SCHEMA 3U" in source
    assert "D1L_CHANNEL_STORE_MAGIC UINT32_C(0x43484e4c)" in source
    for field in [
        "uint32_t magic;",
        "uint32_t blob_length;",
        "uint32_t payload_length;",
        "uint32_t payload_checksum;",
        "uint64_t lineage;",
        "uint64_t generation;",
    ]:
        assert field in source
    assert "crc32_bytes(&blob->lineage" in source
    assert "channel schema v2 payload offset changed" in source
    assert "channel schema v3 message generation offset changed" in source
    assert "d1l_channel_store_blob_v1_t" in source
    assert "d1l_channel_store_blob_v2_t" in source
    assert "migrate_v1_blob" in source
    assert "migrate_v2_blob" in source
    assert "channel schema v1 blob layout changed" in source
    assert "channel schema v2 blob layout changed" in source
    assert "channel schema v3 blob layout changed" in source
    assert "Preserve the corrupt/unknown blob for recovery" in source
    assert "persist_store_or_rollback" in source
    select = source.split("esp_err_t d1l_channel_store_select", 1)[1].split(
        "esp_err_t d1l_channel_store_remove", 1
    )[0]
    assert "d1l_store_lock_take(&s_store_lock)" in select
    assert "s_entries[index].enabled == 0U" in select
    assert "make_only_default((size_t)index)" in select
    assert "persist_store_or_rollback(&s_rollback_scratch)" in select
    snapshot = source.split("esp_err_t d1l_channel_store_snapshot", 1)[1].split(
        "size_t d1l_channel_store_copy", 1
    )[0]
    assert snapshot.count("d1l_store_lock_take(&s_store_lock)") == 1
    assert "copy_info(&s_entries[i], &out_channels[i])" in snapshot
    assert "*out_active_channel_id = s_entries[active_index].channel_id" in snapshot
    assert ".revision = s_revision" in snapshot
    assert "malloc(" not in source
    assert "calloc(" not in source
    assert "realloc(" not in source
    assert "free(" not in source
    assert '"mesh/channel_store.c"' in cmake
    assert '#include "mesh/channel_store.h"' in app_main
    assert "d1l_channel_store_init()" in app_main


def test_channel_store_slice_does_not_reintroduce_public_special_case_runtime():
    # The bounded data-model slice intentionally leaves runtime wiring to its
    # own owner/PR; it must not mutate the shared protocol or UI hotspots.
    source = read("main/mesh/channel_store.c")
    assert "meshcore_service" not in source
    assert "ui_phase1" not in source
    assert "mbedtls_md_info_from_type(MBEDTLS_MD_SHA256)" in source
    assert "D1L_CHANNEL_URI_SCHEME \"meshcore://channel/add?\"" in source
    assert "entry->history_key = history_key_for(s_lineage, entry->channel_id)" in source
    assert "s_count >= D1L_CHANNEL_STORE_CAPACITY" in source


def test_channel_store_qr_locking_and_secret_cleanup_boundaries():
    header = read("main/mesh/channel_store.h")
    source = read("main/mesh/channel_store.c")

    assert "accepts exactly a 16-byte (32-hex) secret" in header
    decode = source.split("static bool decode_uri_secret", 1)[1].split(
        "static bool parse_channel_uri", 1
    )[0]
    assert "encoded_len != D1L_CHANNEL_SECRET_128_LEN * 2U" in decode
    assert "D1L_CHANNEL_SECRET_256_LEN * 2U" not in decode
    export = source.split("esp_err_t d1l_channel_store_export_share_uri", 1)[1]
    assert "entry->secret_len != D1L_CHANNEL_SECRET_128_LEN" in export
    assert "return ESP_ERR_NOT_SUPPORTED" in export

    ensure = source.split("static esp_err_t ensure_loaded", 1)[1].split(
        "esp_err_t d1l_channel_store_init", 1
    )[0]
    assert ensure.index("d1l_store_lock_take") < ensure.index("s_loaded")
    assert ensure.index("s_loaded") < ensure.index("d1l_store_lock_give")
    init = source.split("esp_err_t d1l_channel_store_init", 1)[1].split(
        "esp_err_t d1l_channel_store_reset", 1
    )[0]
    assert "d1l_store_lock_take(&s_store_lock)" in init
    assert "d1l_store_lock_give(&s_store_lock)" in init
    stats = source.split("d1l_channel_store_stats_t d1l_channel_store_stats", 1)[
        1
    ].split("size_t d1l_channel_store_copy", 1)[0]
    assert stats.index("d1l_store_lock_take") < stats.index("s_loaded")
    assert stats.index("s_loaded") < stats.index("d1l_store_lock_give")

    assert "static void secure_zero" in source
    parse = source.split("static bool parse_channel_uri", 1)[1].split(
        "esp_err_t d1l_channel_store_import_uri", 1
    )[0]
    post_init = parse.split("d1l_channel_uri_t parsed;", 1)[1]
    assert "return false;" not in post_init
    assert "goto cleanup;" in post_init
    assert "cleanup:" in post_init
    assert "secure_zero(parsed.secret, sizeof(parsed.secret))" in post_init
    rollback = source.split("static esp_err_t persist_store_or_rollback", 1)[
        1
    ].split("static bool public_entry_is_exact", 1)[0]
    assert "secure_zero(before, sizeof(*before))" in rollback
    assert "secure_zero(normalized, sizeof(normalized))" in source
    assert "secure_zero(parsed.secret, sizeof(parsed.secret))" in source
    assert "secure_zero(secret_hex, sizeof(secret_hex))" in source
    assert "secure_zero(&s_blob_scratch, sizeof(s_blob_scratch))" in source
