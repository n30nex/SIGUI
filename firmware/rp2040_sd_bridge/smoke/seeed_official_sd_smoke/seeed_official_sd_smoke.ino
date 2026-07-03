#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

namespace {

constexpr uint8_t SD_CS_PIN = 13;
constexpr uint8_t SD_SCK_PIN = 10;
constexpr uint8_t SD_MOSI_PIN = 11;
constexpr uint8_t SD_MISO_PIN = 12;
constexpr uint8_t SD_POWER_PIN = 18;
constexpr uint8_t SD_I2C_SDA_PIN = 20;
constexpr uint8_t SD_I2C_SCL_PIN = 21;
constexpr uint32_t SD_SPI_HZ = 1000000U;
constexpr uint8_t MAX_CARD_GB = 32;

constexpr const char *SMOKE_DIR = "/d1l_smoke";
constexpr const char *SMOKE_TMP = "/d1l_smoke/smoke.tmp";
constexpr const char *SMOKE_FINAL = "/d1l_smoke/smoke.txt";
constexpr const char SMOKE_PAYLOAD[] = "D1L SD smoke";

struct SmokeResult {
    bool mounted;
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

void print_result(const SmokeResult &result) {
    const bool ok = result.mounted && result.root_open && result.mkdir_ok &&
                    result.write_ok && result.read_ok && result.rename_ok &&
                    result.stat_ok && result.delete_ok;
    Serial.print("{\"test\":\"seeed_official_sd_smoke\",");
    print_bool_field("ok", ok);
    print_bool_field("mount", result.mounted);
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
    Serial.print("\"cs\":13,\"sck\":10,\"mosi\":11,\"miso\":12,");
    Serial.print("\"power\":18,\"i2c_sda\":20,\"i2c_scl\":21,");
    Serial.print("\"hz\":");
    Serial.print(SD_SPI_HZ);
    Serial.print(",\"fat_type\":");
    Serial.print(result.mounted ? SD.fatType() : 0);
    Serial.print(",\"format\":\"non_destructive\",\"max_card_gb\":");
    Serial.print(MAX_CARD_GB);
    Serial.println("}");
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);
    print_result(run_smoke());
}

void loop() {}
