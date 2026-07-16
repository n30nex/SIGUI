#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/md.h"
#include "mesh/meshcore_packet_hash.h"

static const mbedtls_md_info_t s_sha256 = {
    .type = MBEDTLS_MD_SHA256,
};
static uint8_t s_last_material[1U + 2U + D1L_MESHCORE_MAX_PACKET_PAYLOAD];
static size_t s_last_material_len;

const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type)
{
    return type == MBEDTLS_MD_SHA256 ? &s_sha256 : NULL;
}

int mbedtls_md(const mbedtls_md_info_t *info, const unsigned char *input,
               size_t input_len, unsigned char *output)
{
    if (!info || info->type != MBEDTLS_MD_SHA256 || !input || !output ||
        input_len == 0U || input_len > sizeof(s_last_material)) {
        return -1;
    }
    memcpy(s_last_material, input, input_len);
    s_last_material_len = input_len;
    memset(output, 0, 32U);
    for (size_t index = 0U; index < input_len; ++index) {
        output[index % 32U] ^=
            (uint8_t)(input[index] + (uint8_t)(index * 17U));
    }
    output[31] ^= (uint8_t)input_len;
    return 0;
}

static d1l_meshcore_wire_packet_t packet_for(
    uint8_t type, uint8_t route, uint8_t path_len, const uint8_t *path,
    const uint8_t *payload, uint16_t payload_len)
{
    d1l_meshcore_wire_packet_t packet = {
        .header = (uint8_t)((type << 2U) | route),
        .route = route,
        .type = type,
        .version = D1L_MESHCORE_PAYLOAD_VER_1,
        .transport_codes = {0x1234U, 0xabcdU},
        .path_len = path_len,
        .path_hash_bytes = d1l_meshcore_wire_path_hash_size(path_len),
        .path_hops = d1l_meshcore_wire_path_hash_count(path_len),
        .path = path,
        .path_byte_len = d1l_meshcore_wire_path_byte_len(path_len),
        .payload = payload,
        .payload_len = payload_len,
    };
    return packet;
}

/* advert_type_payload_framing */
static void test_advert_type_payload_framing(void)
{
    static const uint8_t payload[] = {0xa1U, 0xb2U, 0xc3U};
    const d1l_meshcore_wire_packet_t packet = packet_for(
        D1L_MESHCORE_PAYLOAD_ADVERT, D1L_MESHCORE_ROUTE_FLOOD, 0U, NULL,
        payload, sizeof(payload));
    uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    assert(d1l_meshcore_packet_hash_calculate(&packet, hash) == ESP_OK);
    static const uint8_t expected[] = {
        D1L_MESHCORE_PAYLOAD_ADVERT, 0xa1U, 0xb2U, 0xc3U};
    assert(s_last_material_len == sizeof(expected));
    assert(memcmp(s_last_material, expected, sizeof(expected)) == 0);
}

/* non_trace_route_path_transport_invariance */
static void test_non_trace_route_path_transport_invariance(void)
{
    static const uint8_t payload[] = {0x01U, 0x02U, 0x03U, 0x04U};
    static const uint8_t first_path[] = {0x11U, 0x22U};
    static const uint8_t second_path[] = {0x99U, 0x88U, 0x77U};
    d1l_meshcore_wire_packet_t first = packet_for(
        D1L_MESHCORE_PAYLOAD_ADVERT, D1L_MESHCORE_ROUTE_FLOOD, 2U,
        first_path, payload, sizeof(payload));
    d1l_meshcore_wire_packet_t second = packet_for(
        D1L_MESHCORE_PAYLOAD_ADVERT,
        D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT, 3U, second_path, payload,
        sizeof(payload));
    second.transport_codes[0] = 0xeeeeU;
    second.transport_codes[1] = 0xffffU;
    uint8_t first_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    uint8_t second_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    assert(d1l_meshcore_packet_hash_calculate(&first, first_hash) == ESP_OK);
    assert(d1l_meshcore_packet_hash_calculate(&second, second_hash) == ESP_OK);
    assert(memcmp(first_hash, second_hash, sizeof(first_hash)) == 0);
}

/* payload_type_and_content_domain_separation */
static void test_payload_type_and_content_domain_separation(void)
{
    static const uint8_t baseline_payload[] = {0x10U, 0x20U, 0x30U};
    static const uint8_t changed_payload[] = {0x10U, 0x20U, 0x31U};
    const d1l_meshcore_wire_packet_t baseline = packet_for(
        D1L_MESHCORE_PAYLOAD_ADVERT, D1L_MESHCORE_ROUTE_FLOOD, 0U, NULL,
        baseline_payload, sizeof(baseline_payload));
    const d1l_meshcore_wire_packet_t changed_type = packet_for(
        D1L_MESHCORE_PAYLOAD_ACK, D1L_MESHCORE_ROUTE_FLOOD, 0U, NULL,
        baseline_payload, sizeof(baseline_payload));
    const d1l_meshcore_wire_packet_t changed_payload_packet = packet_for(
        D1L_MESHCORE_PAYLOAD_ADVERT, D1L_MESHCORE_ROUTE_FLOOD, 0U, NULL,
        changed_payload, sizeof(changed_payload));
    uint8_t baseline_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    uint8_t type_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    uint8_t payload_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    assert(d1l_meshcore_packet_hash_calculate(&baseline, baseline_hash) == ESP_OK);
    assert(d1l_meshcore_packet_hash_calculate(&changed_type, type_hash) == ESP_OK);
    assert(d1l_meshcore_packet_hash_calculate(
               &changed_payload_packet, payload_hash) == ESP_OK);
    assert(memcmp(baseline_hash, type_hash, sizeof(baseline_hash)) != 0);
    assert(memcmp(baseline_hash, payload_hash, sizeof(baseline_hash)) != 0);
}

/* trace_path_length_little_endian_uint16 */
static void test_trace_path_length_little_endian_uint16(void)
{
    static const uint8_t path[] = {0x11U, 0x22U};
    static const uint8_t payload[] = {0x44U, 0x55U};
    d1l_meshcore_wire_packet_t packet = packet_for(
        D1L_MESHCORE_PAYLOAD_TRACE, D1L_MESHCORE_ROUTE_FLOOD, 2U, path,
        payload, sizeof(payload));
    uint8_t first_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    uint8_t second_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    assert(d1l_meshcore_packet_hash_calculate(&packet, first_hash) == ESP_OK);
    static const uint8_t expected[] = {
        D1L_MESHCORE_PAYLOAD_TRACE, 0x02U, 0x00U, 0x44U, 0x55U};
    assert(s_last_material_len == sizeof(expected));
    assert(memcmp(s_last_material, expected, sizeof(expected)) == 0);
    packet.path_len = 1U;
    assert(d1l_meshcore_packet_hash_calculate(&packet, second_hash) == ESP_OK);
    assert(memcmp(first_hash, second_hash, sizeof(first_hash)) != 0);
}

/* trace_path_bytes_excluded */
static void test_trace_path_bytes_excluded(void)
{
    static const uint8_t first_path[] = {0x11U, 0x22U};
    static const uint8_t second_path[] = {0xaaU, 0xbbU};
    static const uint8_t payload[] = {0x77U, 0x88U};
    const d1l_meshcore_wire_packet_t first = packet_for(
        D1L_MESHCORE_PAYLOAD_TRACE, D1L_MESHCORE_ROUTE_DIRECT, 2U,
        first_path, payload, sizeof(payload));
    const d1l_meshcore_wire_packet_t second = packet_for(
        D1L_MESHCORE_PAYLOAD_TRACE, D1L_MESHCORE_ROUTE_DIRECT, 2U,
        second_path, payload, sizeof(payload));
    uint8_t first_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    uint8_t second_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    assert(d1l_meshcore_packet_hash_calculate(&first, first_hash) == ESP_OK);
    assert(d1l_meshcore_packet_hash_calculate(&second, second_hash) == ESP_OK);
    assert(memcmp(first_hash, second_hash, sizeof(first_hash)) == 0);
}

/* cache_miss_remember_hit_forget */
static void test_cache_miss_remember_hit_forget(void)
{
    d1l_meshcore_packet_hash_cache_t cache;
    memset(&cache, 0xff, sizeof(cache));
    d1l_meshcore_packet_hash_cache_reset(&cache);
    const uint8_t zero_hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    assert(!d1l_meshcore_packet_hash_cache_contains(&cache, zero_hash));
    assert(d1l_meshcore_packet_hash_cache_remember(&cache, zero_hash));
    assert(d1l_meshcore_packet_hash_cache_contains(&cache, zero_hash));
    assert(!d1l_meshcore_packet_hash_cache_remember(&cache, zero_hash));
    assert(cache.next_index == 1U);
    assert(d1l_meshcore_packet_hash_cache_forget(&cache, zero_hash));
    assert(!d1l_meshcore_packet_hash_cache_contains(&cache, zero_hash));
    assert(!d1l_meshcore_packet_hash_cache_forget(&cache, zero_hash));
}

static void make_cache_hash(uint16_t value,
                            uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES])
{
    memset(hash, 0, D1L_MESHCORE_PACKET_HASH_BYTES);
    hash[0] = (uint8_t)value;
    hash[1] = (uint8_t)(value >> 8U);
    hash[7] = (uint8_t)(value ^ 0xa5U);
}

/* cache_160_entry_fifo_eviction */
static void test_cache_160_entry_fifo_eviction(void)
{
    d1l_meshcore_packet_hash_cache_t cache = {0};
    uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES] = {0};
    for (uint16_t value = 0U;
         value < D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY; ++value) {
        make_cache_hash(value, hash);
        assert(d1l_meshcore_packet_hash_cache_remember(&cache, hash));
    }
    for (uint16_t value = 0U;
         value < D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY; ++value) {
        make_cache_hash(value, hash);
        assert(d1l_meshcore_packet_hash_cache_contains(&cache, hash));
    }
    make_cache_hash(D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY, hash);
    assert(d1l_meshcore_packet_hash_cache_remember(&cache, hash));
    assert(d1l_meshcore_packet_hash_cache_contains(&cache, hash));
    make_cache_hash(0U, hash);
    assert(!d1l_meshcore_packet_hash_cache_contains(&cache, hash));
    make_cache_hash(1U, hash);
    assert(d1l_meshcore_packet_hash_cache_contains(&cache, hash));
}

int main(void)
{
    test_advert_type_payload_framing();
    test_non_trace_route_path_transport_invariance();
    test_payload_type_and_content_domain_separation();
    test_trace_path_length_little_endian_uint16();
    test_trace_path_bytes_excluded();
    test_cache_miss_remember_hit_forget();
    test_cache_160_entry_fifo_eviction();
    puts("native MeshCore packet hash: ok");
    return 0;
}
