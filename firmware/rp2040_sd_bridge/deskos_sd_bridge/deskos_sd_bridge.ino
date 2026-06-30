#include <Arduino.h>
#include <SD.h>
#include <SDFS.h>
#include <SPI.h>

namespace {

constexpr const char *STATUS_REQUEST = "DESKOS_SD_STATUS";
constexpr const char *STATUS_REPLY = "DESKOS_SD_STATUS";
constexpr const char *FORMAT_REQUEST = "DESKOS_SD_FORMAT";
constexpr const char *FORMAT_REPLY = "DESKOS_SD_FORMAT";
constexpr const char *FORMAT_CONFIRMATION = "FORMAT-DESKOS-SD";

constexpr uint8_t RP2040_ESP32_RX_PIN = 17;
constexpr uint8_t RP2040_ESP32_TX_PIN = 16;
constexpr uint32_t ESP32_BRIDGE_BAUD = 115200;

constexpr uint8_t SD_CS_PIN = 13;
constexpr uint8_t SD_SCK_PIN = 10;
constexpr uint8_t SD_MOSI_PIN = 11;
constexpr uint8_t SD_MISO_PIN = 12;
constexpr uint32_t SD_SPI_HZ = 1000000U;
constexpr const char *DESKOS_ROOT = "/deskos";

constexpr size_t RX_LINE_MAX = 192;

struct SdSnapshot {
    const char *state;
    bool present;
    bool mounted;
    bool deskos;
    const char *fs;
    bool format_required;
    bool format_supported;
    uint32_t capacity_kb;
    uint32_t free_kb;
    const char *note;
};

char rx_line[RX_LINE_MAX];
size_t rx_len = 0;

uint32_t clamp_kb(uint64_t bytes) {
    const uint64_t kb = bytes / 1024ULL;
    return kb > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(kb);
}

void configure_sd_bus() {
    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
}

bool mount_sd() {
    configure_sd_bus();
    SD.end(false);
    return SD.begin(SD_CS_PIN, SD_SPI_HZ, SPI1);
}

const char *fat_label() {
    switch (SD.fatType()) {
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
    FSInfo info;
    if (SDFS.info(info)) {
        snapshot.capacity_kb = clamp_kb(info.totalBytes);
        snapshot.free_kb = info.usedBytes <= info.totalBytes
                                ? clamp_kb(info.totalBytes - info.usedBytes)
                                : 0;
    } else {
        snapshot.capacity_kb = clamp_kb(SD.size64());
        snapshot.free_kb = 0;
    }
}

SdSnapshot current_status() {
    SdSnapshot snapshot = {
        "setup_required",
        true,
        false,
        false,
        "unknown",
        true,
        true,
        0,
        0,
        "mount_failed_or_unformatted",
    };

    if (!mount_sd()) {
        return snapshot;
    }

    snapshot.state = "setup_required";
    snapshot.present = true;
    snapshot.mounted = true;
    snapshot.deskos = false;
    snapshot.fs = fat_label();
    snapshot.format_required = false;
    snapshot.format_supported = true;
    snapshot.note = "deskos_root_missing";
    fill_capacity(snapshot);

    if (SD.exists(DESKOS_ROOT) || SD.mkdir(DESKOS_ROOT)) {
        snapshot.state = "ready";
        snapshot.deskos = true;
        snapshot.note = "ready";
    } else {
        snapshot.state = "error";
        snapshot.note = "deskos_root_unavailable";
    }

    return snapshot;
}

bool format_card() {
    configure_sd_bus();
    SD.end(false);
    SDFS.setConfig(SDFSConfig(SD_CS_PIN, SD_SPI_HZ, SPI1));
    const bool formatted = SDFS.format();
    SDFS.end();
    if (!formatted) {
        return false;
    }
    return mount_sd() && (SD.exists(DESKOS_ROOT) || SD.mkdir(DESKOS_ROOT));
}

String bool_token(bool value) {
    return value ? "1" : "0";
}

void send_snapshot(Stream &out, const char *prefix, const SdSnapshot &snapshot) {
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
    line += " format_required=";
    line += bool_token(snapshot.format_required);
    line += " format_supported=";
    line += bool_token(snapshot.format_supported);
    line += " capacity_kb=";
    line += String(static_cast<unsigned long>(snapshot.capacity_kb));
    line += " free_kb=";
    line += String(static_cast<unsigned long>(snapshot.free_kb));
    line += " note=";
    line += snapshot.note;
    out.println(line);
}

void send_status() {
    send_snapshot(Serial1, STATUS_REPLY, current_status());
}

void send_format_result(const char *phrase) {
    if (strcmp(phrase, FORMAT_CONFIRMATION) != 0) {
        SdSnapshot refused = current_status();
        refused.state = "confirmation_required";
        refused.note = "confirmation_required";
        send_snapshot(Serial1, FORMAT_REPLY, refused);
        return;
    }

    if (format_card()) {
        SdSnapshot formatted = current_status();
        formatted.state = "ready";
        formatted.mounted = true;
        formatted.deskos = true;
        formatted.format_required = false;
        formatted.format_supported = true;
        formatted.note = "format_complete";
        if (formatted.free_kb == 0 && formatted.capacity_kb > 0) {
            formatted.free_kb = formatted.capacity_kb;
        }
        send_snapshot(Serial1, FORMAT_REPLY, formatted);
        return;
    }

    SdSnapshot failed = {
        "error",
        true,
        false,
        false,
        "unknown",
        true,
        true,
        0,
        0,
        "format_failed",
    };
    send_snapshot(Serial1, FORMAT_REPLY, failed);
}

void handle_line(char *line) {
    while (*line == ' ' || *line == '\t') {
        ++line;
    }
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }

    if (strcmp(line, STATUS_REQUEST) == 0) {
        send_status();
        return;
    }

    constexpr size_t format_prefix_len = sizeof(FORMAT_REQUEST) - 1;
    if (strncmp(line, FORMAT_REQUEST, format_prefix_len) == 0 &&
        line[format_prefix_len] == ' ') {
        send_format_result(line + format_prefix_len + 1);
        return;
    }

    SdSnapshot unsupported = {
        "error",
        false,
        false,
        false,
        "none",
        false,
        false,
        0,
        0,
        "unsupported_request",
    };
    send_snapshot(Serial1, STATUS_REPLY, unsupported);
}

void poll_bridge_uart() {
    while (Serial1.available() > 0) {
        const char c = static_cast<char>(Serial1.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            rx_line[rx_len] = '\0';
            if (rx_len > 0) {
                handle_line(rx_line);
            }
            rx_len = 0;
            continue;
        }
        if (rx_len + 1 < sizeof(rx_line)) {
            rx_line[rx_len++] = c;
        } else {
            rx_len = 0;
            SdSnapshot overflow = {
                "error",
                false,
                false,
                false,
                "none",
                false,
                false,
                0,
                0,
                "line_too_long",
            };
            send_snapshot(Serial1, STATUS_REPLY, overflow);
        }
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    Serial1.setRX(RP2040_ESP32_RX_PIN);
    Serial1.setTX(RP2040_ESP32_TX_PIN);
    Serial1.begin(ESP32_BRIDGE_BAUD);
    configure_sd_bus();
    Serial.println("DeskOS RP2040 SD bridge ready");
}

void loop() {
    poll_bridge_uart();
    delay(1);
}
