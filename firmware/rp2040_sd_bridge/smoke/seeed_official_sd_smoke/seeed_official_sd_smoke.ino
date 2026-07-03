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
    uint8_t error_code;
    uint8_t error_data;
    uint8_t miso_pullup_level;
    uint8_t miso_idle_level;
    uint8_t idle_rx_ff;
    uint8_t cmd0_ready_byte;
    uint8_t cmd8_ready_byte;
    uint8_t cmd0_response;
    uint8_t cmd8_response;
    uint8_t cmd8_echo[4];
    uint8_t cmd55_response;
    uint8_t acmd41_response;
    uint8_t acmd41_attempts;
    uint8_t ocr[4];
};

void configure_seeed_sd_bus() {
    pinMode(SD_POWER_PIN, OUTPUT);
    digitalWrite(SD_POWER_PIN, HIGH);
    delay(1000);

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

RawProbe run_raw_probe() {
    RawProbe probe = {};
    probe.ran = true;
    probe.cmd0_response = 0xFF;
    probe.cmd8_response = 0xFF;
    probe.cmd0_ready_byte = 0xFF;
    probe.cmd8_ready_byte = 0xFF;
    probe.cmd55_response = 0xFF;
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

    (void)sd_command(58, 0, 0xFD, probe.ocr, sizeof(probe.ocr), nullptr);
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
    configure_seeed_sd_bus();
    result.mounted = SD.begin(SD_CS_PIN, SD_SPI_HZ, SPI1);
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
    print_bool_field("diag_ran", probe.ran);
    print_bool_field("raw_present", probe.present);
    print_bool_field("raw_cmd8_echo_ok", probe.cmd8_echo_ok);
    print_bool_field("raw_acmd41_ready", probe.acmd41_ready);
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
    print_u8_field("raw_acmd41", probe.acmd41_response);
    print_u8_field("raw_acmd41_attempts", probe.acmd41_attempts);
    print_u8_field("raw_ocr0", probe.ocr[0]);
    print_u8_field("raw_ocr1", probe.ocr[1]);
    print_u8_field("raw_ocr2", probe.ocr[2]);
    print_u8_field("raw_ocr3", probe.ocr[3]);
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
