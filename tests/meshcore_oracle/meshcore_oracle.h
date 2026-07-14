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
#define D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES 15U
#define D1L_MESHCORE_ORACLE_MAX_LOGIN_ACL_ENTRIES 20U
#define D1L_MESHCORE_ORACLE_LOGIN_ACL_INDEX_NOT_FOUND 0xFFU
#define D1L_MESHCORE_ORACLE_LOGIN_SECRET_SOURCE_NONE 0U
#define D1L_MESHCORE_ORACLE_LOGIN_SECRET_SOURCE_CALLER 1U
#define D1L_MESHCORE_ORACLE_LOGIN_SECRET_SOURCE_STORED_ACL 2U
#define D1L_MESHCORE_ORACLE_LOGIN_CLIENT_MUTATE_OUT_PATH 0x01U
#define D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES 167U
#define D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES 13U
#define D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES 4U
#define D1L_MESHCORE_ORACLE_IDENTITY_SEED_BYTES 32U
#define D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES 32U
#define D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES 4U
#define D1L_MESHCORE_ORACLE_SIGNATURE_BYTES 64U
#define D1L_MESHCORE_ORACLE_MIN_ACK_BYTES 4U
#define D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES 9U
#define D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES 63U
#define D1L_MESHCORE_ORACLE_GROUP_HASH_BYTES 1U
#define D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES 32U
#define D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES \
    D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES
#define D1L_MESHCORE_ORACLE_GROUP_MAC_BYTES 2U
#define D1L_MESHCORE_ORACLE_GROUP_BLOCK_BYTES 16U
#define D1L_MESHCORE_ORACLE_MAX_GROUP_PLAINTEXT_BYTES 168U
#define D1L_MESHCORE_ORACLE_DM_HASH_BYTES 1U
#define D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES 160U
#define D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES 158U
#define D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES 4U
#define D1L_MESHCORE_ORACLE_DM_ACK_BYTES 6U
#define D1L_MESHCORE_ORACLE_MAX_ACK_PATH_BYTES 64U
#define D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES 161U
#define D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES 4U

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

typedef struct {
    uint8_t public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES];
    uint8_t permissions;
    uint8_t out_path_len;
    uint8_t shared_secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES];
    uint32_t last_timestamp;
    uint32_t last_activity;
    uint32_t room_sync_since;
    uint32_t room_pending_ack;
    uint8_t room_push_failures;
} d1l_meshcore_oracle_login_acl_record_t;

typedef struct {
    d1l_meshcore_oracle_login_acl_record_t
        records[D1L_MESHCORE_ORACLE_MAX_LOGIN_ACL_ENTRIES];
    uint8_t record_count;
    uint8_t client_index;
    uint8_t accepted;
    uint8_t inserted;
    uint8_t evicted;
    uint8_t contacts_dirty;
} d1l_meshcore_oracle_login_acl_transition_t;

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

typedef struct {
    uint8_t public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES];
    uint8_t shared_secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES];
} d1l_meshcore_oracle_identity_exchange_t;

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
 * Deterministic Mesh::createAdvert packet construction with the pinned
 * LocalIdentity seed-to-keypair and Ed25519 signer. The numeric timestamp is
 * encoded little-endian, matching the supported ESP32/RP2040 targets. The
 * parser authenticates the complete packet payload before returning fields.
 * Creation returns the upstream pre-route layout; prepare_flood must be
 * applied before a production-equivalent wire send.
 *
 * This boundary does not claim upstream Identity::verify parity, RTC/RNG
 * ownership, replay/timestamp policy, dispatch, contact mutation, duplicate
 * suppression, canonical advert app-data fields, or RF delivery.
 */
bool d1l_meshcore_oracle_create_signed_advert_packet(
    const uint8_t seed[D1L_MESHCORE_ORACLE_IDENTITY_SEED_BYTES],
    uint32_t timestamp,
    const uint8_t *app_data,
    size_t app_data_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_signed_advert_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t out_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    uint32_t *out_timestamp,
    uint8_t *out_app_data,
    size_t app_data_capacity,
    size_t *out_app_data_len);

/*
 * Deterministic valid-input parity for LocalIdentity construction followed by
 * LocalIdentity::calcSharedSecret: create the local Ed25519 keypair from a
 * caller seed, convert a strict peer Ed25519 public key to Montgomery form,
 * and run the pinned ed25519_key_exchange ladder. The complete result is
 * published only after strict peer/derived-public validation and rejection of
 * an all-zero shared secret. Temporary private and shared-secret material is
 * wiped before return.
 *
 * The strict-point and zero-secret guards are a fail-closed D1L oracle policy;
 * upstream calcSharedSecret itself accepts an unchecked peer byte string. This
 * function does not prove persisted-private-key loading, contact lookup,
 * authorization, signature verification, session/admin state, dispatch,
 * routing, production integration, or RF delivery.
 */
bool d1l_meshcore_oracle_identity_shared_secret_from_seed(
    const uint8_t seed[D1L_MESHCORE_ORACLE_IDENTITY_SEED_BYTES],
    const uint8_t peer_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    d1l_meshcore_oracle_identity_exchange_t *out_exchange);

/*
 * Canonical BaseChatMesh::sendLogin plaintext plus Mesh::createAnonDatagram
 * crypto/framing. The caller supplies the destination hash, sender public key,
 * and already-derived shared secret. is_room selects the four-byte timestamp
 * form or the timestamp-plus-four-byte-sync_since room form. Password bytes
 * are explicit, NUL-free, and limited to the upstream 15-byte truncation
 * boundary; overlength input is rejected instead of silently truncated.
 *
 * Creation returns the upstream pre-route ANON_REQ layout. Parsing compares
 * the unauthenticated outer destination/public-key fields to caller-supplied
 * expectations, authenticates/decrypts the body, and accepts only the minimal
 * AES block count and canonical zero padding produced by sendLogin.
 *
 * These functions do not derive an Ed25519 shared secret, validate the sender
 * point, look up a contact, consume an RTC, choose a route, authorize a
 * password, enforce replay policy, create a response, mutate admin/session
 * state, dispatch/schedule a packet, or claim RF delivery.
 */
bool d1l_meshcore_oracle_create_login_request_packet(
    uint8_t destination_hash,
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t timestamp,
    uint8_t is_room,
    uint32_t sync_since,
    const uint8_t *password,
    size_t password_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_login_request_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t is_room,
    uint32_t *out_timestamp,
    uint32_t *out_sync_since,
    uint8_t *out_password,
    size_t password_capacity,
    size_t *out_password_len);

/*
 * Pinned Mesh::createDatagram crypto/framing for regular REQ and RESPONSE
 * packets. The caller supplies the one-byte destination/source hashes, the
 * already-derived shared secret, the payload type, and the logical plaintext.
 * The supported logical length is explicit because MeshCore AES padding does
 * not carry an authenticated length field. Parsing authenticates first,
 * requires the exact minimal block count for expected_plaintext_len, and
 * rejects non-zero padding before publishing output.
 *
 * Empty plaintext is excluded because the pinned receive path rejects the
 * resulting MAC-only datagram. Creation returns the upstream pre-route layout.
 * These functions do not compose or interpret BaseChat request tags, request
 * types, response schemas, authorization, replay/session state, contact or
 * shared-secret derivation, dispatch, route selection, or RF delivery.
 */
bool d1l_meshcore_oracle_create_request_response_packet(
    uint8_t payload_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    const uint8_t *plaintext,
    size_t plaintext_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_request_response_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t payload_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    size_t expected_plaintext_len,
    uint8_t *out_plaintext,
    size_t plaintext_capacity);

/*
 * Canonical successful login RESPONSE schema emitted by the pinned
 * simple_repeater and simple_room_server implementations. The 13 plaintext
 * bytes are server timestamp (little-endian), response code zero, legacy
 * keep-alive zero, the server-specific role indicator, permissions, four
 * caller-supplied uniqueness bytes, and the server-specific firmware level.
 * server_advert_type accepts only REPEATER or ROOM. Creation and parsing use
 * the authenticated regular RESPONSE datagram boundary above and publish no
 * output until the complete schema is authenticated and canonical.
 *
 * This deterministic fixture does not compare passwords, look up or mutate an
 * ACL/contact, enforce replay/timestamps, derive a secret, establish retained
 * session state, dispatch/schedule a packet, choose a route, or claim RF.
 */
bool d1l_meshcore_oracle_create_login_response_packet(
    uint8_t server_advert_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t server_timestamp,
    uint8_t permissions,
    const uint8_t uniqueness[D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES],
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_login_response_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t server_advert_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t *out_server_timestamp,
    uint8_t *out_permissions,
    uint8_t out_uniqueness[D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES]);

/*
 * Deterministic password classification for the ACL-miss/new-client branch of
 * the pinned repeater and room login handlers. Password/configuration spans are
 * explicit NUL-free strings bounded by the upstream 15-byte NodePrefs payload.
 * Admin comparison has precedence over guest comparison. Repeater guest maps
 * to GUEST; room guest maps to READ_WRITE; a room mismatch maps to GUEST only
 * when canonical allow_read_only is one. A structurally valid denial returns
 * true with authorized=0 and permissions=GUEST.
 *
 * This boundary does not perform the blank-password existing-ACL shortcut,
 * contact lookup/insertion/eviction, timestamp replay checks, secret or state
 * mutation, response creation, dispatch, routing, scheduling, or RF.
 */
bool d1l_meshcore_oracle_classify_unmatched_login_password(
    uint8_t server_advert_type,
    uint8_t allow_read_only,
    const uint8_t *password,
    size_t password_len,
    const uint8_t *admin_password,
    size_t admin_password_len,
    const uint8_t *guest_password,
    size_t guest_password_len,
    uint8_t *out_authorized,
    uint8_t *out_permissions);

/*
 * Deterministic full-public-key lookup for the blank-password existing-ACL
 * branch of the pinned repeater and room handlers. ACL keys are a contiguous
 * entry_count by 32-byte immutable table. The first full-key match is reused.
 * A match skips the new-client password, insertion/eviction, replay, timestamp,
 * activity, permission and shared-secret mutation block. Flood receipt marks
 * only the existing client's out-path as the permitted client-record mutation.
 * Repeater replies keep using the caller-derived anonymous secret; room replies
 * use the stored ACL secret. Missing keys return found=0, index=0xFF, no secret
 * source and no client mutation so the caller can enter its separately bounded
 * unmatched-client branch.
 *
 * This boundary does not instantiate ClientACL, load/save storage, mutate a
 * contact, authorize nonblank passwords, derive secrets, apply replay/session
 * transitions, create a response, dispatch, route, schedule, or claim RF.
 */
bool d1l_meshcore_oracle_resolve_existing_acl_blank_login(
    uint8_t server_advert_type,
    uint8_t is_route_flood,
    const uint8_t *blank_password,
    size_t password_len,
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t *acl_public_keys,
    size_t acl_entry_count,
    uint8_t *out_found,
    uint8_t *out_client_index,
    uint8_t *out_response_secret_source,
    uint8_t *out_client_mutation_mask);

/*
 * Deterministic projection of ClientACL::putClient plus the post-password
 * mutation block shared by the pinned repeater and room login handlers. It
 * models the exact fields changed by those sources: full-key reuse or append,
 * least-active non-admin eviction (including the pinned last-slot fallback),
 * replay rejection, role-bit replacement, shared secret, sender timestamp,
 * local activity time, room sync/pending/push-failure fields, flood out-path
 * invalidation and the contacts-dirty decision. A structurally valid replay
 * denial returns true with accepted=0; insertion/eviction that occurred before
 * the replay check remains visible exactly as upstream leaves it.
 *
 * Input records are immutable and the result is published only after complete
 * validation. This projection excludes filesystem load/save, full ClientInfo
 * path bytes and unmodified room scheduling fields, identity signature/secret
 * derivation, password comparison, response creation, dispatch, routing,
 * scheduling, persistence execution, or RF.
 */
bool d1l_meshcore_oracle_apply_authorized_login_acl_transition(
    uint8_t server_advert_type,
    uint8_t is_route_flood,
    uint8_t authorized_permissions,
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t shared_secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t sender_timestamp,
    uint32_t current_time,
    uint32_t room_sync_since,
    const d1l_meshcore_oracle_login_acl_record_t *records,
    size_t record_count,
    d1l_meshcore_oracle_login_acl_transition_t *out_transition);

/*
 * Pinned MeshCore group-channel hash and datagram crypto/framing. This hash
 * helper reproduces BaseChatMesh::setChannel: a padded 16-byte secret with an
 * all-zero upper half hashes 16 bytes, while other secrets hash all 32. The
 * separate addChannel path carries an explicit decoded length and is outside
 * this inferred-length ABI. Group packet crypto is AES-128 ECB with
 * zero padding and a leading two-byte truncated HMAC-SHA-256 exactly as in
 * Utils.cpp. Parsing returns the padded plaintext length because the wire
 * format carries no original plaintext length. Creation intentionally accepts
 * only non-empty application datagrams; application-level group text and group
 * data fields determine their own logical length.
 *
 * These functions do not claim channel persistence, dispatcher callbacks,
 * duplicate suppression, routing, RF delivery, or group-message UI behavior.
 */
bool d1l_meshcore_oracle_group_channel_hash(
    const uint8_t secret[D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES],
    uint8_t *out_hash);

bool d1l_meshcore_oracle_create_group_packet(
    uint8_t payload_type,
    uint8_t channel_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES],
    const uint8_t *plaintext,
    size_t plaintext_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_group_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t channel_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES],
    uint8_t *out_plaintext,
    size_t plaintext_capacity,
    size_t *out_plaintext_len);

/*
 * Canonical BaseChatMesh plain-DM framing with a caller-supplied shared
 * secret and one-byte destination/source hashes. Creation covers the pinned
 * timestamp, low two attempt bits, and attempt>3 trailing full-attempt layout,
 * then delegates AES/HMAC to mesh::Utils. Parsing authenticates before
 * returning fields and accepts exact-block plaintext or only the canonical
 * zero padding produced by that layout. Empty text is valid; embedded NUL text
 * is not.
 *
 * These functions do not derive an Ed25519 shared secret, search contacts,
 * dispatch messages, derive/send/correlate ACKs, select a route, update
 * retained state, or claim RF delivery.
 */
bool d1l_meshcore_oracle_create_dm_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t timestamp,
    uint8_t attempt,
    const uint8_t *text,
    size_t text_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_dm_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t *out_timestamp,
    uint8_t *out_attempt,
    uint8_t *out_text,
    size_t text_capacity,
    size_t *out_text_len);

/*
 * Derives the four-byte BaseChatMesh plain-DM expected ACK hash for every
 * canonical text length.
 */
bool d1l_meshcore_oracle_dm_expected_ack_hash(
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    uint32_t timestamp,
    uint8_t attempt,
    const uint8_t *text,
    size_t text_len,
    uint8_t out_expected_ack[D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES]);

/*
 * Builds the complete six-byte ACK body emitted by the pinned receive path.
 * The caller supplies the final uniqueness byte that upstream obtains from
 * its RNG. Bytes four and five are respectively the optional extended attempt
 * (zero for attempts 0..3) and that uniqueness byte. Normal messages for which
 * 5 + text_len is an exact AES-block multiple are rejected here: upstream
 * derives the four-byte expected hash deterministically but reads the ACK
 * body's fifth byte beyond its initialized decrypted/terminator bytes.
 */
bool d1l_meshcore_oracle_dm_expected_ack(
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    uint32_t timestamp,
    uint8_t attempt,
    const uint8_t *text,
    size_t text_len,
    uint8_t uniqueness_byte,
    uint8_t out_ack[D1L_MESHCORE_ORACLE_DM_ACK_BYTES]);

/*
 * Canonical Mesh::createPathReturn framing for a plain-DM ACK extra. The
 * encoded return-path length uses Packet's two-bit hash-size / six-bit count
 * representation; source/destination hashes, the shared secret, ACK bytes,
 * and RNG-derived uniqueness byte are all caller-supplied. Parsing
 * authenticates before returning fields and accepts only the exact six-byte
 * ACK extra plus canonical zero padding. The separate expected-hash API still
 * covers normal messages whose full six-byte upstream ACK body is undefined at
 * an exact cipher-block boundary.
 *
 * These functions do not derive identities or shared secrets, consume RNG,
 * select/store a route, dispatch/correlate an ACK, update delivery state, send
 * a packet, or claim RF behavior.
 */
bool d1l_meshcore_oracle_create_dm_ack_path_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t encoded_return_path_len,
    const uint8_t *return_path,
    const uint8_t ack[D1L_MESHCORE_ORACLE_DM_ACK_BYTES],
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_dm_ack_path_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t *out_encoded_return_path_len,
    uint8_t *out_return_path,
    size_t return_path_capacity,
    size_t *out_return_path_bytes,
    uint8_t out_ack[D1L_MESHCORE_ORACLE_DM_ACK_BYTES]);

/*
 * General deterministic Mesh::createPathReturn framing. The caller supplies
 * the one-byte peer hashes and already-derived shared secret because identity
 * and key exchange remain outside this ABI. The extra form copies the full
 * upstream extra-type byte on creation; parsing returns its low nibble exactly
 * as Mesh::onRecvPacket does. expected_extra_len disambiguates application
 * data from AES zero padding, which the wire format does not encode.
 *
 * The unique form reproduces createPathReturn's no-extra branch with the four
 * RNG bytes supplied by the caller. Both constructors return a pre-route PATH
 * packet. Applying prepare_flood, prepare_direct, or prepare_zero_hop pins the
 * caller-selected direct/flood and transport-code header semantics only.
 *
 * These functions do not derive identities or secrets, consume RNG, select or
 * store a route, dispatch a PATH, update a contact, schedule/forward a packet,
 * or claim RF behavior.
 */
bool d1l_meshcore_oracle_create_path_return_extra_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t encoded_return_path_len,
    const uint8_t *return_path,
    uint8_t extra_type,
    const uint8_t *extra,
    size_t extra_len,
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_create_path_return_unique_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t encoded_return_path_len,
    const uint8_t *return_path,
    const uint8_t uniqueness[D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES],
    d1l_meshcore_oracle_packet_t *out_packet);

bool d1l_meshcore_oracle_parse_path_return_extra_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    size_t expected_extra_len,
    uint8_t *out_encoded_return_path_len,
    uint8_t *out_return_path,
    size_t return_path_capacity,
    size_t *out_return_path_bytes,
    uint8_t *out_extra_type,
    uint8_t *out_extra,
    size_t extra_capacity,
    size_t *out_extra_len);

bool d1l_meshcore_oracle_parse_path_return_unique_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t *out_encoded_return_path_len,
    uint8_t *out_return_path,
    size_t return_path_capacity,
    size_t *out_return_path_bytes,
    uint8_t out_uniqueness
        [D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES]);

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
