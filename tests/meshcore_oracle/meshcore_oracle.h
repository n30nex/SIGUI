#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_MESHCORE_ORACLE_ABI_VERSION 2U
#define D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT \
    "e8d3c53ba1ea863937081cd0caad759b832f3028"

#define D1L_MESHCORE_ORACLE_MAX_PATH_BYTES 64U
#define D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES 184U
#define D1L_MESHCORE_ORACLE_MAX_RAW_BYTES 255U
#define D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES 32U
#define D1L_MESHCORE_ORACLE_MAX_ADVERT_NAME_BYTES 31U
#define D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES 32U
#define D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES 4U
#define D1L_MESHCORE_ORACLE_SIGNATURE_BYTES 64U
#define D1L_MESHCORE_ORACLE_MIN_ACK_BYTES 4U
#define D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES 9U
#define D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES 63U

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

/*
 * Production signed-advert verifier used by D1L. The signed message is the
 * pinned Mesh::createAdvert layout: public key, four timestamp bytes exactly
 * as carried on wire, then zero-to-32 advert app-data bytes. Verification uses
 * the real Ed25519 C implementation pinned inside the MeshCore gitlink after
 * applying the same canonical-S, canonical-point, and low-order-point guards
 * as the D1L production receive path. It does not claim Mesh dispatch,
 * replay/timestamp policy, contact mutation, or duplicate suppression.
 */
bool d1l_meshcore_oracle_verify_signed_advert(
    const uint8_t public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t timestamp[D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES],
    const uint8_t signature[D1L_MESHCORE_ORACLE_SIGNATURE_BYTES],
    const uint8_t *app_data,
    size_t app_data_len);

/*
 * Canonical non-TRACE preparation vectors derived from the pinned Mesh.cpp
 * sendFlood/sendDirect/sendZeroHop rules. These return header/path/priority
 * semantics only; they do not claim queue timing or forwarding behavior.
 */
bool d1l_meshcore_oracle_prepare_flood(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    uint8_t path_hash_size,
    uint8_t use_transport,
    const uint16_t transport_codes[2],
    uint8_t *out_priority);

bool d1l_meshcore_oracle_prepare_direct(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    const uint8_t *path,
    uint8_t path_len,
    uint8_t *out_priority);

bool d1l_meshcore_oracle_prepare_zero_hop(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    uint8_t use_transport,
    const uint16_t transport_codes[2],
    uint8_t *out_priority);

/*
 * Canonical simple-ACK and multipart-ACK payload framing derived from the
 * pinned Mesh::createAck/createMultiAck implementations. These functions do
 * not derive expected ACK hashes, encrypt ACK+PATH responses, correlate an ACK
 * with a packet, or claim delivery/timing behavior. Creation returns the
 * canonical pre-route layout; route preparation is required before wire use.
 */
bool d1l_meshcore_oracle_create_ack(
    const uint8_t *ack,
    size_t ack_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_create_multi_ack(
    const uint8_t *ack,
    size_t ack_len,
    uint8_t remaining,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_ack(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t *out_ack,
    size_t ack_capacity,
    size_t *out_ack_len,
    uint8_t *out_remaining,
    uint8_t *out_is_multipart);

/*
 * Canonical source TRACE framing derived from pinned Mesh::createTrace and the
 * TRACE branch of Mesh::sendDirect. The constructor result is pre-route;
 * prepare_trace_direct must be applied before wire encoding. Only the public
 * flags-zero, one-byte-hash source layout is accepted. This boundary does not
 * authenticate auth_code or claim hop forwarding, SNR collection, identity
 * matching, duplicate suppression, route discovery completion, or callback
 * dispatch.
 */
bool d1l_meshcore_oracle_create_trace(
    uint32_t tag,
    uint32_t auth_code,
    uint8_t flags,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_prepare_trace_direct(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    const uint8_t *path_hashes,
    size_t path_hashes_len,
    uint8_t *out_priority);

bool d1l_meshcore_oracle_parse_trace_source(
    const d1l_meshcore_oracle_packet_t *packet,
    uint32_t *out_tag,
    uint32_t *out_auth_code,
    uint8_t *out_flags,
    uint8_t *out_path_hashes,
    size_t path_hashes_capacity,
    size_t *out_path_hashes_len);

#ifdef __cplusplus
}
#endif
