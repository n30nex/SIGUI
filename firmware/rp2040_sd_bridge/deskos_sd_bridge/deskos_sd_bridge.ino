#include <Arduino.h>
#include <SD.h>
#include <SdFat.h>
#include <SDFS.h>
#include <SPI.h>

namespace {

constexpr const char *STATUS_REQUEST = "DESKOS_SD_STATUS";
constexpr const char *STATUS_REPLY = "DESKOS_SD_STATUS";
constexpr const char *MOUNT_REQUEST = "DESKOS_SD_MOUNT";
constexpr const char *MOUNT_REPLY = "DESKOS_SD_MOUNT";
constexpr const char *PING_REQUEST = "DESKOS_SD_PING";
constexpr const char *PING_REPLY = "DESKOS_SD_PING";
constexpr const char *DIAG_REQUEST = "DESKOS_SD_DIAG";
constexpr const char *DIAG_REPLY = "DESKOS_SD_DIAG";
constexpr const char *FILE_REQUEST = "DESKOS_SD_FILE";
constexpr const char *FILE_REPLY = "DESKOS_SD_FILE";
constexpr uint32_t FILE_PROTOCOL_VERSION = 1;

constexpr uint8_t RP2040_ESP32_RX_PIN = 17;
constexpr uint8_t RP2040_ESP32_TX_PIN = 16;
constexpr uint32_t ESP32_BRIDGE_BAUD = 921600;

constexpr uint8_t SD_CS_PIN = 13;
constexpr uint8_t SD_SCK_PIN = 10;
constexpr uint8_t SD_MOSI_PIN = 11;
constexpr uint8_t SD_MISO_PIN = 12;
constexpr uint8_t SD_POWER_PIN = 18;
constexpr uint32_t SD_SPI_HZ = 1000000U;
constexpr uint32_t SD_PROBE_SPI_HZ = 400000U;
constexpr const char *DESKOS_ROOT = "/deskos";
constexpr const char *DESKOS_MANIFEST = "/deskos/manifest.json";
constexpr const char *DESKOS_MAP_MANIFEST = "/deskos/map/manifest.json";
constexpr const char DESKOS_MANIFEST_PAYLOAD[] =
    "{\"name\":\"MeshCore DeskOS D1L SD\",\"schema\":1,"
    "\"created_by\":\"MeshCore DeskOS D1L\","
    "\"device\":\"seeed-indicator-d1l\","
    "\"stores\":[\"messages\",\"dm\",\"nodes\",\"routes\",\"packets\",\"map_tiles\"]}\n";
constexpr const char DESKOS_MAP_MANIFEST_PAYLOAD[] =
    "{\"schema\":1,\"kind\":\"map_cache\","
    "\"tile_template\":\"map/tiles/z{z}/x{x}/y{y}.tile\","
    "\"download_supported\":false}\n";
constexpr const char *DESKOS_REQUIRED_DIRS[] = {
    "/deskos/stores",
    "/deskos/stores/messages",
    "/deskos/stores/messages/public",
    "/deskos/stores/messages/dm",
    "/deskos/stores/nodes",
    "/deskos/stores/contacts",
    "/deskos/stores/routes",
    "/deskos/stores/packet_log",
    "/deskos/map",
    "/deskos/map/tiles",
    "/deskos/map/packs",
    "/deskos/exports",
    "/deskos/exports/diagnostics",
    "/deskos/exports/data",
    "/deskos/tmp",
    "/deskos/logs",
};

constexpr size_t FILE_LINE_MAX = 512;
constexpr size_t RX_LINE_MAX = FILE_LINE_MAX + 1;
constexpr size_t FILE_PATH_MAX = 96;
constexpr size_t FILE_CHUNK_MAX = 192;
constexpr size_t FILE_PATH64_MAX = 128;
constexpr size_t FILE_DATA64_MAX = 256;
constexpr size_t FILE_FULL_PATH_MAX = sizeof("/deskos/") + FILE_PATH_MAX;
constexpr const char *REPLACE_BACKUP_SUFFIX = ".bak";
constexpr bool REPLACE_RENAME_PRESERVES_OLD_ON_FAILURE = true;

struct SdSnapshot {
    const char *state;
    bool present;
    bool mounted;
    bool deskos;
    const char *fs;
    bool needs_fat32;
    uint32_t capacity_kb;
    uint32_t free_kb;
    const char *note;
    const char *probe_power;
    const char *probe_mode;
    bool probe_present;
    uint8_t probe_error;
    uint8_t probe_data;
};

struct CardProbe {
    bool present;
    uint32_t capacity_kb;
    uint8_t error_code;
    uint8_t error_data;
    const char *power;
    const char *mode;
    bool power_high;
    uint8_t options;
};

struct DiagSnapshot {
    CardProbe high_dedicated;
    CardProbe high_shared;
    CardProbe low_dedicated;
    CardProbe low_shared;
    const char *selected_power;
    const char *selected_mode;
    bool mount_selected;
    bool valid;
};

struct LineRx {
    char line[RX_LINE_MAX];
    size_t len;
    bool drop_until_newline;
};

enum SdWorkerRequest : uint8_t {
    SD_WORKER_NONE = 0,
    SD_WORKER_MOUNT = 1,
    SD_WORKER_DIAG = 2,
};

LineRx bridge_rx = {{0}, 0, false};
LineRx usb_rx = {{0}, 0, false};
Stream *reply_stream = &Serial1;
SdFat s_sd;
bool s_sd_power_high = true;
uint8_t s_sd_spi_options = DEDICATED_SPI;
SdSnapshot s_cached_snapshot = {};
bool s_cached_snapshot_valid = false;
SdSnapshot s_worker_snapshot = {};
DiagSnapshot s_worker_diag = {};
DiagSnapshot s_cached_diag = {};
bool s_cached_diag_valid = false;
uint8_t s_last_mount_error = 0;
uint8_t s_last_mount_data = 0;
volatile uint8_t s_worker_request = SD_WORKER_NONE;
volatile bool s_worker_busy = false;
volatile bool s_mount_worker_completed = false;
volatile uint32_t s_worker_snapshot_revision = 0;
volatile uint32_t s_worker_diag_revision = 0;
uint32_t s_seen_snapshot_revision = 0;
uint32_t s_seen_diag_revision = 0;

uint32_t clamp_kb(uint64_t bytes) {
    const uint64_t kb = bytes / 1024ULL;
    return kb > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(kb);
}

const char *power_token(bool power_high) {
    return power_high ? "high" : "low";
}

const char *spi_mode_token(uint8_t options) {
    return options == SHARED_SPI ? "shared" : "dedicated";
}

SdSpiConfig sd_spi_config(uint8_t options);

void configure_sd_bus(bool power_high) {
    static bool sd_power_settled = false;
    static bool last_power_high = true;
    pinMode(SD_POWER_PIN, OUTPUT);
    digitalWrite(SD_POWER_PIN, power_high ? HIGH : LOW);
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    if (!sd_power_settled || last_power_high != power_high) {
        delay(250);
        sd_power_settled = true;
        last_power_high = power_high;
    }
    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
    SPI1.setCS(SD_CS_PIN);
}

void configure_sd_bus() {
    configure_sd_bus(s_sd_power_high);
}

bool mount_sd_with_power(bool power_high) {
    configure_sd_bus(power_high);
    SPI1.begin();
    s_sd.end();
    if (s_sd.begin(sd_spi_config(s_sd_spi_options))) {
        s_last_mount_error = 0;
        s_last_mount_data = 0;
        return true;
    }
    if (s_sd.card()) {
        s_last_mount_error = s_sd.card()->errorCode();
        s_last_mount_data = s_sd.card()->errorData();
    } else {
        s_last_mount_error = 0xFE;
        s_last_mount_data = 0;
    }
    s_sd.end();
    delay(50);
    configure_sd_bus(power_high);
    SPI1.begin();
    if (s_sd.begin(sd_spi_config(s_sd_spi_options))) {
        s_last_mount_error = 0;
        s_last_mount_data = 0;
        return true;
    }
    if (s_sd.card()) {
        s_last_mount_error = s_sd.card()->errorCode();
        s_last_mount_data = s_sd.card()->errorData();
    } else {
        s_last_mount_error = 0xFE;
        s_last_mount_data = 0;
    }
    return false;
}

bool mount_sd() {
    return mount_sd_with_power(s_sd_power_high);
}

CardProbe empty_probe(const char *power, const char *mode, bool power_high, uint8_t options) {
    CardProbe probe = {
        false,
        0,
        0xFF,
        0,
        power,
        mode,
        power_high,
        options,
    };
    return probe;
}

uint8_t sd_spi_transfer(uint8_t value) {
    return SPI1.transfer(value);
}

uint8_t sd_wait_ready(uint32_t timeout_ms) {
    const uint32_t start = millis();
    uint8_t value = 0xFF;
    do {
        value = sd_spi_transfer(0xFF);
        if (value == 0xFF) {
            return value;
        }
        delay(1);
    } while (millis() - start < timeout_ms);
    return value;
}

uint8_t sd_command(uint8_t command, uint32_t argument, uint8_t crc, uint8_t *extra, size_t extra_len) {
    digitalWrite(SD_CS_PIN, HIGH);
    (void)sd_spi_transfer(0xFF);
    digitalWrite(SD_CS_PIN, LOW);
    (void)sd_wait_ready(50);
    (void)sd_spi_transfer(0x40U | command);
    (void)sd_spi_transfer(static_cast<uint8_t>(argument >> 24));
    (void)sd_spi_transfer(static_cast<uint8_t>(argument >> 16));
    (void)sd_spi_transfer(static_cast<uint8_t>(argument >> 8));
    (void)sd_spi_transfer(static_cast<uint8_t>(argument));
    (void)sd_spi_transfer(crc);
    uint8_t response = 0xFF;
    for (uint8_t i = 0; i < 16; ++i) {
        response = sd_spi_transfer(0xFF);
        if ((response & 0x80U) == 0) {
            break;
        }
    }
    for (size_t i = 0; i < extra_len; ++i) {
        extra[i] = sd_spi_transfer(0xFF);
    }
    digitalWrite(SD_CS_PIN, HIGH);
    (void)sd_spi_transfer(0xFF);
    return response;
}

CardProbe manual_probe_card(uint8_t options, bool power_high) {
    CardProbe probe = empty_probe(power_token(power_high), spi_mode_token(options), power_high, options);
    configure_sd_bus(power_high);
    SPI1.begin();
    SPI1.beginTransaction(SPISettings(SD_PROBE_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(SD_CS_PIN, HIGH);
    for (uint8_t i = 0; i < 10; ++i) {
        (void)sd_spi_transfer(0xFF);
    }

    const uint8_t cmd0 = sd_command(0, 0, 0x95, nullptr, 0);
    if (cmd0 == 0xFF) {
        probe.error_code = 1;
        SPI1.endTransaction();
        return probe;
    }
    if ((cmd0 & 0x01U) == 0) {
        probe.present = true;
        probe.error_code = 0;
        SPI1.endTransaction();
        return probe;
    }

    uint8_t cmd8_extra[4] = {0, 0, 0, 0};
    const uint8_t cmd8 = sd_command(8, 0x1AA, 0x87, cmd8_extra, sizeof(cmd8_extra));
    const bool sd_v2 = (cmd8 & 0x04U) == 0;
    const uint32_t init_start = millis();
    uint8_t acmd41 = 0xFF;
    do {
        (void)sd_command(55, 0, 0x65, nullptr, 0);
        acmd41 = sd_command(41, sd_v2 ? 0x40000000UL : 0, 0x77, nullptr, 0);
        if (acmd41 == 0) {
            probe.present = true;
            probe.error_code = 0;
            break;
        }
        delay(10);
    } while (millis() - init_start < 750);

    if (!probe.present) {
        probe.error_code = acmd41 == 0xFF ? 2 : acmd41;
        probe.error_data = cmd8;
        SPI1.endTransaction();
        return probe;
    }

    uint8_t ocr[4] = {0, 0, 0, 0};
    (void)sd_command(58, 0, 0xFD, ocr, sizeof(ocr));
    probe.error_data = ocr[0];
    SPI1.endTransaction();
    return probe;
}

SdSpiConfig sd_spi_config(uint8_t options) {
    return SdSpiConfig(SD_CS_PIN, options, SD_SPI_HZ, &SPI1);
}

CardProbe probe_card(uint8_t options, bool power_high) {
    CardProbe probe = {
        false,
        0,
        0xFE,
        0,
        power_token(power_high),
        spi_mode_token(options),
        power_high,
        options,
    };
    configure_sd_bus(power_high);
    s_sd.end();
    SdCardFactory card_factory;
    SdCard *card = card_factory.newCard(sd_spi_config(options));
    if (!card) {
        return probe;
    }
    probe.error_code = card->errorCode();
    probe.error_data = card->errorData();
    if (probe.error_code != 0) {
        delete card;
        return probe;
    }

    probe.present = true;
    probe.capacity_kb = clamp_kb(static_cast<uint64_t>(card->sectorCount()) * 512ULL);
    delete card;
    return probe;
}

CardProbe default_probe() {
    return probe_card(s_sd_spi_options, s_sd_power_high);
}

const CardProbe *first_present_probe(const CardProbe *probes, size_t count) {
    if (!probes) {
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        if (probes[i].present) {
            return &probes[i];
        }
    }
    return nullptr;
}

void apply_probe_to_snapshot(SdSnapshot &snapshot, const CardProbe &probe) {
    snapshot.probe_power = probe.power;
    snapshot.probe_mode = probe.mode;
    snapshot.probe_present = probe.present;
    snapshot.probe_error = probe.error_code;
    snapshot.probe_data = probe.error_data;
}

SdSnapshot make_snapshot(const char *state, const char *note) {
    SdSnapshot snapshot = {
        state,
        false,
        false,
        false,
        "none",
        false,
        0,
        0,
        note,
        power_token(s_sd_power_high),
        spi_mode_token(s_sd_spi_options),
        false,
        0,
        0,
    };
    return snapshot;
}

SdSnapshot current_status() {
    if (s_cached_snapshot_valid) {
        return s_cached_snapshot;
    }
    return make_snapshot("mount_required", "mount_not_checked");
}

SdSnapshot cache_status(const SdSnapshot &snapshot) {
    s_cached_snapshot = snapshot;
    s_cached_snapshot_valid = true;
    return snapshot;
}

SdSnapshot pending_snapshot(const char *note) {
    SdSnapshot snapshot = make_snapshot("mount_pending", note);
    snapshot.probe_power = power_token(s_sd_power_high);
    snapshot.probe_mode = spi_mode_token(s_sd_spi_options);
    return snapshot;
}

void publish_worker_snapshot(const SdSnapshot &snapshot) {
    s_worker_snapshot = snapshot;
    ++s_worker_snapshot_revision;
}

void publish_worker_diag(const DiagSnapshot &diag) {
    s_worker_diag = diag;
    ++s_worker_diag_revision;
}

void refresh_worker_results() {
    const uint32_t snapshot_revision = s_worker_snapshot_revision;
    if (s_seen_snapshot_revision != snapshot_revision) {
        s_seen_snapshot_revision = snapshot_revision;
        (void)cache_status(s_worker_snapshot);
    }
    const uint32_t diag_revision = s_worker_diag_revision;
    if (s_seen_diag_revision != diag_revision) {
        s_seen_diag_revision = diag_revision;
        s_cached_diag = s_worker_diag;
        s_cached_diag_valid = true;
    }
}

bool start_sd_worker(SdWorkerRequest request) {
    refresh_worker_results();
    if (request == SD_WORKER_NONE || s_worker_busy || s_worker_request != SD_WORKER_NONE) {
        return false;
    }
    if (request == SD_WORKER_MOUNT) {
        s_mount_worker_completed = false;
    }
    s_worker_request = request;
    return true;
}

const char *fat_label() {
    switch (s_sd.vol()->fatType()) {
    case 12:
        return "fat12";
    case 16:
        return "fat16";
    case 32:
        return "fat32";
    default:
        return "fatfs";
    }
}

void fill_capacity(SdSnapshot &snapshot) {
    if (s_sd.vol()) {
        const uint64_t cluster_count = s_sd.vol()->clusterCount();
        const uint64_t free_count = s_sd.vol()->freeClusterCount();
        const uint64_t bytes_per_cluster = s_sd.vol()->bytesPerCluster();
        snapshot.capacity_kb = clamp_kb(cluster_count * bytes_per_cluster);
        snapshot.free_kb = free_count <= cluster_count ? clamp_kb(free_count * bytes_per_cluster) : 0;
        return;
    }
    snapshot.capacity_kb = 0;
    snapshot.free_kb = 0;
}

bool path_is_directory(const char *path) {
    FsFile file = s_sd.open(path, O_RDONLY);
    const bool is_dir = file && file.isDir();
    if (file) {
        file.close();
    }
    return is_dir;
}

bool ensure_directory(const char *path) {
    if (s_sd.exists(path)) {
        return path_is_directory(path);
    }
    return s_sd.mkdir(path) && path_is_directory(path);
}

bool manifest_valid() {
    if (!s_sd.exists(DESKOS_MANIFEST)) {
        return false;
    }
    FsFile file = s_sd.open(DESKOS_MANIFEST, O_RDONLY);
    if (!file || file.isDir()) {
        if (file) {
            file.close();
        }
        return false;
    }
    const uint32_t size = static_cast<uint32_t>(file.fileSize());
    if (size == 0 || size > 512) {
        file.close();
        return false;
    }
    char buffer[513];
    const int read_len = file.read(buffer, size);
    file.close();
    if (read_len <= 0) {
        return false;
    }
    buffer[read_len] = '\0';
    String text(buffer);
    return text.indexOf("\"schema\":1") >= 0 &&
           text.indexOf("\"name\":\"MeshCore DeskOS D1L SD\"") >= 0 &&
           text.indexOf("\"device\":\"seeed-indicator-d1l\"") >= 0 &&
           text.indexOf("\"map_tiles\"") >= 0;
}

bool map_manifest_valid() {
    if (!s_sd.exists(DESKOS_MAP_MANIFEST)) {
        return false;
    }
    FsFile file = s_sd.open(DESKOS_MAP_MANIFEST, O_RDONLY);
    if (!file || file.isDir()) {
        if (file) {
            file.close();
        }
        return false;
    }
    const uint32_t size = static_cast<uint32_t>(file.fileSize());
    if (size == 0 || size > 512) {
        file.close();
        return false;
    }
    char buffer[513];
    const int read_len = file.read(buffer, size);
    file.close();
    if (read_len <= 0) {
        return false;
    }
    buffer[read_len] = '\0';
    String text(buffer);
    return text.indexOf("\"schema\":1") >= 0 &&
           text.indexOf("\"kind\":\"map_cache\"") >= 0 &&
           text.indexOf("\"tile_template\":\"map/tiles/z{z}/x{x}/y{y}.tile\"") >= 0;
}

bool write_text_file(const char *path, const char *payload) {
    FsFile file = s_sd.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) {
        return false;
    }
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
    file.sync();
    file.close();
    return written == strlen(payload);
}

bool write_manifest() {
    return write_text_file(DESKOS_MANIFEST, DESKOS_MANIFEST_PAYLOAD) && manifest_valid();
}

bool write_map_manifest() {
    return write_text_file(DESKOS_MAP_MANIFEST, DESKOS_MAP_MANIFEST_PAYLOAD) &&
           map_manifest_valid();
}

bool prepare_deskos_structure(const char **note) {
    bool created = false;
    if (!s_sd.exists(DESKOS_ROOT)) {
        created = true;
    }
    if (!ensure_directory(DESKOS_ROOT)) {
        *note = "deskos_root_unavailable";
        return false;
    }
    for (size_t i = 0; i < sizeof(DESKOS_REQUIRED_DIRS) / sizeof(DESKOS_REQUIRED_DIRS[0]); ++i) {
        if (!s_sd.exists(DESKOS_REQUIRED_DIRS[i])) {
            created = true;
        }
        if (!ensure_directory(DESKOS_REQUIRED_DIRS[i])) {
            *note = "deskos_structure_unavailable";
            return false;
        }
    }
    if (s_sd.exists(DESKOS_MANIFEST)) {
        if (!manifest_valid()) {
            *note = "deskos_manifest_invalid";
            return false;
        }
    } else {
        created = true;
        if (!write_manifest()) {
            *note = "deskos_manifest_unavailable";
            return false;
        }
    }
    if (s_sd.exists(DESKOS_MAP_MANIFEST)) {
        if (!map_manifest_valid()) {
            *note = "deskos_map_manifest_invalid";
            return false;
        }
    } else {
        created = true;
        if (!write_map_manifest()) {
            *note = "deskos_map_manifest_unavailable";
            return false;
        }
    }
    *note = created ? "structure_created" : "ready";
    return true;
}

SdSnapshot mount_status_blocking() {
    SdSnapshot snapshot = make_snapshot("no_card", "no_card");
    s_sd_power_high = true;
    s_sd_spi_options = DEDICATED_SPI;
    snapshot = pending_snapshot("filesystem_mounting");
    publish_worker_snapshot(snapshot);
    if (mount_sd()) {
        CardProbe mounted_probe = {
            true,
            0,
            0,
            0,
            power_token(s_sd_power_high),
            "mount",
            s_sd_power_high,
            s_sd_spi_options,
        };
        apply_probe_to_snapshot(snapshot, mounted_probe);
        snapshot.state = "creating_deskos_files";
        snapshot.present = true;
        snapshot.mounted = true;
        snapshot.deskos = false;
        snapshot.fs = fat_label();
        snapshot.note = "deskos_root_missing";
        fill_capacity(snapshot);

        const char *prepare_note = "ready";
        if (prepare_deskos_structure(&prepare_note)) {
            snapshot.state = "ready";
            snapshot.deskos = true;
            snapshot.note = prepare_note;
        } else {
            snapshot.state = strcmp(prepare_note, "deskos_manifest_invalid") == 0 ||
                             strcmp(prepare_note, "deskos_map_manifest_invalid") == 0 ?
                             "deskos_manifest_invalid" : "error";
            snapshot.note = prepare_note;
        }

        return snapshot;
    }

    snapshot = make_snapshot("no_card", "no_card");
    CardProbe probe = manual_probe_card(DEDICATED_SPI, true);
    if (!probe.present) {
        CardProbe probes[] = {
            probe,
            manual_probe_card(SHARED_SPI, true),
            manual_probe_card(DEDICATED_SPI, false),
            manual_probe_card(SHARED_SPI, false),
        };
        const CardProbe *selected = first_present_probe(probes, sizeof(probes) / sizeof(probes[0]));
        if (!selected) {
            apply_probe_to_snapshot(snapshot, probe);
            s_sd_power_high = true;
            s_sd_spi_options = DEDICATED_SPI;
            configure_sd_bus();
            return snapshot;
        }
        probe = *selected;
    }

    s_sd_power_high = probe.power_high;
    s_sd_spi_options = probe.options;
    apply_probe_to_snapshot(snapshot, probe);
    snapshot.state = "mount_pending";
    snapshot.present = true;
    snapshot.fs = "unknown";
    snapshot.capacity_kb = probe.capacity_kb;
    snapshot.note = "card_detected_mounting";
    publish_worker_snapshot(snapshot);

    if (!mount_sd()) {
        snapshot.state = "not_fat32_or_unmountable";
        snapshot.present = true;
        snapshot.fs = "unknown";
        snapshot.needs_fat32 = true;
        snapshot.capacity_kb = probe.capacity_kb;
        snapshot.note = "needs_fat32_on_computer";
        return snapshot;
    }

    CardProbe mounted_probe = {
        true,
        0,
        0,
        0,
        power_token(s_sd_power_high),
        "mount",
        s_sd_power_high,
        s_sd_spi_options,
    };
    apply_probe_to_snapshot(snapshot, mounted_probe);
    snapshot.state = "creating_deskos_files";
    snapshot.present = true;
    snapshot.mounted = true;
    snapshot.deskos = false;
    snapshot.fs = fat_label();
    snapshot.note = "deskos_root_missing";
    fill_capacity(snapshot);

    const char *prepare_note = "ready";
    if (prepare_deskos_structure(&prepare_note)) {
        snapshot.state = "ready";
        snapshot.deskos = true;
        snapshot.note = prepare_note;
    } else {
        snapshot.state = strcmp(prepare_note, "deskos_manifest_invalid") == 0 ||
                         strcmp(prepare_note, "deskos_map_manifest_invalid") == 0 ?
                         "deskos_manifest_invalid" : "error";
        snapshot.note = prepare_note;
    }

    return snapshot;
}

SdSnapshot mount_status() {
    refresh_worker_results();
    if (s_mount_worker_completed && !s_worker_busy && s_worker_request == SD_WORKER_NONE &&
        s_cached_snapshot_valid) {
        s_mount_worker_completed = false;
        return s_cached_snapshot;
    }
    if (start_sd_worker(SD_WORKER_MOUNT)) {
        return cache_status(pending_snapshot("mount_started"));
    }
    return cache_status(pending_snapshot(s_worker_busy ? "mount_in_progress" : "mount_queued"));
}

DiagSnapshot pending_diag_snapshot() {
    DiagSnapshot diag = {
        empty_probe("high", "dedicated", true, DEDICATED_SPI),
        empty_probe("high", "shared", true, SHARED_SPI),
        empty_probe("low", "dedicated", false, DEDICATED_SPI),
        empty_probe("low", "shared", false, SHARED_SPI),
        "pending",
        "pending",
        false,
        false,
    };
    return diag;
}

DiagSnapshot diag_status_blocking() {
    DiagSnapshot diag = pending_diag_snapshot();
    diag.high_dedicated = manual_probe_card(DEDICATED_SPI, true);
    diag.high_shared = manual_probe_card(SHARED_SPI, true);
    diag.low_dedicated = manual_probe_card(DEDICATED_SPI, false);
    diag.low_shared = manual_probe_card(SHARED_SPI, false);
    const CardProbe probes[] = {diag.high_dedicated, diag.high_shared, diag.low_dedicated, diag.low_shared};
    const CardProbe *selected = first_present_probe(probes, sizeof(probes) / sizeof(probes[0]));
    if (selected) {
        s_sd_power_high = selected->power_high;
        s_sd_spi_options = selected->options;
    } else {
        s_sd_power_high = true;
        s_sd_spi_options = DEDICATED_SPI;
    }
    diag.selected_power = power_token(s_sd_power_high);
    diag.selected_mode = spi_mode_token(s_sd_spi_options);
    diag.mount_selected = false;
    diag.valid = true;
    if (!selected) {
        configure_sd_bus();
    }
    return diag;
}

String bool_token(bool value) {
    return value ? "1" : "0";
}

uint32_t crc32_bytes(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

String crc32_token(const uint8_t *data, size_t len) {
    char token[9];
    snprintf(token, sizeof(token), "%08lX", static_cast<unsigned long>(crc32_bytes(data, len)));
    return String(token);
}

int base64url_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '-') {
        return 62;
    }
    if (c == '_') {
        return 63;
    }
    return -1;
}

bool decode_base64url(const char *input, uint8_t *out, size_t out_size, size_t *out_len) {
    if (!input || !out || !out_len) {
        return false;
    }
    const size_t input_len = strlen(input);
    if ((input_len % 4U) == 1U) {
        return false;
    }

    uint32_t value = 0;
    int value_bits = -8;
    size_t used = 0;
    for (size_t i = 0; i < input_len; ++i) {
        const int part = base64url_value(input[i]);
        if (part < 0) {
            return false;
        }
        value = (value << 6) | static_cast<uint32_t>(part);
        value_bits += 6;
        if (value_bits >= 0) {
            if (used >= out_size) {
                return false;
            }
            out[used++] = static_cast<uint8_t>((value >> value_bits) & 0xFFU);
            value_bits -= 8;
        }
    }
    *out_len = used;
    return true;
}

String encode_base64url(const uint8_t *data, size_t len) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    String out;
    out.reserve(((len + 2U) / 3U) * 4U);
    for (size_t i = 0; i < len; i += 3U) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1U < len) ? data[i + 1U] : 0U;
        const uint32_t b2 = (i + 2U < len) ? data[i + 2U] : 0U;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out += table[(triple >> 18) & 0x3FU];
        out += table[(triple >> 12) & 0x3FU];
        if (i + 1U < len) {
            out += table[(triple >> 6) & 0x3FU];
        }
        if (i + 2U < len) {
            out += table[triple & 0x3FU];
        }
    }
    return out;
}

bool copy_token_value(const char *line, const char *key, char *dest, size_t dest_size) {
    if (!line || !key || !dest || dest_size == 0) {
        return false;
    }
    const size_t key_len = strlen(key);
    const char *p = line;
    while (*p) {
        while (*p == ' ') {
            ++p;
        }
        const char *token = p;
        while (*p && *p != ' ') {
            ++p;
        }
        const size_t token_len = static_cast<size_t>(p - token);
        if (token_len >= key_len + 1U &&
            strncmp(token, key, key_len) == 0 &&
            token[key_len] == '=') {
            const size_t value_len = token_len - key_len - 1U;
            if (value_len + 1U > dest_size) {
                return false;
            }
            memcpy(dest, token + key_len + 1U, value_len);
            dest[value_len] = '\0';
            return true;
        }
    }
    return false;
}

bool parse_u32_token(const char *line, const char *key, uint32_t *out_value) {
    char value[16];
    if (!copy_token_value(line, key, value, sizeof(value)) || value[0] == '\0') {
        return false;
    }
    uint32_t parsed = 0;
    for (size_t i = 0; value[i] != '\0'; ++i) {
        if (value[i] < '0' || value[i] > '9') {
            return false;
        }
        const uint32_t digit = static_cast<uint32_t>(value[i] - '0');
        if (parsed > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        parsed = (parsed * 10U) + digit;
    }
    *out_value = parsed;
    return true;
}

bool parse_bool_token(const char *line, const char *key, bool *out_value) {
    char value[2];
    if (!copy_token_value(line, key, value, sizeof(value))) {
        return false;
    }
    if (strcmp(value, "1") == 0) {
        *out_value = true;
        return true;
    }
    if (strcmp(value, "0") == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

bool is_path_char(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-' || c == '/';
}

bool validate_relative_path(const char *path) {
    if (!path) {
        return false;
    }
    const size_t len = strlen(path);
    if (len == 0 || len > FILE_PATH_MAX || path[0] == '/' || path[len - 1U] == '/') {
        return false;
    }
    if (strstr(path, "..") || strstr(path, "//")) {
        return false;
    }
    const char *segment = path;
    for (size_t i = 0; i <= len; ++i) {
        const char c = path[i];
        if (c == '/' || c == '\0') {
            const size_t segment_len = static_cast<size_t>(&path[i] - segment);
            if (segment_len == 0 ||
                (segment_len == 1 && segment[0] == '.') ||
                (segment_len == 2 && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            segment = &path[i + 1U];
            continue;
        }
        if (!is_path_char(c)) {
            return false;
        }
    }
    return true;
}

bool decode_path_token(const char *line, const char *key, char *path, size_t path_size) {
    char encoded[FILE_PATH64_MAX + 1U];
    uint8_t decoded[FILE_PATH_MAX + 1U];
    size_t decoded_len = 0;
    if (!copy_token_value(line, key, encoded, sizeof(encoded))) {
        return false;
    }
    if (!decode_base64url(encoded, decoded, FILE_PATH_MAX, &decoded_len)) {
        return false;
    }
    if (decoded_len + 1U > path_size) {
        return false;
    }
    decoded[decoded_len] = '\0';
    memcpy(path, decoded, decoded_len + 1U);
    return validate_relative_path(path);
}

bool make_full_path(const char *relative, char *full, size_t full_size) {
    if (!validate_relative_path(relative)) {
        return false;
    }
    const int written = snprintf(full, full_size, "%s/%s", DESKOS_ROOT, relative);
    return written > 0 && static_cast<size_t>(written) < full_size;
}

bool ensure_parent_dirs(const char *full_path) {
    char tmp[FILE_FULL_PATH_MAX];
    if (!full_path || strlen(full_path) >= sizeof(tmp)) {
        return false;
    }
    strcpy(tmp, full_path);
    const size_t root_len = strlen(DESKOS_ROOT);
    for (char *p = tmp + root_len + 1U; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (!s_sd.exists(tmp) && !s_sd.mkdir(tmp)) {
            *p = '/';
            return false;
        }
        *p = '/';
    }
    return true;
}

bool make_backup_path(const char *target_path, char *backup_path, size_t backup_size) {
    if (!target_path || !backup_path || backup_size == 0) {
        return false;
    }
    const int written = snprintf(backup_path, backup_size, "%s%s",
                                 target_path, REPLACE_BACKUP_SUFFIX);
    return written > 0 && static_cast<size_t>(written) < backup_size;
}

const char *rename_replace_preserving_old(const char *source_path, const char *target_path) {
    if (!s_sd.exists(target_path)) {
        return s_sd.rename(source_path, target_path) ? nullptr : "rename_failed";
    }

    char backup_path[FILE_FULL_PATH_MAX];
    if (!make_backup_path(target_path, backup_path, sizeof(backup_path))) {
        return "too_large";
    }
    if (s_sd.exists(backup_path) && !s_sd.remove(backup_path)) {
        return "delete_failed";
    }
    if (!s_sd.rename(target_path, backup_path)) {
        return "rename_failed";
    }
    if (!s_sd.rename(source_path, target_path)) {
        (void)s_sd.rename(backup_path, target_path);
        return "rename_failed";
    }
    if (s_sd.exists(backup_path) && !s_sd.remove(backup_path)) {
        return "delete_failed";
    }
    return nullptr;
}

void send_snapshot(Stream &out, const char *prefix, const SdSnapshot &snapshot) {
    const bool file_ready = snapshot.present && snapshot.mounted && snapshot.deskos &&
                            !snapshot.needs_fat32;
    String line(prefix);
    line += " state=";
    line += snapshot.state;
    line += " present=";
    line += bool_token(snapshot.present);
    line += " mounted=";
    line += bool_token(snapshot.mounted);
    line += " deskos=";
    line += bool_token(snapshot.deskos);
    line += " fs=";
    line += snapshot.fs;
    line += " needs_fat32=";
    line += bool_token(snapshot.needs_fat32);
    line += " capacity_kb=";
    line += String(static_cast<unsigned long>(snapshot.capacity_kb));
    line += " free_kb=";
    line += String(static_cast<unsigned long>(snapshot.free_kb));
    line += " note=";
    line += snapshot.note;
    line += " probe_power=";
    line += snapshot.probe_power;
    line += " probe_mode=";
    line += snapshot.probe_mode;
    line += " probe_present=";
    line += bool_token(snapshot.probe_present);
    line += " probe_err=";
    line += String(static_cast<unsigned int>(snapshot.probe_error));
    line += " probe_data=";
    line += String(static_cast<unsigned int>(snapshot.probe_data));
    line += " mount_err=";
    line += String(static_cast<unsigned int>(s_last_mount_error));
    line += " mount_data=";
    line += String(static_cast<unsigned int>(s_last_mount_data));
    line += " file_ops=";
    line += bool_token(file_ready);
    line += " file_line_max=";
    line += String(static_cast<unsigned long>(FILE_LINE_MAX));
    line += " file_chunk_max=";
    line += String(static_cast<unsigned long>(FILE_CHUNK_MAX));
    line += " path_max=";
    line += String(static_cast<unsigned long>(FILE_PATH_MAX));
    line += " atomic_rename=";
    line += bool_token(file_ready && REPLACE_RENAME_PRESERVES_OLD_ON_FAILURE);
    out.println(line);
}

void append_probe_tokens(String &line, const char *prefix, const CardProbe &probe) {
    line += " ";
    line += prefix;
    line += "_p=";
    line += bool_token(probe.present);
    line += " ";
    line += prefix;
    line += "_e=";
    line += String(static_cast<unsigned int>(probe.error_code));
    line += " ";
    line += prefix;
    line += "_d=";
    line += String(static_cast<unsigned int>(probe.error_data));
    line += " ";
    line += prefix;
    line += "_kb=";
    line += String(static_cast<unsigned long>(probe.capacity_kb));
}

void send_diag_snapshot(const DiagSnapshot &diag) {
    String line(DIAG_REPLY);
    line += " pins=cs13-sck10-mosi11-miso12-pwr18";
    line += " hz=";
    line += String(static_cast<unsigned long>(SD_SPI_HZ));
    line += " selected_power=";
    line += diag.selected_power;
    line += " selected_mode=";
    line += diag.selected_mode;
    line += " mount_selected=";
    line += bool_token(diag.mount_selected);
    append_probe_tokens(line, "hd", diag.high_dedicated);
    append_probe_tokens(line, "hs", diag.high_shared);
    append_probe_tokens(line, "ld", diag.low_dedicated);
    append_probe_tokens(line, "ls", diag.low_shared);
    reply_stream->println(line);
}

void send_diag() {
    refresh_worker_results();
    if (s_cached_diag_valid && !s_worker_busy && s_worker_request == SD_WORKER_NONE) {
        send_diag_snapshot(s_cached_diag);
        s_cached_diag_valid = false;
        return;
    }
    (void)start_sd_worker(SD_WORKER_DIAG);
    send_diag_snapshot(pending_diag_snapshot());
}

void send_status() {
    refresh_worker_results();
    send_snapshot(*reply_stream, STATUS_REPLY, current_status());
}

void send_mount_status() {
    send_snapshot(*reply_stream, MOUNT_REPLY, mount_status());
}

void send_ping() {
    String line(PING_REPLY);
    line += " v=1";
    line += " file_line_max=";
    line += String(static_cast<unsigned long>(FILE_LINE_MAX));
    line += " file_chunk_max=";
    line += String(static_cast<unsigned long>(FILE_CHUNK_MAX));
    line += " path_max=";
    line += String(static_cast<unsigned long>(FILE_PATH_MAX));
    line += " atomic_rename=";
    line += bool_token(REPLACE_RENAME_PRESERVES_OLD_ON_FAILURE);
    line += " sd_touch=0";
    reply_stream->println(line);
}

void send_file_error(uint32_t request_id, const char *op, const char *err) {
    String line(FILE_REPLY);
    line += " v=1 id=";
    line += String(static_cast<unsigned long>(request_id));
    line += " ok=0 op=";
    line += ((op && op[0]) ? op : "unknown");
    line += " err=";
    line += err;
    line += " note=";
    line += err;
    reply_stream->println(line);
}

bool parse_file_header(const char *line, uint32_t *request_id, char *op, size_t op_size) {
    uint32_t version = 0;
    if (!parse_u32_token(line, "v", &version) || version != FILE_PROTOCOL_VERSION) {
        return false;
    }
    if (!parse_u32_token(line, "id", request_id) || *request_id == 0 || *request_id > 65535U) {
        return false;
    }
    return copy_token_value(line, "op", op, op_size);
}

bool decode_file_data(const char *line, uint8_t *data, size_t data_size, size_t *data_len,
                      const char **err) {
    uint32_t expected_len = 0;
    if (!parse_u32_token(line, "len", &expected_len) || expected_len > FILE_CHUNK_MAX) {
        *err = "too_large";
        return false;
    }

    char encoded[FILE_DATA64_MAX + 1U];
    if (!copy_token_value(line, "data", encoded, sizeof(encoded))) {
        *err = "decode_failed";
        return false;
    }
    if (!decode_base64url(encoded, data, data_size, data_len)) {
        *err = "decode_failed";
        return false;
    }
    if (*data_len != expected_len) {
        *err = "bad_value";
        return false;
    }

    char expected_crc[9];
    if (!copy_token_value(line, "crc", expected_crc, sizeof(expected_crc))) {
        *err = "bad_value";
        return false;
    }
    const String actual_crc = crc32_token(data, *data_len);
    if (strcmp(actual_crc.c_str(), expected_crc) != 0) {
        *err = "crc_mismatch";
        return false;
    }
    return true;
}

void handle_file_stat(uint32_t request_id, const char *line) {
    char relative[FILE_PATH_MAX + 1U];
    char full_path[FILE_FULL_PATH_MAX];
    if (!decode_path_token(line, "path", relative, sizeof(relative)) ||
        !make_full_path(relative, full_path, sizeof(full_path))) {
        send_file_error(request_id, "stat", "bad_path");
        return;
    }

    String out(FILE_REPLY);
    out += " v=1 id=";
    out += String(static_cast<unsigned long>(request_id));
    out += " ok=1 op=stat exists=";
    if (!s_sd.exists(full_path)) {
        out += "0 kind=none size=0 note=ok";
        reply_stream->println(out);
        return;
    }

    FsFile file = s_sd.open(full_path, O_RDONLY);
    if (!file) {
        send_file_error(request_id, "stat", "open_failed");
        return;
    }
    out += "1 kind=";
    out += file.isDir() ? "dir" : "file";
    out += " size=";
    out += String(static_cast<unsigned long>(file.isDir() ? 0 : file.fileSize()));
    out += " note=ok";
    file.close();
    reply_stream->println(out);
}

void handle_file_read(uint32_t request_id, const char *line) {
    char relative[FILE_PATH_MAX + 1U];
    char full_path[FILE_FULL_PATH_MAX];
    uint32_t offset = 0;
    uint32_t requested_len = 0;
    if (!decode_path_token(line, "path", relative, sizeof(relative)) ||
        !make_full_path(relative, full_path, sizeof(full_path))) {
        send_file_error(request_id, "read", "bad_path");
        return;
    }
    if (!parse_u32_token(line, "off", &offset) ||
        !parse_u32_token(line, "len", &requested_len)) {
        send_file_error(request_id, "read", "range");
        return;
    }
    if (requested_len > FILE_CHUNK_MAX) {
        send_file_error(request_id, "read", "too_large");
        return;
    }
    if (!s_sd.exists(full_path)) {
        send_file_error(request_id, "read", "not_found");
        return;
    }

    FsFile file = s_sd.open(full_path, O_RDONLY);
    if (!file || file.isDir()) {
        send_file_error(request_id, "read", file && file.isDir() ? "is_dir" : "open_failed");
        if (file) {
            file.close();
        }
        return;
    }
    const uint32_t file_size = static_cast<uint32_t>(file.fileSize());
    if (offset > file_size || !file.seekSet(offset)) {
        file.close();
        send_file_error(request_id, "read", "range");
        return;
    }

    uint8_t data[FILE_CHUNK_MAX];
    const int read_len = file.read(data, requested_len);
    file.close();
    if (read_len < 0) {
        send_file_error(request_id, "read", "read_failed");
        return;
    }
    const size_t used = static_cast<size_t>(read_len);
    String out(FILE_REPLY);
    out += " v=1 id=";
    out += String(static_cast<unsigned long>(request_id));
    out += " ok=1 op=read off=";
    out += String(static_cast<unsigned long>(offset));
    out += " len=";
    out += String(static_cast<unsigned long>(used));
    out += " eof=";
    out += bool_token(offset + used >= file_size);
    out += " data=";
    out += encode_base64url(data, used);
    out += " crc=";
    out += crc32_token(data, used);
    out += " note=ok";
    reply_stream->println(out);
}

void handle_file_write(uint32_t request_id, const char *line, bool append_mode) {
    char op_name[8];
    snprintf(op_name, sizeof(op_name), "%s", append_mode ? "append" : "write");
    char relative[FILE_PATH_MAX + 1U];
    char full_path[FILE_FULL_PATH_MAX];
    uint8_t data[FILE_CHUNK_MAX];
    size_t data_len = 0;
    const char *err = "bad_request";
    if (!decode_path_token(line, "path", relative, sizeof(relative)) ||
        !make_full_path(relative, full_path, sizeof(full_path))) {
        send_file_error(request_id, op_name, "bad_path");
        return;
    }
    if (!decode_file_data(line, data, sizeof(data), &data_len, &err)) {
        send_file_error(request_id, op_name, err);
        return;
    }
    if (append_mode && data_len == 0U) {
        send_file_error(request_id, op_name, "bad_value");
        return;
    }

    uint32_t offset = 0;
    bool truncate = false;
    if (!append_mode) {
        if (!parse_u32_token(line, "off", &offset) ||
            !parse_bool_token(line, "trunc", &truncate)) {
            send_file_error(request_id, op_name, "bad_value");
            return;
        }
        if (truncate && offset != 0U) {
            send_file_error(request_id, op_name, "range");
            return;
        }
    }

    if (!ensure_parent_dirs(full_path)) {
        send_file_error(request_id, op_name, "open_failed");
        return;
    }

    uint32_t current_size = 0;
    if (!truncate && s_sd.exists(full_path)) {
        FsFile existing = s_sd.open(full_path, O_RDONLY);
        if (!existing || existing.isDir()) {
            send_file_error(request_id, op_name, existing && existing.isDir() ? "is_dir" : "open_failed");
            if (existing) {
                existing.close();
            }
            return;
        }
        current_size = static_cast<uint32_t>(existing.fileSize());
        existing.close();
    }
    if (append_mode) {
        offset = current_size;
    } else if (offset != current_size) {
        send_file_error(request_id, op_name, "range");
        return;
    }

    FsFile file = s_sd.open(full_path, truncate ? (O_WRONLY | O_CREAT | O_TRUNC) : (O_WRONLY | O_CREAT | O_APPEND | O_AT_END));
    if (!file) {
        send_file_error(request_id, op_name, "open_failed");
        return;
    }
    const size_t written = data_len == 0 ? 0 : file.write(data, data_len);
    file.sync();
    const uint32_t new_size = static_cast<uint32_t>(file.fileSize());
    file.close();
    if (written != data_len) {
        send_file_error(request_id, op_name, "write_failed");
        return;
    }

    String out(FILE_REPLY);
    out += " v=1 id=";
    out += String(static_cast<unsigned long>(request_id));
    out += " ok=1 op=";
    out += op_name;
    out += " off=";
    out += String(static_cast<unsigned long>(offset));
    out += " len=";
    out += String(static_cast<unsigned long>(data_len));
    out += " size=";
    out += String(static_cast<unsigned long>(new_size));
    out += " note=ok";
    reply_stream->println(out);
}

void handle_file_delete(uint32_t request_id, const char *line) {
    char relative[FILE_PATH_MAX + 1U];
    char full_path[FILE_FULL_PATH_MAX];
    if (!decode_path_token(line, "path", relative, sizeof(relative)) ||
        !make_full_path(relative, full_path, sizeof(full_path))) {
        send_file_error(request_id, "delete", "bad_path");
        return;
    }
    if (!s_sd.exists(full_path)) {
        send_file_error(request_id, "delete", "not_found");
        return;
    }
    if (!s_sd.remove(full_path)) {
        send_file_error(request_id, "delete", "delete_failed");
        return;
    }
    String out(FILE_REPLY);
    out += " v=1 id=";
    out += String(static_cast<unsigned long>(request_id));
    out += " ok=1 op=delete note=ok";
    reply_stream->println(out);
}

void handle_file_rename(uint32_t request_id, const char *line) {
    char source_relative[FILE_PATH_MAX + 1U];
    char target_relative[FILE_PATH_MAX + 1U];
    char source_path[FILE_FULL_PATH_MAX];
    char target_path[FILE_FULL_PATH_MAX];
    bool replace_target = false;
    if (!decode_path_token(line, "path", source_relative, sizeof(source_relative)) ||
        !decode_path_token(line, "to", target_relative, sizeof(target_relative)) ||
        !make_full_path(source_relative, source_path, sizeof(source_path)) ||
        !make_full_path(target_relative, target_path, sizeof(target_path)) ||
        !parse_bool_token(line, "replace", &replace_target)) {
        send_file_error(request_id, "rename", "bad_path");
        return;
    }
    if (!s_sd.exists(source_path)) {
        send_file_error(request_id, "rename", "not_found");
        return;
    }
    if (!ensure_parent_dirs(target_path)) {
        send_file_error(request_id, "rename", "open_failed");
        return;
    }
    if (s_sd.exists(target_path) && !replace_target) {
        send_file_error(request_id, "rename", "exists");
        return;
    }

    const char *replace_error = replace_target
                                    ? rename_replace_preserving_old(source_path, target_path)
                                    : (s_sd.rename(source_path, target_path) ? nullptr : "rename_failed");
    if (replace_error) {
        send_file_error(request_id, "rename", replace_error);
        return;
    }
    if (!s_sd.exists(target_path)) {
        send_file_error(request_id, "rename", "rename_failed");
        return;
    }
    String out(FILE_REPLY);
    out += " v=1 id=";
    out += String(static_cast<unsigned long>(request_id));
    out += " ok=1 op=rename note=ok";
    reply_stream->println(out);
}

void handle_file_line(const char *line) {
    uint32_t request_id = 0;
    char op[16];
    if (!parse_file_header(line, &request_id, op, sizeof(op))) {
        send_file_error(0, "unknown", "bad_request");
        return;
    }

    SdSnapshot status = mount_status();
    if (!status.present) {
        send_file_error(request_id, op, "no_card");
        return;
    }
    if (!status.mounted || !status.deskos || status.needs_fat32) {
        send_file_error(request_id, op, "not_ready");
        return;
    }

    if (strcmp(op, "stat") == 0) {
        handle_file_stat(request_id, line);
    } else if (strcmp(op, "read") == 0) {
        handle_file_read(request_id, line);
    } else if (strcmp(op, "write") == 0) {
        handle_file_write(request_id, line, false);
    } else if (strcmp(op, "append") == 0) {
        handle_file_write(request_id, line, true);
    } else if (strcmp(op, "delete") == 0) {
        handle_file_delete(request_id, line);
    } else if (strcmp(op, "rename") == 0) {
        handle_file_rename(request_id, line);
    } else {
        send_file_error(request_id, op, "unsupported_op");
    }
}

void handle_line(char *line) {
    while (*line == ' ' || *line == '\t') {
        ++line;
    }
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }

    if (strcmp(line, PING_REQUEST) == 0) {
        send_ping();
        return;
    }

    if (strcmp(line, STATUS_REQUEST) == 0) {
        send_status();
        return;
    }

    if (strcmp(line, MOUNT_REQUEST) == 0) {
        send_mount_status();
        return;
    }

    if (strcmp(line, DIAG_REQUEST) == 0) {
        send_diag();
        return;
    }

    constexpr size_t file_prefix_len = sizeof(FILE_REQUEST) - 1;
    if (strncmp(line, FILE_REQUEST, file_prefix_len) == 0 &&
        (line[file_prefix_len] == '\0' || line[file_prefix_len] == ' ')) {
        handle_file_line(line);
        return;
    }

    SdSnapshot unsupported = {
        "error",
        false,
        false,
        false,
        "none",
        false,
        0,
        0,
        "unsupported_request",
        power_token(s_sd_power_high),
        spi_mode_token(s_sd_spi_options),
        false,
        0,
        0,
    };
    send_snapshot(*reply_stream, STATUS_REPLY, unsupported);
}

void handle_line_for_stream(char *line, Stream &out) {
    Stream *previous = reply_stream;
    reply_stream = &out;
    handle_line(line);
    reply_stream = previous;
}

void poll_stream(Stream &in, Stream &out, LineRx &rx) {
    while (in.available() > 0) {
        const char c = static_cast<char>(in.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (!rx.drop_until_newline) {
                rx.line[rx.len] = '\0';
                if (rx.len > 0) {
                    handle_line_for_stream(rx.line, out);
                }
            }
            rx.len = 0;
            rx.drop_until_newline = false;
            continue;
        }
        if (rx.drop_until_newline) {
            continue;
        }
        if (rx.len + 1 < sizeof(rx.line)) {
            rx.line[rx.len++] = c;
        } else {
            rx.len = 0;
            rx.drop_until_newline = true;
            Stream *previous = reply_stream;
            reply_stream = &out;
            send_file_error(0, "unknown", "line_too_long");
            reply_stream = previous;
        }
    }
}

void sd_worker_loop_once() {
    const uint8_t request = s_worker_request;
    if (request == SD_WORKER_NONE || s_worker_busy) {
        delay(2);
        return;
    }

    s_worker_busy = true;
    s_worker_request = SD_WORKER_NONE;
    if (request == SD_WORKER_MOUNT) {
        publish_worker_snapshot(mount_status_blocking());
        s_mount_worker_completed = true;
    } else if (request == SD_WORKER_DIAG) {
        publish_worker_diag(diag_status_blocking());
    }
    s_worker_busy = false;
}

} // namespace

void setup() {
    Serial.begin(115200);
    Serial1.setRX(RP2040_ESP32_RX_PIN);
    Serial1.setTX(RP2040_ESP32_TX_PIN);
    Serial1.begin(ESP32_BRIDGE_BAUD);
    configure_sd_bus();
    delay(50);
    Serial.println("DeskOS RP2040 SD bridge ready");
}

void loop() {
    poll_stream(Serial1, Serial1, bridge_rx);
    poll_stream(Serial, Serial, usb_rx);
    delay(1);
}

void setup1() {
    delay(100);
}

void loop1() {
    sd_worker_loop_once();
}
