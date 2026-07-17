#include "mesh/meshcore_packet_hash.h"

#include <stddef.h>
#include <string.h>

#include "mbedtls/md.h"

#include "mesh/meshcore_lifetime.h"

static bool cache_slot_occupied(const d1l_meshcore_packet_hash_cache_t *cache,
                                uint16_t index)
{
    return (cache->occupied[index / 8U] & (uint8_t)(1U << (index % 8U))) != 0U;
}

static void cache_slot_set_occupied(d1l_meshcore_packet_hash_cache_t *cache,
                                    uint16_t index, bool occupied)
{
    const uint8_t mask = (uint8_t)(1U << (index % 8U));
    if (occupied) {
        cache->occupied[index / 8U] |= mask;
    } else {
        cache->occupied[index / 8U] &= (uint8_t)~mask;
    }
}

esp_err_t d1l_meshcore_packet_hash_calculate(
    const d1l_meshcore_wire_packet_t *packet,
    uint8_t out_hash[D1L_MESHCORE_PACKET_HASH_BYTES])
{
    if (!packet || !out_hash || !packet->payload || packet->type > 0x0fU ||
        packet->payload_len == 0U ||
        packet->payload_len > D1L_MESHCORE_MAX_PACKET_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t material[1U + 2U + D1L_MESHCORE_MAX_PACKET_PAYLOAD];
    size_t material_len = 0U;
    material[material_len++] = packet->type;
    if (packet->type == D1L_MESHCORE_PAYLOAD_TRACE) {
        /* Packet::path_len is uint16_t in the pinned little-endian targets. */
        material[material_len++] = packet->path_len;
        material[material_len++] = 0U;
    }
    memcpy(&material[material_len], packet->payload, packet->payload_len);
    material_len += packet->payload_len;

    const mbedtls_md_info_t *md =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t digest[32U];
    if (!md || mbedtls_md(md, material, material_len, digest) != 0) {
        return ESP_FAIL;
    }
    memcpy(out_hash, digest, D1L_MESHCORE_PACKET_HASH_BYTES);
    return ESP_OK;
}

void d1l_meshcore_packet_hash_cache_reset(
    d1l_meshcore_packet_hash_cache_t *cache)
{
    if (cache) {
        memset(cache, 0, sizeof(*cache));
    }
}

bool d1l_meshcore_packet_hash_cache_contains(
    const d1l_meshcore_packet_hash_cache_t *cache,
    const uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES])
{
    if (!cache || !hash) {
        return false;
    }
    for (uint16_t index = 0U;
         index < D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY; ++index) {
        if (cache_slot_occupied(cache, index) &&
            memcmp(cache->hashes[index], hash,
                   D1L_MESHCORE_PACKET_HASH_BYTES) == 0) {
            return true;
        }
    }
    return false;
}

bool d1l_meshcore_packet_hash_cache_remember(
    d1l_meshcore_packet_hash_cache_t *cache,
    const uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES])
{
    if (!cache || !hash ||
        cache->next_index >= D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY ||
        d1l_meshcore_packet_hash_cache_contains(cache, hash)) {
        return false;
    }
    memcpy(cache->hashes[cache->next_index], hash,
           D1L_MESHCORE_PACKET_HASH_BYTES);
    cache_slot_set_occupied(cache, cache->next_index, true);
    uint16_t next_index = 0U;
    if (!d1l_meshcore_lifetime_packet_fifo_next(
            cache->next_index, D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY,
            &next_index)) {
        cache_slot_set_occupied(cache, cache->next_index, false);
        memset(cache->hashes[cache->next_index], 0,
               D1L_MESHCORE_PACKET_HASH_BYTES);
        return false;
    }
    cache->next_index = next_index;
    return true;
}

bool d1l_meshcore_packet_hash_cache_forget(
    d1l_meshcore_packet_hash_cache_t *cache,
    const uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES])
{
    if (!cache || !hash) {
        return false;
    }
    for (uint16_t index = 0U;
         index < D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY; ++index) {
        if (cache_slot_occupied(cache, index) &&
            memcmp(cache->hashes[index], hash,
                   D1L_MESHCORE_PACKET_HASH_BYTES) == 0) {
            memset(cache->hashes[index], 0,
                   D1L_MESHCORE_PACKET_HASH_BYTES);
            cache_slot_set_occupied(cache, index, false);
            return true;
        }
    }
    return false;
}
