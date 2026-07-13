#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_MESHCORE_ORACLE_ABI_VERSION 1U
#define D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT \
    "e8d3c53ba1ea863937081cd0caad759b832f3028"

#define D1L_MESHCORE_ORACLE_MAX_PATH_BYTES 64U
#define D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES 184U
#define D1L_MESHCORE_ORACLE_MAX_RAW_BYTES 255U
#define D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES 32U
#define D1L_MESHCORE_ORACLE_MAX_ADVERT_NAME_BYTES 31U

#define D1L_MESHCORE_ADVERT_TYPE_NONE 0U
#define D1L_MESHCORE_ADVERT_TYPE_CHAT 1U
#define D1L_MESHCORE_ADVERT_TYPE_REPEATER 2U
#define D1L_MESHCORE_ADVERT_TYPE_ROOM 3U
#define D1L_MESHCORE_ADVERT_TYPE_SENSOR 4U
#define D1L_MESHCORE_ADVERT_LATLON_MASK 0x10U
#define D1L_MESHCORE_ADVERT_FEAT1_MASK 0x20U
#define D1L_MESHCORE_ADVERT_FEAT2_MASK 0x40U
#define D1L_MESHCORE_ADVERT_NAME_MASK 0x80U

/*
 * Stable C boundary for vectors produced by the pinned upstream Packet class.
 * This packet portion is deliberately envelope-only. It does not authenticate
 * or interpret payload bytes and must not be described as a packet-semantic or
 * crypto oracle.
 */
typedef struct {
    uint8_t header;
    uint16_t transport_codes[2];
    uint8_t path_len;
    uint8_t path[D1L_MESHCORE_ORACLE_MAX_PATH_BYTES];
    uint16_t payload_len;
    uint8_t payload[D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES];
} d1l_meshcore_oracle_packet_t;

/*
 * Canonical field representation for upstream AdvertDataBuilder/Parser.
 * Feature/name presence is explicit so zero-valued or empty non-canonical
 * encodings can be rejected without losing wire information.
 */
typedef struct {
    uint8_t type;
    uint8_t has_lat_lon;
    int32_t latitude_e6;
    int32_t longitude_e6;
    uint8_t has_feat1;
    uint16_t feat1;
    uint8_t has_feat2;
    uint16_t feat2;
    uint8_t has_name;
    uint8_t name_len;
    uint8_t name[D1L_MESHCORE_ORACLE_MAX_ADVERT_NAME_BYTES];
} d1l_meshcore_oracle_advert_data_t;

uint32_t d1l_meshcore_oracle_abi_version(void);
const char *d1l_meshcore_oracle_upstream_commit(void);

bool d1l_meshcore_oracle_packet_decode(
    const uint8_t *raw,
    size_t raw_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_packet_encode(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t *dest,
    size_t dest_capacity,
    size_t *out_len);

bool d1l_meshcore_oracle_advert_data_decode(
    const uint8_t *raw,
    size_t raw_len,
    d1l_meshcore_oracle_advert_data_t *out_advert);

bool d1l_meshcore_oracle_advert_data_encode(
    const d1l_meshcore_oracle_advert_data_t *advert,
    uint8_t *dest,
    size_t dest_capacity,
    size_t *out_len);

#ifdef __cplusplus
}
#endif
