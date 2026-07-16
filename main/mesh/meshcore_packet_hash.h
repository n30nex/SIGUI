#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "mesh/meshcore_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_MESHCORE_PACKET_HASH_BYTES 8U
#define D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY 160U
#define D1L_MESHCORE_PACKET_HASH_OCCUPANCY_BYTES \
    ((D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY + 7U) / 8U)

typedef struct {
    uint8_t hashes[D1L_MESHCORE_PACKET_HASH_CACHE_CAPACITY]
                  [D1L_MESHCORE_PACKET_HASH_BYTES];
    uint8_t occupied[D1L_MESHCORE_PACKET_HASH_OCCUPANCY_BYTES];
    uint16_t next_index;
} d1l_meshcore_packet_hash_cache_t;

/*
 * Matches pinned MeshCore Packet::calculatePacketHash(): SHA-256 over the
 * payload type, the encoded TRACE path length as a little-endian uint16_t
 * when applicable, and the payload. The digest is truncated to eight bytes.
 */
esp_err_t d1l_meshcore_packet_hash_calculate(
    const d1l_meshcore_wire_packet_t *packet,
    uint8_t out_hash[D1L_MESHCORE_PACKET_HASH_BYTES]);

void d1l_meshcore_packet_hash_cache_reset(
    d1l_meshcore_packet_hash_cache_t *cache);

bool d1l_meshcore_packet_hash_cache_contains(
    const d1l_meshcore_packet_hash_cache_t *cache,
    const uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES]);

/* Returns true only when a new entry is inserted. */
bool d1l_meshcore_packet_hash_cache_remember(
    d1l_meshcore_packet_hash_cache_t *cache,
    const uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES]);

/* Returns true only when an existing entry is removed. */
bool d1l_meshcore_packet_hash_cache_forget(
    d1l_meshcore_packet_hash_cache_t *cache,
    const uint8_t hash[D1L_MESHCORE_PACKET_HASH_BYTES]);

#ifdef __cplusplus
}
#endif
