#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

namespace {

constexpr uint8_t SD_CS_PIN = 13;
constexpr uint8_t SD_DET_PIN = 7;
constexpr uint8_t SD_SCK_PIN = 10;
constexpr uint8_t SD_MOSI_PIN = 11;
constexpr uint8_t SD_MISO_PIN = 12;
constexpr uint8_t SD_POWER_PIN = 18;
constexpr uint8_t SD_I2C_SDA_PIN = 20;
constexpr uint8_t SD_I2C_SCL_PIN = 21;
constexpr uint32_t SD_SPI_HZ = 1000000U;
constexpr uint32_t SD_PROBE_SPI_HZ = 400000U;
constexpr uint16_t SD_POWER_CYCLE_OFF_MS = 500;
constexpr uint16_t SD_POWER_SETTLE_MS = 1000;
constexpr uint16_t SD_ACMD41_TIMEOUT_MS = 1000;
constexpr uint16_t SD_READ_TOKEN_TIMEOUT_MS = 1000;
constexpr uint8_t MAX_CARD_GB = 32;

constexpr const char *SMOKE_DIR = "/d1l_smoke";
constexpr const char *SMOKE_TMP = "/d1l_smoke/smoke.tmp";
constexpr const char *SMOKE_FINAL = "/d1l_smoke/smoke.txt";
constexpr const char SMOKE_PAYLOAD[] = "D1L SD smoke";

struct SmokeResult {
    bool mounted;
    bool fat32;
    bool needs_fat32;
    uint8_t fat_type;
    bool mount_sd_2arg;
    bool mount_sd_2arg_spi_begin;
    bool mount_sd_3arg_spi_begin;
    const char *mount_mode;
    bool root_open;
    bool mkdir_ok;
    bool write_open;
    bool write_ok;
    bool read_open;
    bool read_ok;
    bool rename_ok;
    bool stat_ok;
    bool delete_ok;
};

struct DetectSample {
    const char *state;
    uint8_t pullup_level;
    uint8_t pulldown_level;
};

struct RawProbe {
    bool ran;
    bool present;
    bool cmd8_echo_ok;
    bool acmd41_ready;
    bool high_capacity;
    bool sector0_read;
    bool sector0_sig_ok;
    bool boot_sector_read;
    bool boot_sector_sig_ok;
    uint8_t error_code;
    uint8_t error_data;
    uint8_t sector0_response;
    uint8_t sector0_token;
    uint8_t boot_sector_response;
    uint8_t boot_sector_token;
    uint8_t partition_type;
    uint8_t miso_pullup_level;
    uint8_t miso_idle_level;
    uint8_t idle_rx_ff;
    uint8_t cmd0_ready_byte;
    uint8_t cmd8_ready_byte;
    uint8_t cmd0_response;
    uint8_t cmd8_response;
    uint8_t cmd8_echo[4];
    uint8_t cmd55_response;
    uint8_t cmd59_response;
    uint8_t acmd41_response;
    uint8_t acmd41_attempts;
    uint8_t ocr[4];
    uint32_t first_lba;
    const char *fs_hint;
};

void float_sd_lines_for_power_off();

void settle_seeed_sd_power(bool force_power_cycle) {
    pinMode(SD_POWER_PIN, OUTPUT);
    if (force_power_cycle) {
        float_sd_lines_for_power_off();
        digitalWrite(SD_POWER_PIN, LOW);
        delay(SD_POWER_CYCLE_OFF_MS);
    }
    digitalWrite(SD_POWER_PIN, HIGH);
    delay(SD_POWER_SETTLE_MS);
}

void configure_seeed_sd_bus() {
    settle_seeed_sd_power(false);

    Wire.setSDA(SD_I2C_SDA_PIN);
    Wire.setSCL(SD_I2C_SCL_PIN);
    Wire.begin();

    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
    SPI1.setCS(SD_CS_PIN);
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(SD_MISO_PIN, INPUT_PULLUP);
}

void configure_seeed_sd_bus_for_mount(bool force_power_cycle, bool explicit_spi_begin) {
    SD.end(false);
    SPI1.end();
    settle_seeed_sd_power(force_power_cycle);

    Wire.setSDA(SD_I2C_SDA_PIN);
    Wire.setSCL(SD_I2C_SCL_PIN);
    Wire.begin();

    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
    SPI1.setCS(SD_CS_PIN);
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(SD_MISO_PIN, INPUT_PULLUP);
    if (explicit_spi_begin) {
        SPI1.begin();
    }
}

DetectSample sample_detect() {
    pinMode(SD_DET_PIN, INPUT_PULLUP);
    delay(2);
    const uint8_t pullup = digitalRead(SD_DET_PIN) ? 1U : 0U;
    pinMode(SD_DET_PIN, INPUT_PULLDOWN);
    delay(2);
    const uint8_t pulldown = digitalRead(SD_DET_PIN) ? 1U : 0U;
    const char *state = "floating";
    if (pullup == 0U && pulldown == 0U) {
        state = "low";
    } else if (pullup == 1U && pulldown == 1U) {
        state = "high";
    }
    return {state, pullup, pulldown};
}

void float_sd_lines_for_power_off() {
    SPI1.end();
    const uint8_t pins[] = {SD_CS_PIN, SD_MOSI_PIN, SD_SCK_PIN, SD_MISO_PIN};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        pinMode(pins[i], INPUT);
    }
}

void configure_raw_probe_bus() {
    SD.end(false);
    pinMode(SD_POWER_PIN, OUTPUT);
    float_sd_lines_for_power_off();
    digitalWrite(SD_POWER_PIN, LOW);
    delay(SD_POWER_CYCLE_OFF_MS);
    digitalWrite(SD_POWER_PIN, HIGH);
    delay(SD_POWER_SETTLE_MS);

    Wire.setSDA(SD_I2C_SDA_PIN);
    Wire.setSCL(SD_I2C_SCL_PIN);
    Wire.begin();

    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
    SPI1.setCS(SD_CS_PIN);
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(SD_MOSI_PIN, OUTPUT);
    digitalWrite(SD_MOSI_PIN, HIGH);
    pinMode(SD_SCK_PIN, OUTPUT);
    digitalWrite(SD_SCK_PIN, LOW);
    pinMode(SD_MISO_PIN, INPUT_PULLUP);
    SPI1.begin();
}

uint8_t spi_transfer(uint8_t value) {
    return SPI1.transfer(value);
}

uint8_t wait_selected_ready(uint16_t timeout_ms) {
    const uint32_t start_ms = millis();
    uint8_t value = 0xFF;
    do {
        value = spi_transfer(0xFF);
        if (value == 0xFFU) {
            return value;
        }
    } while (millis() - start_ms < timeout_ms);
    return value;
}

uint8_t wait_sd_response() {
    uint8_t response = 0xFF;
    for (uint8_t i = 0; i < 16; ++i) {
        response = spi_transfer(0xFF);
        if ((response & 0x80U) == 0U) {
            return response;
        }
    }
    return response;
}

uint8_t sd_command(uint8_t command,
                   uint32_t argument,
                   uint8_t crc,
                   uint8_t *extra,
                   size_t extra_len,
                   uint8_t *ready_byte) {
    digitalWrite(SD_CS_PIN, LOW);
    if (ready_byte) {
        *ready_byte = wait_selected_ready(10);
    } else {
        (void)spi_transfer(0xFF);
    }
    (void)spi_transfer(0x40U | command);
    (void)spi_transfer(static_cast<uint8_t>(argument >> 24));
    (void)spi_transfer(static_cast<uint8_t>(argument >> 16));
    (void)spi_transfer(static_cast<uint8_t>(argument >> 8));
    (void)spi_transfer(static_cast<uint8_t>(argument));
    (void)spi_transfer(crc);
    const uint8_t response = wait_sd_response();
    for (size_t i = 0; i < extra_len; ++i) {
        extra[i] = spi_transfer(0xFF);
    }
    digitalWrite(SD_CS_PIN, HIGH);
    (void)spi_transfer(0xFF);
    return response;
}

uint8_t sd_command_crc(uint8_t command, uint32_t argument) {
    uint8_t crc = 0;
    const uint8_t bytes[] = {
        static_cast<uint8_t>(0x40U | command),
        static_cast<uint8_t>(argument >> 24),
        static_cast<uint8_t>(argument >> 16),
        static_cast<uint8_t>(argument >> 8),
        static_cast<uint8_t>(argument),
    };
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        uint8_t data = bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc <<= 1;
            if (((data ^ crc) & 0x80U) != 0U) {
                crc ^= 0x09U;
            }
            data <<= 1;
        }
    }
    return static_cast<uint8_t>((crc << 1) | 1U);
}

uint32_t read_le32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

const char *sector_fs_hint(const uint8_t *sector) {
    if (memcmp(sector + 3, "EXFAT   ", 8) == 0) {
        return "exfat";
    }
    if (memcmp(sector + 82, "FAT32   ", 8) == 0) {
        return "fat32";
    }
    if (memcmp(sector + 54, "FAT16   ", 8) == 0) {
        return "fat16";
    }
    if (memcmp(sector + 54, "FAT12   ", 8) == 0) {
        return "fat12";
    }
    return "unknown";
}

bool raw_read_sector(uint32_t lba, bool high_capacity, uint8_t *sector,
                     uint8_t *response_out, uint8_t *token_out) {
    const uint32_t address = high_capacity ? lba : (lba * 512UL);
    digitalWrite(SD_CS_PIN, LOW);
    (void)wait_selected_ready(10);
    (void)spi_transfer(0x40U | 17U);
    (void)spi_transfer(static_cast<uint8_t>(address >> 24));
    (void)spi_transfer(static_cast<uint8_t>(address >> 16));
    (void)spi_transfer(static_cast<uint8_t>(address >> 8));
    (void)spi_transfer(static_cast<uint8_t>(address));
    (void)spi_transfer(sd_command_crc(17, address));
    const uint8_t response = wait_sd_response();
    if (response_out) {
        *response_out = response;
    }
    if (response != 0U) {
        digitalWrite(SD_CS_PIN, HIGH);
        (void)spi_transfer(0xFF);
        return false;
    }

    uint8_t token = 0xFF;
    const uint32_t start_ms = millis();
    do {
        token = spi_transfer(0xFF);
        if (token == 0xFEU) {
            break;
        }
    } while (millis() - start_ms < SD_READ_TOKEN_TIMEOUT_MS);
    if (token_out) {
        *token_out = token;
    }
    if (token != 0xFEU) {
        digitalWrite(SD_CS_PIN, HIGH);
        (void)spi_transfer(0xFF);
        return false;
    }

    for (uint16_t i = 0; i < 512U; ++i) {
        sector[i] = spi_transfer(0xFF);
    }
    (void)spi_transfer(0xFF);
    (void)spi_transfer(0xFF);
    digitalWrite(SD_CS_PIN, HIGH);
    (void)spi_transfer(0xFF);
    return true;
}

void parse_raw_sector0(RawProbe &probe) {
    uint8_t sector[512] = {0};
    uint8_t response = 0xFF;
    uint8_t token = 0xFF;
    probe.sector0_read = raw_read_sector(0, probe.high_capacity, sector, &response, &token);
    probe.sector0_response = response;
    probe.sector0_token = token;
    if (!probe.sector0_read) {
        probe.error_code = 6U;
        probe.error_data = response != 0U ? response : token;
        return;
    }

    probe.sector0_sig_ok = sector[510] == 0x55U && sector[511] == 0xAAU;
    if (!probe.sector0_sig_ok) {
        probe.fs_hint = "no_55aa";
        return;
    }

    probe.partition_type = sector[0x1BE + 4];
    probe.first_lba = read_le32(sector + 0x1BE + 8);
    const bool has_mbr_partition = probe.partition_type != 0U && probe.first_lba != 0U;
    const uint8_t *boot_sector = sector;
    uint8_t boot[512] = {0};
    if (has_mbr_partition) {
        probe.boot_sector_read =
            raw_read_sector(probe.first_lba, probe.high_capacity, boot, &response, &token);
        probe.boot_sector_response = response;
        probe.boot_sector_token = token;
        if (probe.boot_sector_read) {
            boot_sector = boot;
        }
    } else {
        probe.boot_sector_read = true;
        probe.boot_sector_response = 0U;
        probe.boot_sector_token = 0xFEU;
    }
    if (!probe.boot_sector_read) {
        probe.error_code = 7U;
        probe.error_data = response != 0U ? response : token;
        return;
    }
    probe.boot_sector_sig_ok = boot_sector[510] == 0x55U && boot_sector[511] == 0xAAU;
    probe.fs_hint = probe.boot_sector_sig_ok ? sector_fs_hint(boot_sector) : "boot_no_55aa";
}

RawProbe run_raw_probe() {
    RawProbe probe = {};
    probe.ran = true;
    probe.fs_hint = "not_read";
    probe.sector0_response = 0xFF;
    probe.sector0_token = 0xFF;
    probe.boot_sector_response = 0xFF;
    probe.boot_sector_token = 0xFF;
    probe.cmd0_response = 0xFF;
    probe.cmd8_response = 0xFF;
    probe.cmd0_ready_byte = 0xFF;
    probe.cmd8_ready_byte = 0xFF;
    probe.cmd55_response = 0xFF;
    probe.cmd59_response = 0xFF;
    probe.acmd41_response = 0xFF;
    probe.idle_rx_ff = 0xFF;
    for (size_t i = 0; i < sizeof(probe.cmd8_echo); ++i) {
        probe.cmd8_echo[i] = 0;
        probe.ocr[i] = 0;
    }

    configure_raw_probe_bus();
    probe.miso_pullup_level = digitalRead(SD_MISO_PIN) ? 1U : 0U;
    SPI1.beginTransaction(SPISettings(SD_PROBE_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(SD_CS_PIN, HIGH);
    probe.idle_rx_ff = spi_transfer(0xFF);
    for (uint8_t i = 1; i < 10; ++i) {
        (void)spi_transfer(0xFF);
    }
    probe.miso_idle_level = digitalRead(SD_MISO_PIN) ? 1U : 0U;

    for (uint8_t attempt = 0; attempt < 8; ++attempt) {
        probe.cmd0_response = sd_command(0, 0, 0x95, nullptr, 0,
                                         &probe.cmd0_ready_byte);
        if (probe.cmd0_response == 0x01U) {
            break;
        }
        delay(10);
    }
    if (probe.cmd0_response != 0x01U) {
        probe.error_code = probe.cmd0_response == 0xFFU ? 1U : 3U;
        probe.error_data = probe.cmd0_response;
        SPI1.endTransaction();
        return probe;
    }

    probe.cmd8_response = sd_command(8, 0x1AA, 0x87, probe.cmd8_echo,
                                     sizeof(probe.cmd8_echo),
                                     &probe.cmd8_ready_byte);
    const bool sd_v2 = (probe.cmd8_response & 0x04U) == 0U;
    probe.cmd8_echo_ok = probe.cmd8_echo[2] == 0x01U && probe.cmd8_echo[3] == 0xAAU;
    if (sd_v2 && !probe.cmd8_echo_ok) {
        probe.error_code = 4U;
        probe.error_data = probe.cmd8_echo[3];
        SPI1.endTransaction();
        return probe;
    }

    const uint32_t start_ms = millis();
    do {
        probe.cmd55_response = sd_command(55, 0, 0x65, nullptr, 0, nullptr);
        probe.acmd41_response = sd_command(41, sd_v2 ? 0x40000000UL : 0, 0x77,
                                           nullptr, 0, nullptr);
        ++probe.acmd41_attempts;
        if (probe.acmd41_response == 0U) {
            probe.acmd41_ready = true;
            probe.present = true;
            break;
        }
        delay(10);
    } while (millis() - start_ms < SD_ACMD41_TIMEOUT_MS);

    if (!probe.acmd41_ready) {
        probe.error_code = 5U;
        probe.error_data = probe.acmd41_response;
        SPI1.endTransaction();
        return probe;
    }

    probe.cmd59_response = sd_command(59, 0, sd_command_crc(59, 0),
                                      nullptr, 0, nullptr);
    (void)sd_command(58, 0, 0xFD, probe.ocr, sizeof(probe.ocr), nullptr);
    probe.high_capacity = (probe.ocr[0] & 0x40U) != 0U;
    parse_raw_sector0(probe);
    if (!probe.sector0_read || !probe.boot_sector_read) {
        SPI1.endTransaction();
        return probe;
    }
    SPI1.endTransaction();
    probe.error_code = 0U;
    probe.error_data = probe.ocr[0];
    return probe;
}

bool root_directory_opens() {
    File root = SD.open("/");
    const bool ok = root && root.isDirectory();
    if (root) {
        root.close();
    }
    return ok;
}

bool write_smoke_file() {
    File f = SD.open(SMOKE_TMP, FILE_WRITE);
    if (!f) {
        return false;
    }
    const size_t written = f.write(reinterpret_cast<const uint8_t *>(SMOKE_PAYLOAD),
                                   sizeof(SMOKE_PAYLOAD) - 1U);
    f.flush();
    f.close();
    return written == sizeof(SMOKE_PAYLOAD) - 1U;
}

bool read_smoke_file(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        return false;
    }
    char buffer[sizeof(SMOKE_PAYLOAD)] = {0};
    const int count = f.read(reinterpret_cast<uint8_t *>(buffer), sizeof(buffer) - 1U);
    f.close();
    return count == static_cast<int>(sizeof(SMOKE_PAYLOAD) - 1U) &&
           strcmp(buffer, SMOKE_PAYLOAD) == 0;
}

SmokeResult run_smoke() {
    SmokeResult result = {};
    result.mount_mode = "none";

    configure_seeed_sd_bus_for_mount(true, false);
    result.mount_sd_2arg = SD.begin(SD_CS_PIN, SPI1);
    result.mounted = result.mount_sd_2arg;
    if (result.mounted) {
        result.mount_mode = "sd_begin_cs_spi1";
    }

    if (!result.mounted) {
        configure_seeed_sd_bus_for_mount(true, true);
        result.mount_sd_2arg_spi_begin = SD.begin(SD_CS_PIN, SPI1);
        result.mounted = result.mount_sd_2arg_spi_begin;
        if (result.mounted) {
            result.mount_mode = "spi1_begin_then_sd_begin_cs_spi1";
        }
    }

    if (!result.mounted) {
        configure_seeed_sd_bus_for_mount(true, true);
        result.mount_sd_3arg_spi_begin = SD.begin(SD_CS_PIN, SD_SPI_HZ, SPI1);
        result.mounted = result.mount_sd_3arg_spi_begin;
        if (result.mounted) {
            result.mount_mode = "spi1_begin_then_sd_begin_cs_hz_spi1";
        }
    }

    if (!result.mounted) {
        return result;
    }
    result.fat_type = SD.fatType();
    result.fat32 = result.fat_type == 32U;
    result.needs_fat32 = !result.fat32;
    if (!result.fat32) {
        return result;
    }

    result.root_open = root_directory_opens();
    result.mkdir_ok = SD.exists(SMOKE_DIR) || SD.mkdir(SMOKE_DIR);
    (void)SD.remove(SMOKE_TMP);
    (void)SD.remove(SMOKE_FINAL);

    result.write_open = write_smoke_file();
    result.write_ok = result.write_open;
    result.read_open = SD.exists(SMOKE_TMP);
    result.read_ok = result.read_open && read_smoke_file(SMOKE_TMP);
    result.rename_ok = result.read_ok && SD.rename(SMOKE_TMP, SMOKE_FINAL);
    result.stat_ok = result.rename_ok && SD.exists(SMOKE_FINAL) &&
                     read_smoke_file(SMOKE_FINAL);
    result.delete_ok = result.stat_ok && SD.remove(SMOKE_FINAL) &&
                       !SD.exists(SMOKE_FINAL);
    return result;
}

void print_bool_field(const char *name, bool value, bool comma = true) {
    Serial.print('"');
    Serial.print(name);
    Serial.print("\":");
    Serial.print(value ? "true" : "false");
    if (comma) {
        Serial.print(',');
    }
}

void print_string_field(const char *name, const char *value, bool comma = true) {
    Serial.print('"');
    Serial.print(name);
    Serial.print("\":\"");
    Serial.print(value);
    Serial.print('"');
    if (comma) {
        Serial.print(',');
    }
}

void print_u8_field(const char *name, uint8_t value, bool comma = true) {
    Serial.print('"');
    Serial.print(name);
    Serial.print("\":");
    Serial.print(static_cast<unsigned int>(value));
    if (comma) {
        Serial.print(',');
    }
}

void print_result(const SmokeResult &result, const RawProbe &probe,
                  const DetectSample &detect) {
    const bool ok = result.mounted && result.fat32 && result.root_open && result.mkdir_ok &&
                    result.write_ok && result.read_ok && result.rename_ok &&
                    result.stat_ok && result.delete_ok;
    Serial.print("{\"test\":\"seeed_official_sd_smoke\",");
    print_bool_field("ok", ok);
    print_bool_field("mount", result.mounted);
    print_bool_field("fat32", result.fat32);
    print_bool_field("needs_fat32", result.needs_fat32);
    print_bool_field("root_open", result.root_open);
    print_bool_field("mkdir", result.mkdir_ok);
    print_bool_field("write_open", result.write_open);
    print_bool_field("write", result.write_ok);
    print_bool_field("read_open", result.read_open);
    print_bool_field("read", result.read_ok);
    print_bool_field("rename", result.rename_ok);
    print_bool_field("stat", result.stat_ok);
    print_bool_field("delete", result.delete_ok);
    print_bool_field("public_rf_tx", false);
    print_bool_field("formats_sd", false);
    print_bool_field("will_format", false);
    print_bool_field("format_performed", false);
    print_bool_field("detect_used_for_ok", false);
    print_bool_field("power_measured", false);
    print_bool_field("mount_sd_2arg", result.mount_sd_2arg);
    print_bool_field("mount_sd_2arg_spi_begin", result.mount_sd_2arg_spi_begin);
    print_bool_field("mount_sd_3arg_spi_begin", result.mount_sd_3arg_spi_begin);
    print_bool_field("diag_ran", probe.ran);
    print_bool_field("raw_present", probe.present);
    print_bool_field("raw_cmd8_echo_ok", probe.cmd8_echo_ok);
    print_bool_field("raw_acmd41_ready", probe.acmd41_ready);
    print_bool_field("raw_high_capacity", probe.high_capacity);
    print_bool_field("raw_sector0_read", probe.sector0_read);
    print_bool_field("raw_sector0_sig_ok", probe.sector0_sig_ok);
    print_bool_field("raw_boot_sector_read", probe.boot_sector_read);
    print_bool_field("raw_boot_sector_sig_ok", probe.boot_sector_sig_ok);
    Serial.print("\"cs\":13,\"sck\":10,\"mosi\":11,\"miso\":12,");
    Serial.print("\"power\":18,\"detect_pin\":7,\"i2c_sda\":20,\"i2c_scl\":21,");
    Serial.print("\"hz\":");
    Serial.print(SD_SPI_HZ);
    Serial.print(",\"fat_type\":");
    Serial.print(result.fat_type);
    Serial.print(',');
    print_string_field("detect", detect.state);
    print_u8_field("detect_pullup", detect.pullup_level);
    print_u8_field("detect_pulldown", detect.pulldown_level);
    print_u8_field("raw_err", probe.error_code);
    print_u8_field("raw_data", probe.error_data);
    print_u8_field("raw_miso_pullup", probe.miso_pullup_level);
    print_u8_field("raw_miso_idle", probe.miso_idle_level);
    print_u8_field("raw_idle_ff", probe.idle_rx_ff);
    print_u8_field("raw_cmd0_ready", probe.cmd0_ready_byte);
    print_u8_field("raw_cmd0", probe.cmd0_response);
    print_u8_field("raw_cmd8_ready", probe.cmd8_ready_byte);
    print_u8_field("raw_cmd8", probe.cmd8_response);
    print_u8_field("raw_r70", probe.cmd8_echo[0]);
    print_u8_field("raw_r71", probe.cmd8_echo[1]);
    print_u8_field("raw_r72", probe.cmd8_echo[2]);
    print_u8_field("raw_r73", probe.cmd8_echo[3]);
    print_u8_field("raw_cmd55", probe.cmd55_response);
    print_u8_field("raw_cmd59", probe.cmd59_response);
    print_u8_field("raw_acmd41", probe.acmd41_response);
    print_u8_field("raw_acmd41_attempts", probe.acmd41_attempts);
    print_u8_field("raw_sector0_response", probe.sector0_response);
    print_u8_field("raw_sector0_token", probe.sector0_token);
    print_u8_field("raw_boot_sector_response", probe.boot_sector_response);
    print_u8_field("raw_boot_sector_token", probe.boot_sector_token);
    print_u8_field("raw_partition_type", probe.partition_type);
    Serial.print("\"raw_first_lba\":");
    Serial.print(probe.first_lba);
    Serial.print(',');
    print_u8_field("raw_ocr0", probe.ocr[0]);
    print_u8_field("raw_ocr1", probe.ocr[1]);
    print_u8_field("raw_ocr2", probe.ocr[2]);
    print_u8_field("raw_ocr3", probe.ocr[3]);
    print_string_field("mount_mode", result.mount_mode);
    print_string_field("raw_fs_hint", probe.fs_hint);
    print_string_field("power_state", "gpio18_commanded_high_not_measured");
    Serial.print("\"format\":\"non_destructive\",\"max_card_gb\":");
    Serial.print(MAX_CARD_GB);
    Serial.println("}");
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);
    SmokeResult result = run_smoke();
    RawProbe probe = run_raw_probe();
    DetectSample detect = sample_detect();
    print_result(result, probe, detect);
}

void loop() {}
