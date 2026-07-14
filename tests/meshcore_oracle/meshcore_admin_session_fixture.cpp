#include "meshcore_admin_session_fixture.h"

#include "meshcore_oracle.h"

#include "Packet.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace d1l::meshcore::host_fixture {
namespace {

constexpr uint8_t kClientHash = 0x42U;
constexpr uint8_t kServerHash = 0xA7U;
constexpr uint8_t kGetStatus = 0x01U;
constexpr uint8_t kAdminPermissions = 0x03U;
constexpr uint32_t kLoginTimestamp = 0x01020304U;
constexpr uint32_t kRequestTag = 0x01020305U;
constexpr uint32_t kSecondRequestTag = 0x01020306U;
constexpr uint32_t kServerTimestamp = 0x55667788U;
constexpr uint32_t kRequestDeadline = 1200U;
constexpr std::size_t kRequestBytes = 13U;
constexpr std::size_t kRepeaterStatsBytes = 56U;
constexpr std::size_t kStatusResponseBytes = 4U + kRepeaterStatsBytes;
constexpr std::array<uint8_t, 5U> kAdminPassword = {
    'a', 'd', 'm', 'i', 'n'};
constexpr std::array<uint8_t, 5U> kWrongPassword = {
    'g', 'u', 'e', 's', 't'};
constexpr std::array<uint8_t, 5U> kGuestPassword = {
    'r', 'e', 'a', 'd', 'r'};
constexpr std::array<uint8_t, 4U> kLoginUniqueness = {
    0x10U, 0x20U, 0x30U, 0x40U};
constexpr std::array<uint8_t, 4U> kRequestUniqueness = {
    0xA1U, 0xB2U, 0xC3U, 0xD4U};
constexpr std::array<uint8_t, 2U> kRoutePath = {0x31U, 0x32U};
constexpr uint8_t kEncodedRoutePath = 0x02U;

struct RepeaterStatsFixture {
    uint16_t batt_milli_volts;
    uint16_t curr_tx_queue_len;
    int16_t noise_floor;
    int16_t last_rssi;
    uint32_t n_packets_recv;
    uint32_t n_packets_sent;
    uint32_t total_air_time_secs;
    uint32_t total_up_time_secs;
    uint32_t n_sent_flood;
    uint32_t n_sent_direct;
    uint32_t n_recv_flood;
    uint32_t n_recv_direct;
    uint16_t err_events;
    int16_t last_snr;
    uint16_t n_direct_dups;
    uint16_t n_flood_dups;
    uint32_t total_rx_air_time_secs;
    uint32_t n_recv_errors;
};

constexpr RepeaterStatsFixture kStatusStats = {
    3700U,       // batt_milli_volts
    7U,          // curr_tx_queue_len
    -117,        // noise_floor
    -83,         // last_rssi
    0x01020304U, // n_packets_recv
    0x11121314U, // n_packets_sent
    0x21222324U, // total_air_time_secs
    0x31323334U, // total_up_time_secs
    0x41424344U, // n_sent_flood
    0x51525354U, // n_sent_direct
    0x61626364U, // n_recv_flood
    0x71727374U, // n_recv_direct
    0x8182U,     // err_events
    -20,         // last_snr, x4
    0x9192U,     // n_direct_dups
    0xA1A2U,     // n_flood_dups
    0xB1B2B3B4U, // total_rx_air_time_secs
    0xC1C2C3C4U, // n_recv_errors
};

static_assert(kStatusResponseBytes == 60U,
              "GET_STATUS response must be tag plus RepeaterStats");
static_assert(kRepeaterStatsBytes ==
                  (8U * sizeof(uint16_t)) + (10U * sizeof(uint32_t)),
              "RepeaterStats wire field widths changed");

constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
    kClientPublicKey = {
        0xCAU, 0xFEU, 0xBAU, 0xBEU, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU};
constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
    kPrefixCollisionClientKey = {
        0xCAU, 0xFEU, 0xBAU, 0xBEU, 0xF4U, 0xF5U, 0xF6U, 0xF7U,
        0xF8U, 0xF9U, 0xFAU, 0xFBU, 0xFCU, 0xFDU, 0xFEU, 0xFFU,
        0xE0U, 0xE1U, 0xE2U, 0xE3U, 0xE4U, 0xE5U, 0xE6U, 0xE7U,
        0xE8U, 0xE9U, 0xEAU, 0xEBU, 0xECU, 0xEDU, 0xEEU, 0xEFU};
constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
    kServerPublicKey = {
        0x53U, 0x45U, 0x52U, 0x56U, 0x24U, 0x25U, 0x26U, 0x27U,
        0x28U, 0x29U, 0x2AU, 0x2BU, 0x2CU, 0x2DU, 0x2EU, 0x2FU,
        0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U,
        0x38U, 0x39U, 0x3AU, 0x3BU, 0x3CU, 0x3DU, 0x3EU, 0x3FU};
constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
    kOtherServerPublicKey = {
        0x53U, 0x45U, 0x52U, 0x56U, 0xD4U, 0xD5U, 0xD6U, 0xD7U,
        0xD8U, 0xD9U, 0xDAU, 0xDBU, 0xDCU, 0xDDU, 0xDEU, 0xDFU,
        0xC0U, 0xC1U, 0xC2U, 0xC3U, 0xC4U, 0xC5U, 0xC6U, 0xC7U,
        0xC8U, 0xC9U, 0xCAU, 0xCBU, 0xCCU, 0xCDU, 0xCEU, 0xCFU};
constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES>
    kSharedSecret = {
        0x80U, 0x81U, 0x82U, 0x83U, 0x84U, 0x85U, 0x86U, 0x87U,
        0x88U, 0x89U, 0x8AU, 0x8BU, 0x8CU, 0x8DU, 0x8EU, 0x8FU,
        0x90U, 0x91U, 0x92U, 0x93U, 0x94U, 0x95U, 0x96U, 0x97U,
        0x98U, 0x99U, 0x9AU, 0x9BU, 0x9CU, 0x9DU, 0x9EU, 0x9FU};

using Key = std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>;
using Secret =
    std::array<uint8_t, D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES>;
using Packet = d1l_meshcore_oracle_packet_t;
using AclRecord = d1l_meshcore_oracle_login_acl_record_t;

void secure_zero(void *data, std::size_t length)
{
    auto *bytes = static_cast<volatile uint8_t *>(data);
    while (length > 0U) {
        *bytes++ = 0U;
        --length;
    }
}

template <typename Container>
bool is_zero(const Container &value)
{
    return std::all_of(value.begin(), value.end(), [](uint8_t byte) {
        return byte == 0U;
    });
}

bool same_key(const uint8_t *left, const Key &right)
{
    return left != nullptr &&
        std::memcmp(left, right.data(), right.size()) == 0;
}

uint32_t read_le32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8U) |
        (static_cast<uint32_t>(bytes[2]) << 16U) |
        (static_cast<uint32_t>(bytes[3]) << 24U);
}

void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
}

void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
    bytes[2] = static_cast<uint8_t>(value >> 16U);
    bytes[3] = static_cast<uint8_t>(value >> 24U);
}

template <typename Container>
std::string hex_bytes(const Container &bytes)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return stream.str();
}

std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES>
login_plaintext()
{
    std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES> value{};
    write_le32(value.data(), kServerTimestamp);
    value[4] = 0U;
    value[5] = 0U;
    value[6] = 1U;
    value[7] = kAdminPermissions;
    std::copy(kLoginUniqueness.begin(), kLoginUniqueness.end(),
              value.begin() + 8);
    value[12] = 2U;
    return value;
}

std::array<uint8_t, kRequestBytes> request_plaintext(uint32_t tag)
{
    std::array<uint8_t, kRequestBytes> value{};
    write_le32(value.data(), tag);
    value[4] = kGetStatus;
    std::copy(kRequestUniqueness.begin(), kRequestUniqueness.end(),
              value.begin() + 9);
    return value;
}

std::array<uint8_t, kStatusResponseBytes> response_plaintext(uint32_t tag)
{
    std::array<uint8_t, kStatusResponseBytes> value{};
    write_le32(value.data(), tag);
    std::size_t offset = 4U;
    const auto append_u16 = [&value, &offset](uint16_t field) {
        write_le16(value.data() + offset, field);
        offset += sizeof(field);
    };
    const auto append_i16 = [&append_u16](int16_t field) {
        append_u16(static_cast<uint16_t>(field));
    };
    const auto append_u32 = [&value, &offset](uint32_t field) {
        write_le32(value.data() + offset, field);
        offset += sizeof(field);
    };

    // RepeaterStats wire order is pinned to simple_repeater/MyMesh.h.  Every
    // field is serialized explicitly so host ABI padding/endian cannot alter
    // the fixture.
    append_u16(kStatusStats.batt_milli_volts);
    append_u16(kStatusStats.curr_tx_queue_len);
    append_i16(kStatusStats.noise_floor);
    append_i16(kStatusStats.last_rssi);
    append_u32(kStatusStats.n_packets_recv);
    append_u32(kStatusStats.n_packets_sent);
    append_u32(kStatusStats.total_air_time_secs);
    append_u32(kStatusStats.total_up_time_secs);
    append_u32(kStatusStats.n_sent_flood);
    append_u32(kStatusStats.n_sent_direct);
    append_u32(kStatusStats.n_recv_flood);
    append_u32(kStatusStats.n_recv_direct);
    append_u16(kStatusStats.err_events);
    append_i16(kStatusStats.last_snr);
    append_u16(kStatusStats.n_direct_dups);
    append_u16(kStatusStats.n_flood_dups);
    append_u32(kStatusStats.total_rx_air_time_secs);
    append_u32(kStatusStats.n_recv_errors);
    return value;
}

bool prepare_route(Packet *packet, bool flood)
{
    uint8_t priority = 0xFFU;
    if (flood) {
        return d1l_meshcore_oracle_prepare_flood(
            packet, 1U, 0U, nullptr, &priority) && priority >= 1U;
    }
    return d1l_meshcore_oracle_prepare_direct(
        packet, kRoutePath.data(), kEncodedRoutePath, &priority) &&
        priority <= 1U;
}

struct ClientState {
    bool login_pending = false;
    bool logged_in = false;
    bool admin = false;
    bool request_pending = false;
    uint8_t permissions = 0U;
    uint32_t login_deadline = 0U;
    uint32_t request_deadline = 0U;
    uint32_t pending_tag = 0U;
    Key expected_login_server{};
    Key server_key{};
    Key pending_server_key{};
    Secret login_secret{};
    Secret session_secret{};
    Secret pending_secret{};
    std::vector<uint32_t> completed_tags;

    void begin_login(uint32_t deadline)
    {
        login_pending = true;
        login_deadline = deadline;
        expected_login_server = kServerPublicKey;
        login_secret = kSharedSecret;
    }

    void clear_login_pending()
    {
        login_pending = false;
        login_deadline = 0U;
        secure_zero(expected_login_server.data(), expected_login_server.size());
        secure_zero(login_secret.data(), login_secret.size());
    }

    void expire_login(uint32_t now)
    {
        if (login_pending && now > login_deadline) {
            clear_login_pending();
        }
    }

    void begin_request(uint32_t tag, uint32_t deadline)
    {
        request_pending = true;
        pending_tag = tag;
        request_deadline = deadline;
        pending_server_key = server_key;
        pending_secret = session_secret;
    }

    void clear_request_pending()
    {
        request_pending = false;
        pending_tag = 0U;
        request_deadline = 0U;
        secure_zero(pending_server_key.data(), pending_server_key.size());
        secure_zero(pending_secret.data(), pending_secret.size());
    }

    void expire_request(uint32_t now)
    {
        if (request_pending && now > request_deadline) {
            clear_request_pending();
        }
    }

    void logout()
    {
        clear_login_pending();
        clear_request_pending();
        logged_in = false;
        admin = false;
        permissions = 0U;
        secure_zero(server_key.data(), server_key.size());
        secure_zero(session_secret.data(), session_secret.size());
        completed_tags.clear();
    }
};

struct ServerState {
    bool has_client = false;
    AclRecord client{};

    void logout()
    {
        has_client = false;
        secure_zero(&client, sizeof(client));
    }
};

struct LoginDelivery {
    Packet packet{};
    bool path_return = false;
    bool ready = false;
    uint8_t creation_kind = D1L_MESHCORE_ORACLE_CREATION_NONE;
    uint8_t dispatch_mode = D1L_MESHCORE_ORACLE_DISPATCH_NONE;
};

struct ResponseDelivery {
    Packet packet{};
    bool path_return = false;
    bool ready = false;
};

bool validate_login_plaintext(
    const std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES>
        &plaintext,
    uint32_t *server_timestamp, uint8_t *permissions)
{
    if (plaintext[4] != 0U || plaintext[5] != 0U || plaintext[6] != 1U ||
        plaintext[7] != kAdminPermissions || plaintext[12] != 2U ||
        !std::equal(kLoginUniqueness.begin(), kLoginUniqueness.end(),
                    plaintext.begin() + 8)) {
        return false;
    }
    *server_timestamp = read_le32(plaintext.data());
    *permissions = plaintext[7];
    return *server_timestamp == kServerTimestamp;
}

bool server_login(ServerState *server, const Key &sender_identity,
                  const Packet &packet, bool request_was_flood,
                  LoginDelivery *delivery)
{
    if (server == nullptr || delivery == nullptr ||
        !same_key(sender_identity.data(), kClientPublicKey)) {
        return false;
    }

    uint32_t timestamp = 0U;
    uint32_t sync_since = 0U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES>
        password{};
    std::size_t password_len = 0U;
    if (!d1l_meshcore_oracle_parse_login_request_packet(
            &packet, kServerHash, sender_identity.data(),
            kSharedSecret.data(), 0U, &timestamp, &sync_since,
            password.data(), password.size(), &password_len) ||
        sync_since != 0U) {
        return false;
    }

    uint8_t authorized = 0U;
    uint8_t permissions = 0U;
    if (!d1l_meshcore_oracle_classify_unmatched_login_password(
            D1L_MESHCORE_ADVERT_TYPE_REPEATER, 0U, password.data(),
            password_len, kAdminPassword.data(), kAdminPassword.size(),
            kGuestPassword.data(), kGuestPassword.size(), &authorized,
            &permissions) ||
        authorized != 1U || permissions != kAdminPermissions) {
        return false;
    }

    std::array<AclRecord, D1L_MESHCORE_ORACLE_MAX_LOGIN_ACL_ENTRIES>
        records{};
    std::size_t record_count = 0U;
    if (server->has_client) {
        records[0] = server->client;
        record_count = 1U;
    }
    d1l_meshcore_oracle_login_acl_transition_t transition{};
    if (!d1l_meshcore_oracle_apply_authorized_login_acl_transition(
            D1L_MESHCORE_ADVERT_TYPE_REPEATER,
            request_was_flood ? 1U : 0U, permissions,
            sender_identity.data(), kSharedSecret.data(), timestamp, 100U,
            0U, records.data(), record_count, &transition) ||
        transition.accepted != 1U || transition.record_count != 1U ||
        transition.client_index != 0U) {
        return false;
    }
    server->client = transition.records[0];
    server->has_client = true;

    Packet canonical{};
    if (!d1l_meshcore_oracle_create_login_response_packet(
            D1L_MESHCORE_ADVERT_TYPE_REPEATER, kClientHash, kServerHash,
            server->client.shared_secret, kServerTimestamp,
            server->client.permissions, kLoginUniqueness.data(),
            &canonical)) {
        return false;
    }
    uint32_t parsed_timestamp = 0U;
    uint8_t parsed_permissions = 0U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES>
        parsed_uniqueness{};
    if (!d1l_meshcore_oracle_parse_login_response_packet(
            &canonical, D1L_MESHCORE_ADVERT_TYPE_REPEATER, kClientHash,
            kServerHash, server->client.shared_secret, &parsed_timestamp,
            &parsed_permissions, parsed_uniqueness.data()) ||
        parsed_timestamp != kServerTimestamp ||
        parsed_permissions != kAdminPermissions ||
        parsed_uniqueness != kLoginUniqueness) {
        return false;
    }

    d1l_meshcore_oracle_login_response_dispatch_transition_t dispatch{};
    if (!d1l_meshcore_oracle_apply_login_response_dispatch_transition(
            D1L_MESHCORE_ADVERT_TYPE_REPEATER,
            request_was_flood ? 1U : 0U, 1U,
            D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES, 1U, &server->client,
            &dispatch) ||
        dispatch.dispatch_attempted != 1U ||
        dispatch.dispatch_mode != D1L_MESHCORE_ORACLE_DISPATCH_FLOOD ||
        dispatch.dispatch_delay_ms !=
            D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_DELAY_MS) {
        return false;
    }

    if (request_was_flood) {
        const auto plaintext = login_plaintext();
        if (!d1l_meshcore_oracle_create_path_return_extra_packet(
                kClientHash, kServerHash, server->client.shared_secret,
                kEncodedRoutePath, kRoutePath.data(), PAYLOAD_TYPE_RESPONSE,
                plaintext.data(), plaintext.size(), &delivery->packet) ||
            !prepare_route(&delivery->packet, true)) {
            return false;
        }
        delivery->path_return = true;
    } else {
        delivery->packet = canonical;
        if (!prepare_route(&delivery->packet, true)) {
            return false;
        }
    }
    delivery->ready = true;
    delivery->creation_kind = dispatch.response_creation_kind;
    delivery->dispatch_mode = dispatch.dispatch_mode;
    return true;
}

bool client_receive_login(ClientState *client, const Key &sender_identity,
                          const LoginDelivery &delivery, uint32_t now)
{
    if (client == nullptr || !client->login_pending || !delivery.ready ||
        now > client->login_deadline ||
        !same_key(sender_identity.data(), client->expected_login_server)) {
        return false;
    }

    uint32_t server_timestamp = 0U;
    uint8_t permissions = 0U;
    if (delivery.path_return) {
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES> path{};
        std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES>
            plaintext{};
        uint8_t encoded_path_len = 0U;
        std::size_t path_bytes = 0U;
        uint8_t extra_type = 0U;
        std::size_t extra_len = 0U;
        if (!d1l_meshcore_oracle_parse_path_return_extra_packet(
                &delivery.packet, kClientHash, kServerHash,
                client->login_secret.data(), plaintext.size(),
                &encoded_path_len, path.data(), path.size(), &path_bytes,
                &extra_type, plaintext.data(), plaintext.size(), &extra_len) ||
            encoded_path_len != kEncodedRoutePath ||
            path_bytes != kRoutePath.size() || extra_type != PAYLOAD_TYPE_RESPONSE ||
            extra_len != plaintext.size() ||
            !validate_login_plaintext(plaintext, &server_timestamp,
                                      &permissions)) {
            return false;
        }
    } else {
        std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES>
            uniqueness{};
        if (!d1l_meshcore_oracle_parse_login_response_packet(
                &delivery.packet, D1L_MESHCORE_ADVERT_TYPE_REPEATER,
                kClientHash, kServerHash, client->login_secret.data(),
                &server_timestamp, &permissions, uniqueness.data()) ||
            server_timestamp != kServerTimestamp ||
            permissions != kAdminPermissions ||
            uniqueness != kLoginUniqueness) {
            return false;
        }
    }

    client->server_key = sender_identity;
    client->session_secret = client->login_secret;
    client->permissions = permissions;
    client->admin = (permissions & 0x03U) == kAdminPermissions;
    client->logged_in = client->admin;
    client->clear_login_pending();
    return client->logged_in;
}

bool create_login_request(const Key &sender_key, const Secret &secret,
                          const std::array<uint8_t, 5U> &password,
                          bool flood, Packet *packet)
{
    return d1l_meshcore_oracle_create_login_request_packet(
               kServerHash, sender_key.data(), secret.data(),
               kLoginTimestamp, 0U, 0U, password.data(), password.size(),
               packet) &&
        prepare_route(packet, flood);
}

bool create_request(const Secret &secret, uint32_t tag, bool flood,
                    Packet *packet)
{
    const auto plaintext = request_plaintext(tag);
    return d1l_meshcore_oracle_create_request_response_packet(
               PAYLOAD_TYPE_REQ, kServerHash, kClientHash, secret.data(),
               plaintext.data(), plaintext.size(), packet) &&
        prepare_route(packet, flood);
}

bool server_request(ServerState *server, const Key &sender_identity,
                    const Packet &packet, bool request_was_flood,
                    ResponseDelivery *delivery)
{
    if (server == nullptr || delivery == nullptr || !server->has_client ||
        !same_key(sender_identity.data(), kClientPublicKey) ||
        std::memcmp(sender_identity.data(), server->client.public_key,
                    sender_identity.size()) != 0 ||
        (server->client.permissions & 0x03U) != kAdminPermissions) {
        return false;
    }

    std::array<uint8_t, kRequestBytes> plaintext{};
    if (!d1l_meshcore_oracle_parse_request_response_packet(
            &packet, PAYLOAD_TYPE_REQ, kServerHash, kClientHash,
            server->client.shared_secret, plaintext.size(), plaintext.data(),
            plaintext.size()) ||
        plaintext[4] != kGetStatus ||
        !std::all_of(plaintext.begin() + 5, plaintext.begin() + 9,
                     [](uint8_t byte) { return byte == 0U; }) ||
        !std::equal(kRequestUniqueness.begin(), kRequestUniqueness.end(),
                    plaintext.begin() + 9)) {
        return false;
    }

    const uint32_t tag = read_le32(plaintext.data());
    d1l_meshcore_oracle_authenticated_request_transition_t transition{};
    if (!d1l_meshcore_oracle_apply_authenticated_request_replay_transition(
            D1L_MESHCORE_ADVERT_TYPE_REPEATER,
            request_was_flood ? 0U : 1U, kGetStatus, 1U,
            plaintext.size(), tag, 101U, 0U, &server->client,
            &transition) ||
        transition.replay_accepted != 1U ||
        transition.handler_invoked != 1U ||
        transition.state_committed != 1U ||
        transition.response_attempt_eligible != 1U) {
        return false;
    }
    server->client = transition.record;

    const auto response = response_plaintext(tag);
    if (request_was_flood) {
        if (!d1l_meshcore_oracle_create_path_return_extra_packet(
                kClientHash, kServerHash, server->client.shared_secret,
                kEncodedRoutePath, kRoutePath.data(), PAYLOAD_TYPE_RESPONSE,
                response.data(), response.size(), &delivery->packet) ||
            !prepare_route(&delivery->packet, true)) {
            return false;
        }
        delivery->path_return = true;
    } else if (!d1l_meshcore_oracle_create_request_response_packet(
                   PAYLOAD_TYPE_RESPONSE, kClientHash, kServerHash,
                   server->client.shared_secret, response.data(),
                   response.size(), &delivery->packet) ||
               !prepare_route(&delivery->packet, false)) {
        return false;
    }
    delivery->ready = true;
    return true;
}

bool client_receive_response(ClientState *client, const Key &sender_identity,
                             const ResponseDelivery &delivery, uint32_t now)
{
    if (client == nullptr || !delivery.ready || !client->request_pending ||
        !same_key(sender_identity.data(), client->pending_server_key)) {
        return false;
    }
    if (now > client->request_deadline) {
        client->expire_request(now);
        return false;
    }

    std::array<uint8_t, kStatusResponseBytes> plaintext{};
    if (delivery.path_return) {
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES> path{};
        uint8_t encoded_path_len = 0U;
        std::size_t path_bytes = 0U;
        uint8_t extra_type = 0U;
        std::size_t extra_len = 0U;
        if (!d1l_meshcore_oracle_parse_path_return_extra_packet(
                &delivery.packet, kClientHash, kServerHash,
                client->pending_secret.data(), plaintext.size(),
                &encoded_path_len, path.data(), path.size(), &path_bytes,
                &extra_type, plaintext.data(), plaintext.size(), &extra_len) ||
            encoded_path_len != kEncodedRoutePath ||
            path_bytes != kRoutePath.size() || extra_type != PAYLOAD_TYPE_RESPONSE ||
            extra_len != plaintext.size()) {
            return false;
        }
    } else if (!d1l_meshcore_oracle_parse_request_response_packet(
                   &delivery.packet, PAYLOAD_TYPE_RESPONSE, kClientHash,
                   kServerHash, client->pending_secret.data(),
                   plaintext.size(), plaintext.data(), plaintext.size())) {
        return false;
    }

    const uint32_t tag = read_le32(plaintext.data());
    const auto expected = response_plaintext(tag);
    if (tag != client->pending_tag ||
        std::find(client->completed_tags.begin(), client->completed_tags.end(),
                  tag) != client->completed_tags.end() ||
        !std::equal(expected.begin() + 4, expected.end(),
                    plaintext.begin() + 4)) {
        return false;
    }
    client->completed_tags.push_back(tag);
    client->clear_request_pending();
    return true;
}

bool establish_session(bool flood, ClientState *client, ServerState *server,
                       ResponseDelivery *status_delivery = nullptr)
{
    client->begin_login(500U);
    Packet login{};
    LoginDelivery login_delivery{};
    if (!create_login_request(kClientPublicKey, kSharedSecret, kAdminPassword,
                              flood, &login) ||
        !server_login(server, kClientPublicKey, login, flood,
                      &login_delivery) ||
        !client_receive_login(client, kServerPublicKey, login_delivery,
                              400U) ||
        !client->logged_in || !client->admin ||
        client->permissions != kAdminPermissions) {
        return false;
    }

    client->begin_request(kRequestTag, kRequestDeadline);
    Packet request{};
    ResponseDelivery delivery{};
    if (!create_request(client->session_secret, kRequestTag, flood,
                        &request) ||
        !server_request(server, kClientPublicKey, request, flood, &delivery) ||
        !client_receive_response(client, kServerPublicKey, delivery, 1100U)) {
        return false;
    }
    if (status_delivery != nullptr) {
        *status_delivery = delivery;
    }
    return true;
}

void add_check(bool condition, const std::string &name,
               std::size_t *counter, std::vector<std::string> *failures)
{
    ++(*counter);
    if (!condition) {
        failures->push_back(name);
    }
}

ResponseDelivery make_direct_response(const Secret &secret, uint32_t tag)
{
    ResponseDelivery delivery{};
    const auto plaintext = response_plaintext(tag);
    delivery.ready =
        d1l_meshcore_oracle_create_request_response_packet(
            PAYLOAD_TYPE_RESPONSE, kClientHash, kServerHash, secret.data(),
            plaintext.data(), plaintext.size(), &delivery.packet) &&
        prepare_route(&delivery.packet, false);
    return delivery;
}

std::string make_receipt_json(bool passed, std::size_t positive_checks,
                              std::size_t negative_checks,
                              const std::vector<std::pair<std::string, bool>>
                                  &negative_matrix,
                              bool request_timeout_zeroized,
                              bool login_timeout_zeroized,
                              bool logout_zeroized)
{
    const auto request = request_plaintext(kRequestTag);
    const auto login_response = login_plaintext();
    const auto status_response = response_plaintext(kRequestTag);
    std::ostringstream stream;
    stream
        << "{\"schema_version\":1"
        << ",\"artifact_type\":\"meshcore_host_admin_session_fixture\""
        << ",\"receipt_id\":\"RCPT-WP04-HOST-ADMIN-SESSION-20260714\""
        << ",\"status\":\"" << (passed ? "pass" : "fail") << "\""
        << ",\"host_only\":true"
        << ",\"production_runtime_proven\":false"
        << ",\"wp18_closure_eligible\":false"
        << ",\"rf_closure_eligible\":false"
        << ",\"ui_closure_eligible\":false"
        << ",\"hardware_closure_eligible\":false"
        << ",\"upstream_commit\":\""
        << D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT << "\""
        << ",\"source_contract\":{"
        << "\"login_request\":\"BaseChatMesh::sendLogin plus Mesh::createAnonDatagram\""
        << ",\"login_server\":\"simple_repeater::handleLoginReq and ClientACL full-key rules\""
        << ",\"request\":\"BaseChatMesh::sendRequest(uint8_t) exact 13-byte schema\""
        << ",\"response\":\"simple_repeater::handleRequest reflected LE32 tag plus explicit little-endian 56-byte RepeaterStats in MyMesh.h order\""
        << ",\"direct_and_flood_return\":\"simple_repeater::onAnonDataRecv and onPeerDataRecv\"}"
        << ",\"invariants\":{"
        << "\"identity_bytes\":32,\"prefix_authorization\":false"
        << ",\"login_payload_type\":7,\"request_payload_type\":0"
        << ",\"response_payload_type\":1"
        << ",\"admin_permissions\":3"
        << ",\"login_response_bytes\":13"
        << ",\"get_status_request_bytes\":13"
        << ",\"repeater_stats_bytes\":" << kRepeaterStatsBytes
        << ",\"repeater_stats_fields\":18"
        << ",\"get_status_response_bytes\":" << kStatusResponseBytes
        << ",\"get_status_type\":1"
        << ",\"login_timestamp\":" << kLoginTimestamp
        << ",\"request_tag\":" << kRequestTag << "}"
        << ",\"canonical_bytes\":{"
        << "\"get_status_request_hex\":\"" << hex_bytes(request) << "\""
        << ",\"login_response_hex\":\"" << hex_bytes(login_response)
        << "\""
        << ",\"get_status_response_hex\":\"" << hex_bytes(status_response)
        << "\"}"
        << ",\"repeater_stats\":{"
        << "\"batt_milli_volts\":" << kStatusStats.batt_milli_volts
        << ",\"curr_tx_queue_len\":" << kStatusStats.curr_tx_queue_len
        << ",\"noise_floor\":" << kStatusStats.noise_floor
        << ",\"last_rssi\":" << kStatusStats.last_rssi
        << ",\"n_packets_recv\":" << kStatusStats.n_packets_recv
        << ",\"n_packets_sent\":" << kStatusStats.n_packets_sent
        << ",\"total_air_time_secs\":" << kStatusStats.total_air_time_secs
        << ",\"total_up_time_secs\":" << kStatusStats.total_up_time_secs
        << ",\"n_sent_flood\":" << kStatusStats.n_sent_flood
        << ",\"n_sent_direct\":" << kStatusStats.n_sent_direct
        << ",\"n_recv_flood\":" << kStatusStats.n_recv_flood
        << ",\"n_recv_direct\":" << kStatusStats.n_recv_direct
        << ",\"err_events\":" << kStatusStats.err_events
        << ",\"last_snr\":" << kStatusStats.last_snr
        << ",\"n_direct_dups\":" << kStatusStats.n_direct_dups
        << ",\"n_flood_dups\":" << kStatusStats.n_flood_dups
        << ",\"total_rx_air_time_secs\":"
        << kStatusStats.total_rx_air_time_secs
        << ",\"n_recv_errors\":" << kStatusStats.n_recv_errors << "}"
        << ",\"transcript\":["
        << "{\"seq\":1,\"case\":\"direct\",\"actor\":\"client\",\"event\":\"ANON_REQ admin login\"}"
        << ",{\"seq\":2,\"case\":\"direct\",\"actor\":\"server\",\"event\":\"full-key ACL admin established\"}"
        << ",{\"seq\":3,\"case\":\"direct\",\"actor\":\"server\",\"event\":\"13-byte login RESPONSE datagram flood-dispatched\"}"
        << ",{\"seq\":4,\"case\":\"direct\",\"actor\":\"client\",\"event\":\"13-byte GET_STATUS REQ direct\"}"
        << ",{\"seq\":5,\"case\":\"direct\",\"actor\":\"server\",\"event\":\"tag-correlated 60-byte GET_STATUS RESPONSE direct\"}"
        << ",{\"seq\":6,\"case\":\"flood_return\",\"actor\":\"client\",\"event\":\"ANON_REQ admin login flood\"}"
        << ",{\"seq\":7,\"case\":\"flood_return\",\"actor\":\"server\",\"event\":\"13-byte login RESPONSE in PATH return\"}"
        << ",{\"seq\":8,\"case\":\"flood_return\",\"actor\":\"client\",\"event\":\"13-byte GET_STATUS REQ flood\"}"
        << ",{\"seq\":9,\"case\":\"flood_return\",\"actor\":\"server\",\"event\":\"tag-correlated 60-byte GET_STATUS RESPONSE in PATH return\"}"
        << ",{\"seq\":10,\"case\":\"lifecycle\",\"actor\":\"client\",\"event\":\"login and request timeouts clear pending identity tag secret and deadlines\"}"
        << ",{\"seq\":11,\"case\":\"lifecycle\",\"actor\":\"client_server\",\"event\":\"explicit logout zeroizes session\"}]"
        << ",\"positive_checks\":" << positive_checks
        << ",\"negative_checks\":" << negative_checks
        << ",\"negative_matrix\":[";
    for (std::size_t index = 0U; index < negative_matrix.size(); ++index) {
        if (index != 0U) {
            stream << ',';
        }
        stream << "{\"id\":\"" << negative_matrix[index].first
               << "\",\"rejected\":"
               << (negative_matrix[index].second ? "true" : "false") << '}';
    }
    stream << "]"
           << ",\"zeroization\":{\"timeout_pending_state\":"
           << (request_timeout_zeroized ? "true" : "false")
           << ",\"timeout_pending_login_state\":"
           << (login_timeout_zeroized ? "true" : "false")
           << ",\"explicit_logout_client_server_session\":"
           << (logout_zeroized ? "true" : "false") << "}}";
    return stream.str();
}

}  // namespace

AdminSessionFixtureResult run_admin_session_fixture()
{
    AdminSessionFixtureResult result{};
    std::vector<std::pair<std::string, bool>> negative_matrix;
    auto negative = [&result, &negative_matrix](const std::string &name,
                                                bool rejected) {
        negative_matrix.emplace_back(name, rejected);
        add_check(rejected, name, &result.negative_checks, &result.failures);
    };

    ClientState direct_client{};
    ServerState direct_server{};
    add_check(establish_session(false, &direct_client, &direct_server),
              "direct authenticated admin session", &result.positive_checks,
              &result.failures);
    ClientState flood_client{};
    ServerState flood_server{};
    add_check(establish_session(true, &flood_client, &flood_server),
              "flood-return authenticated admin session",
              &result.positive_checks, &result.failures);

    Packet packet{};
    LoginDelivery login_delivery{};
    ServerState negative_server{};
    const bool wrong_password_rejected =
        create_login_request(kClientPublicKey, kSharedSecret, kWrongPassword,
                             false, &packet) &&
        !server_login(&negative_server, kClientPublicKey, packet, false,
                      &login_delivery) &&
        !negative_server.has_client;
    negative("wrong_password", wrong_password_rejected);

    negative_server = {};
    login_delivery = {};
    const bool colliding_key_rejected =
        std::equal(kClientPublicKey.begin(), kClientPublicKey.begin() + 4,
                   kPrefixCollisionClientKey.begin()) &&
        !std::equal(kClientPublicKey.begin(), kClientPublicKey.end(),
                    kPrefixCollisionClientKey.begin()) &&
        create_login_request(kPrefixCollisionClientKey, kSharedSecret,
                             kAdminPassword, false, &packet) &&
        !server_login(&negative_server, kPrefixCollisionClientKey, packet,
                      false, &login_delivery) &&
        !negative_server.has_client;
    negative("wrong_full_key_colliding_prefix", colliding_key_rejected);

    ClientState base_client{};
    ServerState base_server{};
    const bool base_established =
        establish_session(false, &base_client, &base_server);
    add_check(base_established, "negative matrix baseline session",
              &result.positive_checks, &result.failures);

    Secret wrong_secret = kSharedSecret;
    wrong_secret[31] ^= 0x01U;
    ResponseDelivery ignored_delivery{};
    const bool wrong_mac_rejected =
        create_request(wrong_secret, kSecondRequestTag, false, &packet) &&
        !server_request(&base_server, kClientPublicKey, packet, false,
                        &ignored_delivery);
    negative("wrong_secret_mac", wrong_mac_rejected);

    auto malformed = request_plaintext(kSecondRequestTag);
    malformed[5] = 1U;
    const bool malformed_rejected =
        d1l_meshcore_oracle_create_request_response_packet(
            PAYLOAD_TYPE_REQ, kServerHash, kClientHash,
            base_server.client.shared_secret, malformed.data(),
            malformed.size(), &packet) &&
        prepare_route(&packet, false) &&
        !server_request(&base_server, kClientPublicKey, packet, false,
                        &ignored_delivery);
    negative("malformed_payload", malformed_rejected);

    const bool canonical_request_created = create_request(
        base_client.session_secret, kSecondRequestTag, false, &packet);
    Packet truncated = packet;
    if (truncated.payload_len > 0U) {
        --truncated.payload_len;
    }
    negative("truncated_payload",
             canonical_request_created &&
                 !server_request(&base_server, kClientPublicKey, truncated,
                                 false, &ignored_delivery));
    Packet extra = packet;
    if (extra.payload_len < D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES) {
        extra.payload[extra.payload_len++] = 0U;
    }
    negative("extra_payload",
             canonical_request_created &&
                 !server_request(&base_server, kClientPublicKey, extra, false,
                                 &ignored_delivery));

    ClientState correlation_client = base_client;
    correlation_client.completed_tags.clear();
    correlation_client.clear_request_pending();
    const ResponseDelivery response_a =
        make_direct_response(correlation_client.session_secret,
                             kSecondRequestTag);
    negative("unsolicited_response",
             response_a.ready &&
                 !client_receive_response(&correlation_client,
                                          kServerPublicKey, response_a, 1100U));

    correlation_client.begin_request(kSecondRequestTag, kRequestDeadline);
    const ResponseDelivery wrong_tag_response =
        make_direct_response(correlation_client.session_secret,
                             kSecondRequestTag + 1U);
    negative("wrong_tag_response",
             wrong_tag_response.ready &&
                 !client_receive_response(&correlation_client,
                                          kServerPublicKey,
                                          wrong_tag_response, 1100U) &&
                 correlation_client.request_pending);

    const bool first_response_accepted = client_receive_response(
        &correlation_client, kServerPublicKey, response_a, 1100U);
    negative("duplicate_response",
             first_response_accepted &&
                 !client_receive_response(&correlation_client,
                                          kServerPublicKey, response_a, 1100U));

    correlation_client.begin_request(kSecondRequestTag + 1U,
                                     kRequestDeadline);
    const ResponseDelivery late_response = make_direct_response(
        correlation_client.session_secret, kSecondRequestTag + 1U);
    const bool late_rejected =
        late_response.ready &&
        !client_receive_response(&correlation_client, kServerPublicKey,
                                 late_response, kRequestDeadline + 1U) &&
        !correlation_client.request_pending &&
        is_zero(correlation_client.pending_secret) &&
        is_zero(correlation_client.pending_server_key) &&
        correlation_client.pending_tag == 0U;
    negative("late_response", late_rejected);

    correlation_client.begin_request(kSecondRequestTag + 2U,
                                     kRequestDeadline);
    negative("replayed_response",
             !client_receive_response(&correlation_client, kServerPublicKey,
                                      response_a, 1100U) &&
                 correlation_client.request_pending &&
                 correlation_client.pending_tag == kSecondRequestTag + 2U);

    ServerState replay_server{};
    ClientState replay_client{};
    ResponseDelivery replay_delivery{};
    const bool replay_baseline =
        establish_session(false, &replay_client, &replay_server) &&
        create_request(replay_client.session_secret, kRequestTag, false,
                       &packet);
    negative("replayed_request",
             replay_baseline &&
                 !server_request(&replay_server, kClientPublicKey, packet,
                                 false, &replay_delivery));

    ServerState login_replay_server = base_server;
    const AclRecord login_replay_before = login_replay_server.client;
    LoginDelivery replayed_login_delivery{};
    const bool stale_login_rejected =
        create_login_request(kClientPublicKey, kSharedSecret, kAdminPassword,
                             false, &packet) &&
        !server_login(&login_replay_server, kClientPublicKey, packet, false,
                      &replayed_login_delivery) &&
        login_replay_server.has_client &&
        std::memcmp(&login_replay_server.client, &login_replay_before,
                    sizeof(login_replay_before)) == 0;
    negative("stale_replayed_login_timestamp", stale_login_rejected);

    ServerState permission_server = base_server;
    permission_server.client.permissions = 0U;
    negative("permission_failure",
             create_request(base_client.session_secret, kSecondRequestTag,
                            false, &packet) &&
                 !server_request(&permission_server, kClientPublicKey, packet,
                                 false, &ignored_delivery));

    negative("response_from_another_full_identity",
             std::equal(kServerPublicKey.begin(), kServerPublicKey.begin() + 4,
                        kOtherServerPublicKey.begin()) &&
                 !client_receive_response(&correlation_client,
                                          kOtherServerPublicKey,
                                          late_response, 1100U) &&
                 correlation_client.request_pending);

    ServerState request_identity_server = base_server;
    negative("request_from_another_full_identity",
             create_request(base_client.session_secret, kSecondRequestTag,
                            false, &packet) &&
                 !server_request(&request_identity_server,
                                 kPrefixCollisionClientKey, packet, false,
                                 &ignored_delivery));

    ClientState timeout_client = base_client;
    timeout_client.completed_tags.clear();
    timeout_client.begin_request(kSecondRequestTag, kRequestDeadline);
    timeout_client.expire_request(kRequestDeadline + 1U);
    const bool timeout_zeroized = !timeout_client.request_pending &&
        timeout_client.pending_tag == 0U &&
        is_zero(timeout_client.pending_server_key) &&
        is_zero(timeout_client.pending_secret);
    add_check(timeout_zeroized, "timeout pending-state zeroization",
              &result.positive_checks, &result.failures);

    ClientState login_timeout_client{};
    login_timeout_client.begin_login(500U);
    login_timeout_client.expire_login(501U);
    const bool login_timeout_zeroized =
        !login_timeout_client.login_pending &&
        login_timeout_client.login_deadline == 0U &&
        is_zero(login_timeout_client.expected_login_server) &&
        is_zero(login_timeout_client.login_secret);
    add_check(login_timeout_zeroized,
              "timeout pending-login identity and secret zeroization",
              &result.positive_checks, &result.failures);

    ClientState logout_client = base_client;
    ServerState logout_server = base_server;
    logout_client.logout();
    logout_server.logout();
    const Secret zero_secret{};
    const Key zero_key{};
    const bool logout_zeroized = !logout_client.logged_in &&
        !logout_client.admin && logout_client.permissions == 0U &&
        logout_client.server_key == zero_key &&
        logout_client.session_secret == zero_secret &&
        !logout_server.has_client &&
        std::all_of(reinterpret_cast<const uint8_t *>(&logout_server.client),
                    reinterpret_cast<const uint8_t *>(&logout_server.client) +
                        sizeof(logout_server.client),
                    [](uint8_t byte) { return byte == 0U; });
    add_check(logout_zeroized, "explicit logout session zeroization",
              &result.positive_checks, &result.failures);

    result.passed = result.failures.empty() &&
        result.negative_checks == negative_matrix.size() &&
        std::all_of(negative_matrix.begin(), negative_matrix.end(),
                    [](const auto &entry) { return entry.second; });
    result.receipt_json = make_receipt_json(
        result.passed, result.positive_checks, result.negative_checks,
        negative_matrix, timeout_zeroized, login_timeout_zeroized,
        logout_zeroized);
    return result;
}

}  // namespace d1l::meshcore::host_fixture
