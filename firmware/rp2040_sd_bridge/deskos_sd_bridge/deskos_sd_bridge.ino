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
constexpr const char *FILE_REQUEST = "DESKOS_SD_FILE";
constexpr const char *FILE_REPLY = "DESKOS_SD_FILE";
constexpr uint32_t FILE_PROTOCOL_VERSION = 1;

constexpr uint8_t RP2040_ESP32_RX_PIN = 17;
constexpr uint8_t RP2040_ESP32_TX_PIN = 16;
constexpr uint32_t ESP32_BRIDGE_BAUD = 115200;

constexpr uint8_t SD_CS_PIN = 13;
constexpr uint8_t SD_SCK_PIN = 10;
constexpr uint8_t SD_MOSI_PIN = 11;
constexpr uint8_t SD_MISO_PIN = 12;
constexpr uint32_t SD_SPI_HZ = 1000000U;
constexpr const char *DESKOS_ROOT = "/deskos";

constexpr size_t FILE_LINE_MAX = 512;
constexpr size_t RX_LINE_MAX = FILE_LINE_MAX + 1;
constexpr size_t FILE_PATH_MAX = 96;
constexpr size_t FILE_CHUNK_MAX = 192;
constexpr size_t FILE_PATH64_MAX = 128;
constexpr size_t FILE_DATA64_MAX = 256;
constexpr size_t FILE_FULL_PATH_MAX = sizeof("/deskos/") + FILE_PATH_MAX;

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
bool drop_until_newline = false;

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
        if (token_len > key_len + 1U &&
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
        if (!SD.exists(tmp) && !SD.mkdir(tmp)) {
            *p = '/';
            return false;
        }
        *p = '/';
    }
    return true;
}

void send_snapshot(Stream &out, const char *prefix, const SdSnapshot &snapshot) {
    const bool file_ready = snapshot.present && snapshot.mounted && snapshot.deskos &&
                            !snapshot.format_required;
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
    line += " file_ops=";
    line += bool_token(file_ready);
    line += " file_line_max=";
    line += String(static_cast<unsigned long>(FILE_LINE_MAX));
    line += " file_chunk_max=";
    line += String(static_cast<unsigned long>(FILE_CHUNK_MAX));
    line += " path_max=";
    line += String(static_cast<unsigned long>(FILE_PATH_MAX));
    line += " atomic_rename=";
    line += bool_token(file_ready);
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
    Serial1.println(line);
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
    if (!SD.exists(full_path)) {
        out += "0 kind=none size=0 note=ok";
        Serial1.println(out);
        return;
    }

    File file = SD.open(full_path, "r");
    if (!file) {
        send_file_error(request_id, "stat", "open_failed");
        return;
    }
    out += "1 kind=";
    out += file.isDirectory() ? "dir" : "file";
    out += " size=";
    out += String(static_cast<unsigned long>(file.isDirectory() ? 0 : file.size()));
    out += " note=ok";
    file.close();
    Serial1.println(out);
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
        !parse_u32_token(line, "len", &requested_len) ||
        requested_len > FILE_CHUNK_MAX) {
        send_file_error(request_id, "read", "range");
        return;
    }
    if (!SD.exists(full_path)) {
        send_file_error(request_id, "read", "not_found");
        return;
    }

    File file = SD.open(full_path, "r");
    if (!file || file.isDirectory()) {
        send_file_error(request_id, "read", file && file.isDirectory() ? "is_dir" : "open_failed");
        if (file) {
            file.close();
        }
        return;
    }
    const uint32_t file_size = static_cast<uint32_t>(file.size());
    if (offset > file_size || !file.seek(offset)) {
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
    Serial1.println(out);
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
    if (!truncate && SD.exists(full_path)) {
        File existing = SD.open(full_path, "r");
        if (!existing || existing.isDirectory()) {
            send_file_error(request_id, op_name, existing && existing.isDirectory() ? "is_dir" : "open_failed");
            if (existing) {
                existing.close();
            }
            return;
        }
        current_size = static_cast<uint32_t>(existing.size());
        existing.close();
    }
    if (append_mode) {
        offset = current_size;
    } else if (offset != current_size) {
        send_file_error(request_id, op_name, "range");
        return;
    }

    File file = SD.open(full_path, truncate ? "w" : "a");
    if (!file) {
        send_file_error(request_id, op_name, "open_failed");
        return;
    }
    const size_t written = data_len == 0 ? 0 : file.write(data, data_len);
    file.flush();
    const uint32_t new_size = static_cast<uint32_t>(file.size());
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
    Serial1.println(out);
}

void handle_file_delete(uint32_t request_id, const char *line) {
    char relative[FILE_PATH_MAX + 1U];
    char full_path[FILE_FULL_PATH_MAX];
    if (!decode_path_token(line, "path", relative, sizeof(relative)) ||
        !make_full_path(relative, full_path, sizeof(full_path))) {
        send_file_error(request_id, "delete", "bad_path");
        return;
    }
    if (!SD.exists(full_path)) {
        send_file_error(request_id, "delete", "not_found");
        return;
    }
    if (!SD.remove(full_path)) {
        send_file_error(request_id, "delete", "delete_failed");
        return;
    }
    String out(FILE_REPLY);
    out += " v=1 id=";
    out += String(static_cast<unsigned long>(request_id));
    out += " ok=1 op=delete note=ok";
    Serial1.println(out);
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
    if (!SD.exists(source_path)) {
        send_file_error(request_id, "rename", "not_found");
        return;
    }
    if (SD.exists(target_path)) {
        if (!replace_target) {
            send_file_error(request_id, "rename", "exists");
            return;
        }
        if (!SD.remove(target_path)) {
            send_file_error(request_id, "rename", "delete_failed");
            return;
        }
    }
    if (!ensure_parent_dirs(target_path)) {
        send_file_error(request_id, "rename", "open_failed");
        return;
    }
    if (!SD.rename(source_path, target_path)) {
        send_file_error(request_id, "rename", "rename_failed");
        return;
    }
    String out(FILE_REPLY);
    out += " v=1 id=";
    out += String(static_cast<unsigned long>(request_id));
    out += " ok=1 op=rename note=ok";
    Serial1.println(out);
}

void handle_file_line(const char *line) {
    uint32_t request_id = 0;
    char op[16];
    if (!parse_file_header(line, &request_id, op, sizeof(op))) {
        send_file_error(0, "unknown", "bad_request");
        return;
    }

    SdSnapshot status = current_status();
    if (!status.present) {
        send_file_error(request_id, op, "no_card");
        return;
    }
    if (!status.mounted || !status.deskos || status.format_required) {
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
            if (!drop_until_newline) {
                rx_line[rx_len] = '\0';
                if (rx_len > 0) {
                    handle_line(rx_line);
                }
            }
            rx_len = 0;
            drop_until_newline = false;
            continue;
        }
        if (drop_until_newline) {
            continue;
        }
        if (rx_len + 1 < sizeof(rx_line)) {
            rx_line[rx_len++] = c;
        } else {
            rx_len = 0;
            drop_until_newline = true;
            send_file_error(0, "unknown", "line_too_long");
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
