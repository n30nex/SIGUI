from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_packet_hash_matches_pinned_upstream_framing_and_bounds():
    header = read("main/mesh/meshcore_packet_hash.h")
    source = read("main/mesh/meshcore_packet_hash.c")

    assert "#define D1L_MESHCORE_PACKET_HASH_BYTES 8U" in header
    assert "#define D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY 160U" in header
    assert "uint8_t occupied[D1L_MESHCORE_PACKET_HASH_OCCUPANCY_BYTES]" in header
    assert "material[material_len++] = packet->type;" in source
    trace = source.split(
        "if (packet->type == D1L_MESHCORE_PAYLOAD_TRACE)", 1
    )[1].split("memcpy(&material[material_len]", 1)[0]
    assert "material[material_len++] = packet->path_len;" in trace
    assert "material[material_len++] = 0U;" in trace
    assert "mbedtls_md(md, material, material_len, digest)" in source
    assert "memcpy(out_hash, digest, D1L_MESHCORE_PACKET_HASH_BYTES)" in source
    assert "malloc" not in source


def test_signed_advert_duplicate_probe_preserves_retryable_storage_failures():
    service = read("main/mesh/meshcore_service.c")
    receiver = service.split("static void parse_rx_advert_packet", 1)[1].split(
        "static void meshcore_service_handle_radio_tx_done", 1
    )[0]

    signature = receiver.index("verify_advert_signature(")
    self_check = receiver.index("memcmp(pub_key, settings->identity_public_key")
    app_parse = receiver.index("d1l_advert_data_parse(")
    hash_calculate = receiver.index("d1l_meshcore_packet_hash_calculate(")
    hash_probe = receiver.index("d1l_meshcore_packet_hash_cache_contains(")
    admission = receiver.index("d1l_meshcore_advert_admit_verified(")
    cache_policy = receiver.index(
        "d1l_meshcore_advert_admission_receipt_cacheable(&admission)"
    )
    remember = receiver.index("d1l_meshcore_packet_hash_cache_remember(")
    outcome_dispatch = receiver.index("switch (admission.outcome)")
    assert (
        signature
        < self_check
        < app_parse
        < hash_calculate
        < hash_probe
        < admission
        < cache_policy
        < remember
        < outcome_dispatch
    )
    assert '"advert_duplicate"' in receiver
    assert "packet_hash_ready &&" in receiver[cache_policy - 80 : remember]


def test_admission_receipt_cache_policy_is_terminal_and_explicit():
    source = read("main/mesh/meshcore_advert_admission.c")
    policy = source.split(
        "bool d1l_meshcore_advert_admission_receipt_cacheable", 1
    )[1].split("esp_err_t d1l_meshcore_advert_admit_verified", 1)[0]

    assert "D1L_MESHCORE_ADVERT_ADMISSION_ACCEPTED" in policy
    assert "receipt->contact_store_error == ESP_OK" in policy
    assert "D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_SUCCEEDED" in policy
    assert "D1L_MESHCORE_ADVERT_ADMISSION_REPLAY_REJECTED" in policy
    for outcome in (
        "D1L_MESHCORE_ADVERT_ADMISSION_INVALID",
        "D1L_MESHCORE_ADVERT_ADMISSION_CONTACT_RETRY_FAILED",
        "D1L_MESHCORE_ADVERT_ADMISSION_KEY_COLLISION",
        "D1L_MESHCORE_ADVERT_ADMISSION_NODE_STORE_ERROR",
    ):
        assert outcome in policy


def test_packet_hash_cache_is_boot_local_and_firmware_linked():
    service = read("main/mesh/meshcore_service.c")
    init = service.split("void d1l_meshcore_service_init", 1)[1]
    first_init = init.split("if (s_service_initialized)", 1)[1]
    assert (
        "d1l_meshcore_packet_hash_cache_reset(&s_advert_packet_hash_cache);"
        in first_init
    )
    assert '"mesh/meshcore_packet_hash.c"' in read("main/CMakeLists.txt")
