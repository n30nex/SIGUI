#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>

#include "mesh/meshcore_packet_hash.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "signed-advert runtime failure: %s\n", message);
    std::exit(1);
}

void require(bool condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

std::string hex(const uint8_t *bytes, std::size_t length)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(length * 2U, '0');
    for (std::size_t index = 0U; index < length; ++index) {
        result[index * 2U] = digits[bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
    }
    return result;
}

class DeterministicRng final : public mesh::RNG {
  public:
    explicit DeterministicRng(uint32_t state) : state_(state) {}

    void random(uint8_t *destination, std::size_t size) override
    {
        require(destination != nullptr || size == 0U, "RNG destination is null");
        for (std::size_t index = 0U; index < size; ++index) {
            state_ ^= state_ << 13U;
            state_ ^= state_ >> 17U;
            state_ ^= state_ << 5U;
            destination[index] = static_cast<uint8_t>(state_ >> 24U);
        }
    }

  private:
    uint32_t state_;
};

class FixedRtc final : public mesh::RTCClock {
  public:
    explicit FixedRtc(uint32_t current) : current_(current) {}

    uint32_t getCurrentTime() override { return current_; }
    void setCurrentTime(uint32_t current) override { current_ = current; }

  private:
    uint32_t current_;
};

class ManualMilliseconds final : public mesh::MillisecondClock {
  public:
    explicit ManualMilliseconds(unsigned long current) : current_(current) {}

    unsigned long getMillis() override { return current_; }
    void advance(unsigned long delta) { current_ += delta; }

  private:
    unsigned long current_;
};

class DeterministicRadio final : public mesh::Radio {
  public:
    int recvRaw(uint8_t *destination, int size) override
    {
        if (inbound_.empty()) {
            return 0;
        }
        const std::vector<uint8_t> raw = inbound_.front();
        inbound_.pop_front();
        require(size >= 0 && static_cast<std::size_t>(size) >= raw.size(),
                "radio receive buffer is too small");
        std::memcpy(destination, raw.data(), raw.size());
        return static_cast<int>(raw.size());
    }

    uint32_t getEstAirtimeFor(int length) override
    {
        require(length >= 0, "negative airtime length");
        return static_cast<uint32_t>(length + 8);
    }

    float packetScore(float, int) override { return 1.0F; }

    bool startSendRaw(const uint8_t *bytes, int length) override
    {
        require(!sending_, "radio send overlapped");
        require(bytes != nullptr && length > 0, "radio send was empty");
        captured_.emplace_back(bytes, bytes + length);
        sending_ = true;
        return true;
    }

    bool isSendComplete() override { return sending_; }
    void onSendFinished() override { sending_ = false; }
    bool isInRecvMode() const override { return !sending_; }
    float getLastRSSI() const override { return -70.0F; }
    float getLastSNR() const override { return 8.0F; }

    void inject(const std::vector<uint8_t> &raw) { inbound_.push_back(raw); }

    const std::vector<std::vector<uint8_t>> &captured() const
    {
        return captured_;
    }

  private:
    std::deque<std::vector<uint8_t>> inbound_;
    std::vector<std::vector<uint8_t>> captured_;
    bool sending_ = false;
};

class FixedPacketManager final : public mesh::PacketManager {
  public:
    mesh::Packet *allocNew() override
    {
        for (std::size_t index = 0U; index < packets_.size(); ++index) {
            if (!allocated_[index]) {
                allocated_[index] = true;
                packets_[index] = mesh::Packet();
                ++allocations_;
                return &packets_[index];
            }
        }
        return nullptr;
    }

    void free(mesh::Packet *packet) override
    {
        const std::size_t index = packet_index(packet);
        require(allocated_[index], "packet was released twice");
        require(!queued(packet), "queued packet was released prematurely");
        allocated_[index] = false;
        ++releases_;
    }

    void queueOutbound(mesh::Packet *packet, uint8_t priority,
                       uint32_t scheduled_for) override
    {
        require(is_allocated(packet), "unowned outbound packet");
        outbound_.push_back({packet, priority, scheduled_for});
    }

    mesh::Packet *getNextOutbound(uint32_t now) override
    {
        auto selected = outbound_.end();
        for (auto item = outbound_.begin(); item != outbound_.end(); ++item) {
            if (item->scheduled_for <= now &&
                (selected == outbound_.end() || item->priority < selected->priority)) {
                selected = item;
            }
        }
        if (selected == outbound_.end()) {
            return nullptr;
        }
        mesh::Packet *packet = selected->packet;
        outbound_.erase(selected);
        return packet;
    }

    int getOutboundCount(uint32_t now) const override
    {
        return static_cast<int>(std::count_if(
            outbound_.begin(), outbound_.end(),
            [now](const QueueEntry &entry) { return entry.scheduled_for <= now; }));
    }

    int getOutboundTotal() const override
    {
        return static_cast<int>(outbound_.size());
    }

    int getFreeCount() const override
    {
        return static_cast<int>(std::count(allocated_.begin(), allocated_.end(), false));
    }

    mesh::Packet *getOutboundByIdx(int index) override
    {
        if (index < 0 || static_cast<std::size_t>(index) >= outbound_.size()) {
            return nullptr;
        }
        return outbound_[static_cast<std::size_t>(index)].packet;
    }

    mesh::Packet *removeOutboundByIdx(int index) override
    {
        if (index < 0 || static_cast<std::size_t>(index) >= outbound_.size()) {
            return nullptr;
        }
        auto item = outbound_.begin() + index;
        mesh::Packet *packet = item->packet;
        outbound_.erase(item);
        return packet;
    }

    void queueInbound(mesh::Packet *packet, uint32_t scheduled_for) override
    {
        require(is_allocated(packet), "unowned inbound packet");
        inbound_.push_back({packet, 0U, scheduled_for});
    }

    mesh::Packet *getNextInbound(uint32_t now) override
    {
        auto item = std::find_if(
            inbound_.begin(), inbound_.end(),
            [now](const QueueEntry &entry) { return entry.scheduled_for <= now; });
        if (item == inbound_.end()) {
            return nullptr;
        }
        mesh::Packet *packet = item->packet;
        inbound_.erase(item);
        return packet;
    }

    int live_count() const
    {
        return static_cast<int>(std::count(allocated_.begin(), allocated_.end(), true));
    }

    int allocations() const { return allocations_; }
    int releases() const { return releases_; }

  private:
    struct QueueEntry {
        mesh::Packet *packet;
        uint8_t priority;
        uint32_t scheduled_for;
    };

    std::size_t packet_index(const mesh::Packet *packet) const
    {
        require(packet >= packets_.data() && packet < packets_.data() + packets_.size(),
                "packet does not belong to manager");
        return static_cast<std::size_t>(packet - packets_.data());
    }

    bool is_allocated(const mesh::Packet *packet) const
    {
        return allocated_[packet_index(packet)];
    }

    bool queued(const mesh::Packet *packet) const
    {
        const auto contains = [packet](const QueueEntry &entry) {
            return entry.packet == packet;
        };
        return std::any_of(outbound_.begin(), outbound_.end(), contains) ||
               std::any_of(inbound_.begin(), inbound_.end(), contains);
    }

    std::array<mesh::Packet, 16U> packets_{};
    std::array<bool, 16U> allocated_{};
    std::vector<QueueEntry> outbound_;
    std::vector<QueueEntry> inbound_;
    int allocations_ = 0;
    int releases_ = 0;
};

class SeenTable final : public SimpleMeshTables {
  public:
    bool hasSeen(const mesh::Packet *packet) override
    {
        ++lookups_;
        return SimpleMeshTables::hasSeen(packet);
    }

    void clear(const mesh::Packet *packet) override
    {
        SimpleMeshTables::clear(packet);
    }

    int lookups() const { return lookups_; }
    uint32_t flood_duplicates() const { return getNumFloodDups(); }

  private:
    int lookups_ = 0;
};

static_assert(MAX_HASH_SIZE == D1L_MESHCORE_PACKET_HASH_BYTES,
              "pinned upstream and D1L packet hashes must have equal width");

std::array<uint8_t, D1L_MESHCORE_PACKET_HASH_BYTES>
upstream_packet_hash(const mesh::Packet &packet)
{
    std::array<uint8_t, D1L_MESHCORE_PACKET_HASH_BYTES> hash{};
    packet.calculatePacketHash(hash.data());
    return hash;
}

std::array<uint8_t, D1L_MESHCORE_PACKET_HASH_BYTES>
d1l_packet_hash(const mesh::Packet &packet)
{
    d1l_meshcore_wire_packet_t view{};
    view.header = packet.header;
    view.route = packet.getRouteType();
    view.type = packet.getPayloadType();
    view.version = packet.getPayloadVer();
    view.transport_codes[0] = packet.transport_codes[0];
    view.transport_codes[1] = packet.transport_codes[1];
    view.path_len = static_cast<uint8_t>(packet.path_len);
    view.path_hash_bytes = packet.getPathHashSize();
    view.path_hops = packet.getPathHashCount();
    view.path = packet.getPathByteLen() > 0U ? packet.path : nullptr;
    view.path_byte_len = packet.getPathByteLen();
    view.payload = packet.payload;
    view.payload_len = packet.payload_len;
    std::array<uint8_t, D1L_MESHCORE_PACKET_HASH_BYTES> hash{};
    require(d1l_meshcore_packet_hash_calculate(&view, hash.data()) == ESP_OK,
            "D1L packet hash rejected a pinned upstream packet");
    return hash;
}

void require_hash_parity(const mesh::Packet &packet, const char *message)
{
    require(upstream_packet_hash(packet) == d1l_packet_hash(packet), message);
}

int verify_packet_hash_semantics(const std::vector<uint8_t> &advert_wire)
{
    require(!advert_wire.empty() && advert_wire.size() <= UINT8_MAX,
            "signed advert wire cannot be decoded for hash parity");
    mesh::Packet advert;
    require(advert.readFrom(advert_wire.data(),
                            static_cast<uint8_t>(advert_wire.size())),
            "signed advert wire did not decode for hash parity");
    require_hash_parity(advert, "upstream/D1L signed-advert hash changed");
    const auto advert_hash = upstream_packet_hash(advert);

    mesh::Packet route_variant = advert;
    route_variant.header = static_cast<uint8_t>(
        (route_variant.header & ~PH_ROUTE_MASK) | ROUTE_TYPE_TRANSPORT_DIRECT);
    route_variant.transport_codes[0] = 0x1234U;
    route_variant.transport_codes[1] = 0xabcdU;
    route_variant.setPathHashSizeAndCount(1U, 2U);
    route_variant.path[0] = 0x51U;
    route_variant.path[1] = 0x62U;
    require_hash_parity(route_variant,
                        "upstream/D1L routed advert hash changed");
    require(upstream_packet_hash(route_variant) == advert_hash,
            "non-TRACE route/path/transport changed packet hash");

    mesh::Packet changed_type = advert;
    changed_type.header = static_cast<uint8_t>(
        (changed_type.header & ~(PH_TYPE_MASK << PH_TYPE_SHIFT)) |
        (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT));
    mesh::Packet changed_payload = advert;
    require(changed_payload.payload_len > 0U,
            "advert payload is empty for hash mutation");
    changed_payload.payload[changed_payload.payload_len - 1U] ^= 0x01U;
    require_hash_parity(changed_type,
                        "upstream/D1L type-domain hash changed");
    require_hash_parity(changed_payload,
                        "upstream/D1L payload-domain hash changed");
    require(upstream_packet_hash(changed_type) != advert_hash &&
                upstream_packet_hash(changed_payload) != advert_hash,
            "packet type or payload mutation did not change hash");

    mesh::Packet trace;
    trace.header = static_cast<uint8_t>(
        (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD);
    trace.setPathHashSizeAndCount(2U, 2U);
    trace.path[0] = 0x10U;
    trace.path[1] = 0x20U;
    trace.path[2] = 0x30U;
    trace.path[3] = 0x40U;
    trace.payload_len = 4U;
    trace.payload[0] = 0x90U;
    trace.payload[1] = 0x91U;
    trace.payload[2] = 0x92U;
    trace.payload[3] = 0x93U;
    require_hash_parity(trace,
                        "TRACE uint16 little-endian path length hash changed");
    const auto trace_hash = upstream_packet_hash(trace);

    mesh::Packet trace_path_variant = trace;
    trace_path_variant.path[0] ^= 0xffU;
    trace_path_variant.path[3] ^= 0xffU;
    require_hash_parity(trace_path_variant,
                        "TRACE path-byte exclusion parity changed");
    require(upstream_packet_hash(trace_path_variant) == trace_hash,
            "TRACE path bytes unexpectedly changed packet hash");

    mesh::Packet trace_length_variant = trace;
    trace_length_variant.setPathHashSizeAndCount(2U, 1U);
    require_hash_parity(trace_length_variant,
                        "TRACE changed-length parity changed");
    require(upstream_packet_hash(trace_length_variant) != trace_hash,
            "TRACE encoded path length did not change packet hash");
    return 5;
}

std::vector<uint8_t> routed_advert_variant(const std::vector<uint8_t> &wire,
                                           bool transport)
{
    require(!wire.empty() && wire.size() <= UINT8_MAX,
            "signed advert route variant input is invalid");
    mesh::Packet packet;
    require(packet.readFrom(wire.data(), static_cast<uint8_t>(wire.size())),
            "signed advert route variant did not decode");
    packet.header = static_cast<uint8_t>(
        (packet.header & ~PH_ROUTE_MASK) |
        (transport ? ROUTE_TYPE_TRANSPORT_FLOOD : ROUTE_TYPE_FLOOD));
    packet.transport_codes[0] = 0x1357U;
    packet.transport_codes[1] = 0x2468U;
    packet.setPathHashSizeAndCount(1U, transport ? 3U : 2U);
    packet.path[0] = transport ? 0xa1U : 0xb1U;
    packet.path[1] = transport ? 0xa2U : 0xb2U;
    packet.path[2] = 0xa3U;
    std::array<uint8_t, 255U> encoded{};
    const uint8_t length = packet.writeTo(encoded.data());
    require(length > 0U, "signed advert route variant encoded empty");
    return std::vector<uint8_t>(encoded.begin(), encoded.begin() + length);
}

struct ContactSnapshot {
    std::array<uint8_t, PUB_KEY_SIZE> public_key{};
    std::string name;
    uint8_t type = 0U;
    uint32_t advert_timestamp = 0U;
    int32_t latitude = 0;
    int32_t longitude = 0;
    bool upstream_is_new = false;

    bool operator==(const ContactSnapshot &other) const
    {
        return public_key == other.public_key && name == other.name &&
               type == other.type && advert_timestamp == other.advert_timestamp &&
               latitude == other.latitude && longitude == other.longitude &&
               upstream_is_new == other.upstream_is_new;
    }
};

class RuntimeChatMesh final : public BaseChatMesh {
  public:
    RuntimeChatMesh(mesh::Radio &radio, mesh::MillisecondClock &milliseconds,
                    mesh::RNG &rng, mesh::RTCClock &rtc,
                    mesh::PacketManager &manager, mesh::MeshTables &tables)
        : BaseChatMesh(radio, milliseconds, rng, rtc, manager, tables)
    {
    }

    void setIdentity(mesh::RNG &rng) { self_id = mesh::LocalIdentity(&rng); }

    int callback_count() const { return callback_count_; }
    int filter_count() const { return filter_count_; }
    int blob_write_count() const { return blob_write_count_; }
    const ContactSnapshot &last_contact() const { return last_contact_; }
    const std::vector<uint8_t> &stored_advert() const { return stored_advert_; }

  protected:
    bool filterRecvFloodPacket(mesh::Packet *) override
    {
        ++filter_count_;
        return false;
    }

    void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t,
                             const uint8_t *) override
    {
        ++callback_count_;
        std::copy(std::begin(contact.id.pub_key), std::end(contact.id.pub_key),
                  last_contact_.public_key.begin());
        last_contact_.name = contact.name;
        last_contact_.type = contact.type;
        last_contact_.advert_timestamp = contact.last_advert_timestamp;
        last_contact_.latitude = contact.gps_lat;
        last_contact_.longitude = contact.gps_lon;
        last_contact_.upstream_is_new = is_new;
    }

    ContactInfo *processAck(const uint8_t *) override { return nullptr; }
    void onContactPathUpdated(const ContactInfo &) override {}
    void onMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t,
                       const char *) override {}
    void onCommandDataRecv(const ContactInfo &, mesh::Packet *, uint32_t,
                           const char *) override {}
    void onSignedMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t,
                            const uint8_t *, const char *) override {}
    uint32_t calcFloodTimeoutMillisFor(uint32_t airtime) const override
    {
        return airtime + 1000U;
    }
    uint32_t calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t path_len) const override
    {
        return airtime + path_len + 1000U;
    }
    void onSendTimeout() override {}
    void onChannelMessageRecv(const mesh::GroupChannel &, mesh::Packet *,
                              uint32_t, const char *) override {}
    uint8_t onContactRequest(const ContactInfo &, uint32_t, const uint8_t *,
                             uint8_t, uint8_t *) override
    {
        return 0U;
    }
    void onContactResponse(const ContactInfo &, const uint8_t *, uint8_t) override {}

    bool putBlobByKey(const uint8_t *, int, const uint8_t *source, int length) override
    {
        require(source != nullptr && length > 0, "stored advert blob was empty");
        stored_advert_.assign(source, source + length);
        ++blob_write_count_;
        return true;
    }

  private:
    int callback_count_ = 0;
    int filter_count_ = 0;
    int blob_write_count_ = 0;
    ContactSnapshot last_contact_{};
    std::vector<uint8_t> stored_advert_;
};

struct ScenarioResult {
    std::vector<uint8_t> wire;
    ContactSnapshot contact;
    std::vector<uint8_t> stored_advert;
    std::array<uint8_t, PUB_KEY_SIZE> sender_key{};
    int receiver_filter_count = 0;
    int receiver_table_lookups = 0;
    int receiver_table_flood_duplicates = 0;
    int packet_hash_parity_cases = 0;
    int signed_adverts_sent = 0;
    int contact_callbacks = 0;
    int stored_advert_writes = 0;
    int timestamp_replay_cases = 0;
    int timestamp_replay_rejections = 0;
    int timestamp_newer_acceptances = 0;
    int allocations = 0;
    int releases = 0;

    bool operator==(const ScenarioResult &other) const
    {
        return wire == other.wire && contact == other.contact &&
               stored_advert == other.stored_advert && sender_key == other.sender_key &&
               receiver_filter_count == other.receiver_filter_count &&
               receiver_table_lookups == other.receiver_table_lookups &&
               receiver_table_flood_duplicates ==
                   other.receiver_table_flood_duplicates &&
               packet_hash_parity_cases == other.packet_hash_parity_cases &&
               signed_adverts_sent == other.signed_adverts_sent &&
               contact_callbacks == other.contact_callbacks &&
               stored_advert_writes == other.stored_advert_writes &&
               timestamp_replay_cases == other.timestamp_replay_cases &&
               timestamp_replay_rejections == other.timestamp_replay_rejections &&
               timestamp_newer_acceptances == other.timestamp_newer_acceptances &&
               allocations == other.allocations && releases == other.releases;
    }
};

void pump_transmit(RuntimeChatMesh &sender, ManualMilliseconds &clock)
{
    clock.advance(1U);
    sender.loop();
    clock.advance(1U);
    sender.loop();
}

void pump_receive(RuntimeChatMesh &receiver)
{
    receiver.loop();
}

std::vector<uint8_t> transmit_signed_advert(
    RuntimeChatMesh &sender, FixedRtc &rtc, ManualMilliseconds &clock,
    DeterministicRadio &radio, FixedPacketManager &manager, uint32_t timestamp,
    const char *name)
{
    rtc.setCurrentTime(timestamp);
    const std::size_t captured_before = radio.captured().size();
    mesh::Packet *advert = sender.createSelfAdvert(name, 43.6532, -79.3832);
    require(advert != nullptr, "createSelfAdvert returned null");
    sender.sendFlood(advert);
    require(manager.live_count() == 1, "outbound queue did not own advert");
    pump_transmit(sender, clock);
    require(radio.captured().size() == captured_before + 1U,
            "signed advert was not sent exactly once");
    require(manager.live_count() == 0,
            "sender retained signed advert after send completion");
    return radio.captured().back();
}

ScenarioResult run_scenario()
{
    DeterministicRng sender_rng(0xD1C0A551U);
    DeterministicRng receiver_rng(0xD1C0B662U);
    FixedRtc sender_rtc(1'750'000'123U);
    FixedRtc receiver_rtc(1'750'000'500U);
    ManualMilliseconds sender_ms(10'000U);
    ManualMilliseconds receiver_ms(20'000U);
    DeterministicRadio sender_radio;
    DeterministicRadio receiver_radio;
    FixedPacketManager sender_manager;
    FixedPacketManager receiver_manager;
    SeenTable sender_seen;
    SeenTable receiver_seen;
    RuntimeChatMesh sender(sender_radio, sender_ms, sender_rng, sender_rtc,
                           sender_manager, sender_seen);
    RuntimeChatMesh receiver(receiver_radio, receiver_ms, receiver_rng, receiver_rtc,
                             receiver_manager, receiver_seen);
    sender.setIdentity(sender_rng);
    receiver.setIdentity(receiver_rng);
    sender.begin();
    receiver.begin();

    constexpr uint32_t baseline_timestamp = 1'750'000'123U;
    const std::vector<uint8_t> wire = transmit_signed_advert(
        sender, sender_rtc, sender_ms, sender_radio, sender_manager,
        baseline_timestamp, "semantic-node");
    const int packet_hash_parity_cases = verify_packet_hash_semantics(wire);
    receiver_radio.inject(wire);
    pump_receive(receiver);
    require(receiver.callback_count() == 1, "valid advert did not invoke contact callback");
    require(receiver.getNumContacts() == 1, "valid advert did not promote one contact");
    require(receiver.blob_write_count() == 1, "valid advert was not stored exactly once");
    require(receiver_manager.live_count() == 0, "receiver retained valid advert packet");

    ContactInfo contact{};
    require(receiver.getContactByIdx(0U, contact), "promoted contact is unavailable");
    require(contact.id.matches(sender.self_id), "promoted contact key changed");
    require(std::strcmp(contact.name, "semantic-node") == 0, "promoted contact name changed");
    require(contact.type == ADV_TYPE_CHAT, "promoted contact type changed");
    require(contact.last_advert_timestamp == sender_rtc.getCurrentTime(),
            "promoted advert timestamp changed");
    require(contact.gps_lat == 43'653'200 && contact.gps_lon == -79'383'200,
            "promoted advert location changed");

    receiver_radio.inject(wire);
    pump_receive(receiver);
    require(receiver.callback_count() == 1, "duplicate advert reached callback");
    require(receiver.blob_write_count() == 1, "duplicate advert rewrote storage");
    require(receiver_manager.live_count() == 0, "receiver retained duplicate packet");

    const std::vector<uint8_t> path_variant =
        routed_advert_variant(wire, false);
    const std::vector<uint8_t> transport_variant =
        routed_advert_variant(wire, true);
    require(path_variant != wire && transport_variant != wire &&
                path_variant != transport_variant,
            "advert route variants were not distinct wires");
    receiver_radio.inject(path_variant);
    pump_receive(receiver);
    receiver_radio.inject(transport_variant);
    pump_receive(receiver);
    require(receiver.callback_count() == 1,
            "routed duplicate advert reached callback");
    require(receiver.blob_write_count() == 1,
            "routed duplicate advert rewrote storage");
    require(receiver_seen.flood_duplicates() == 3U,
            "real SimpleMeshTables did not count all advert duplicates");

    std::vector<std::vector<uint8_t>> signed_wires = {wire};
    const auto transmit_distinct = [&](uint32_t timestamp, const char *name) {
        std::vector<uint8_t> next = transmit_signed_advert(
            sender, sender_rtc, sender_ms, sender_radio, sender_manager,
            timestamp, name);
        require(std::find(signed_wires.begin(), signed_wires.end(), next) ==
                    signed_wires.end(),
                "timestamp scenario reused an earlier wire hash input");
        signed_wires.push_back(next);
        return next;
    };

    const std::vector<uint8_t> equal_timestamp_wire =
        transmit_distinct(baseline_timestamp, "equal-replay");
    receiver_radio.inject(equal_timestamp_wire);
    pump_receive(receiver);
    require(receiver.callback_count() == 1,
            "distinct equal-timestamp advert reached callback");
    require(receiver.blob_write_count() == 1,
            "distinct equal-timestamp advert rewrote storage");

    const std::vector<uint8_t> older_timestamp_wire =
        transmit_distinct(baseline_timestamp - 1U, "older-replay");
    receiver_radio.inject(older_timestamp_wire);
    pump_receive(receiver);
    require(receiver.callback_count() == 1,
            "distinct older-timestamp advert reached callback");
    require(receiver.blob_write_count() == 1,
            "distinct older-timestamp advert rewrote storage");

    const std::vector<uint8_t> newer_timestamp_wire =
        transmit_distinct(baseline_timestamp + 1U, "newer-node");
    receiver_radio.inject(newer_timestamp_wire);
    pump_receive(receiver);
    require(receiver.callback_count() == 2,
            "strictly newer timestamp did not reach callback");
    require(receiver.blob_write_count() == 2,
            "strictly newer timestamp did not update storage");
    require(receiver.last_contact().advert_timestamp == baseline_timestamp + 1U,
            "strictly newer timestamp was not retained");
    require(receiver.last_contact().name == "newer-node",
            "strictly newer contact name was not retained");

    const std::vector<uint8_t> max_timestamp_wire =
        transmit_distinct(UINT32_MAX, "max-node");
    receiver_radio.inject(max_timestamp_wire);
    pump_receive(receiver);
    require(receiver.callback_count() == 3,
            "UINT32_MAX timestamp did not reach callback");
    require(receiver.blob_write_count() == 3,
            "UINT32_MAX timestamp did not update storage");
    require(receiver.last_contact().advert_timestamp == UINT32_MAX,
            "UINT32_MAX timestamp was not retained");
    require(receiver.last_contact().name == "max-node",
            "UINT32_MAX contact name was not retained");

    const std::vector<uint8_t> wrapped_zero_wire =
        transmit_distinct(0U, "wrapped-zero");
    receiver_radio.inject(wrapped_zero_wire);
    pump_receive(receiver);
    require(receiver.callback_count() == 3,
            "wrapped zero timestamp reached callback");
    require(receiver.blob_write_count() == 3,
            "wrapped zero timestamp rewrote storage");
    require(receiver.last_contact().advert_timestamp == UINT32_MAX,
            "wrapped zero timestamp replaced UINT32_MAX");
    require(receiver.last_contact().name == "max-node",
            "wrapped zero timestamp changed contact name");
    require(receiver_manager.live_count() == 0,
            "receiver retained a timestamp scenario packet");

    std::vector<uint8_t> forged = wire;
    constexpr std::size_t signature_offset = 2U + PUB_KEY_SIZE + 4U;
    require(forged.size() > signature_offset, "wire advert is shorter than signature");
    forged[signature_offset] ^= 0x80U;
    receiver_radio.inject(forged);
    pump_receive(receiver);
    require(receiver.callback_count() == 3, "forged advert reached callback");
    require(receiver.getNumContacts() == 1, "forged advert changed contacts");
    require(receiver_manager.live_count() == 0, "receiver retained forged packet");

    sender_radio.inject(wire);
    pump_receive(sender);
    require(sender.callback_count() == 0, "self advert reached callback");
    require(sender.getNumContacts() == 0, "self advert created a contact");
    require(sender_manager.live_count() == 0, "sender retained self advert packet");

    ContactInfo persisted{};
    require(receiver.getContactByIdx(0U, persisted),
            "contact did not outlive receive packet ownership");
    require(std::strcmp(persisted.name, "max-node") == 0,
            "contact contents did not outlive receive packet");
    require(persisted.last_advert_timestamp == UINT32_MAX,
            "timestamp watermark did not outlive receive packet");
    require(!receiver.stored_advert().empty(), "stored advert did not outlive receive packet");
    require(sender_manager.allocations() == sender_manager.releases(),
            "sender packet ownership is imbalanced");
    require(receiver_manager.allocations() == receiver_manager.releases(),
            "receiver packet ownership is imbalanced");

    ScenarioResult result;
    result.wire = wire;
    result.contact = receiver.last_contact();
    result.stored_advert = receiver.stored_advert();
    std::copy(std::begin(sender.self_id.pub_key), std::end(sender.self_id.pub_key),
              result.sender_key.begin());
    result.receiver_filter_count = receiver.filter_count();
    result.receiver_table_lookups = receiver_seen.lookups();
    result.receiver_table_flood_duplicates =
        static_cast<int>(receiver_seen.flood_duplicates());
    result.packet_hash_parity_cases = packet_hash_parity_cases;
    result.signed_adverts_sent = static_cast<int>(sender_radio.captured().size());
    result.contact_callbacks = receiver.callback_count();
    result.stored_advert_writes = receiver.blob_write_count();
    result.timestamp_replay_cases = 5;
    result.timestamp_replay_rejections = 3;
    result.timestamp_newer_acceptances = 2;
    result.allocations = sender_manager.allocations() + receiver_manager.allocations();
    result.releases = sender_manager.releases() + receiver_manager.releases();
    return result;
}

} // namespace

int main()
{
    const ScenarioResult first = run_scenario();
    const ScenarioResult second = run_scenario();
    require(first == second, "deterministic replay changed runtime results");
    require(first.receiver_filter_count == 10,
            "flood pre-dispatch filter was not exercised for each receive");
    require(first.receiver_table_lookups == 10,
            "seen-table lookup count did not cover distinct replay wires");
    require(first.receiver_table_flood_duplicates == 3,
            "real SimpleMeshTables duplicate count drifted");
    require(first.packet_hash_parity_cases == 5,
            "packet-hash parity case count drifted");
    require(first.signed_adverts_sent == 6,
            "signed timestamp scenario count drifted");
    require(first.contact_callbacks == 3 && first.stored_advert_writes == 3,
            "accepted signed-advert count drifted");
    require(first.timestamp_replay_cases == 5 &&
                first.timestamp_replay_rejections == 3 &&
                first.timestamp_newer_acceptances == 2,
            "timestamp replay window counts drifted");
    require(first.allocations == first.releases, "aggregate packet ownership is imbalanced");

    std::printf(
        "{\"status\":\"pass\",\"runtime\":\"pinned_meshcore_signed_advert\","
        "\"wire_hex\":\"%s\",\"sender_public_key_hex\":\"%s\","
        "\"contact_name\":\"%s\",\"contact_type\":%u,"
        "\"advert_timestamp\":%u,\"latitude_e6\":%d,\"longitude_e6\":%d,"
        "\"upstream_callback_is_new\":%s,"
        "\"filter_calls\":%d,\"table_lookups\":%d,"
        "\"table_flood_duplicates\":%d,\"packet_hash_parity_cases\":%d,"
        "\"signed_adverts_sent\":%d,\"contact_callbacks\":%d,"
        "\"stored_advert_writes\":%d,\"timestamp_replay_cases\":%d,"
        "\"timestamp_replay_rejections\":%d,"
        "\"timestamp_newer_acceptances\":%d,"
        "\"allocations\":%d,\"releases\":%d,"
        "\"duplicate_suppressed\":true,"
        "\"identical_wire_hash_suppressed\":true,"
        "\"real_simple_mesh_tables\":true,"
        "\"upstream_d1l_packet_hash_match\":true,"
        "\"route_path_transport_variants_suppressed\":true,"
        "\"trace_path_length_little_endian_match\":true,"
        "\"trace_path_bytes_excluded\":true,"
        "\"distinct_equal_timestamp_rejected\":true,"
        "\"distinct_older_timestamp_rejected\":true,"
        "\"strictly_newer_timestamp_accepted\":true,"
        "\"uint32_max_timestamp_accepted\":true,"
        "\"wrapped_zero_timestamp_rejected\":true,"
        "\"distinct_signed_wires\":true,\"bad_signature_rejected\":true,"
        "\"self_suppressed\":true,\"ownership_balanced\":true,"
        "\"deterministic_replay\":true}\n",
        hex(first.wire.data(), first.wire.size()).c_str(),
        hex(first.sender_key.data(), first.sender_key.size()).c_str(),
        first.contact.name.c_str(), static_cast<unsigned int>(first.contact.type),
        first.contact.advert_timestamp, first.contact.latitude,
        first.contact.longitude,
        first.contact.upstream_is_new ? "true" : "false",
        first.receiver_filter_count,
        first.receiver_table_lookups, first.receiver_table_flood_duplicates,
        first.packet_hash_parity_cases, first.signed_adverts_sent,
        first.contact_callbacks, first.stored_advert_writes,
        first.timestamp_replay_cases, first.timestamp_replay_rejections,
        first.timestamp_newer_acceptances, first.allocations, first.releases);
    return 0;
}
