#include <Arduino.h>

#include <SD.h>
#include <SPI.h>
#include <Wire.h>

namespace {

constexpr uint8_t SD_CS_PIN = 13;
constexpr uint8_t SD_SCK_PIN = 10;
constexpr uint8_t SD_MOSI_PIN = 11;
constexpr uint8_t SD_MISO_PIN = 12;
constexpr uint8_t SD_POWER_PIN = 18;
constexpr uint8_t SD_I2C_SDA_PIN = 20;
constexpr uint8_t SD_I2C_SCL_PIN = 21;
constexpr uint32_t SD_SPI_HZ = 1000000U;

char rx_line[48];
size_t rx_len = 0;
bool last_ok = false;

void configure_seeed_sd_bus() {
    pinMode(SD_POWER_PIN, OUTPUT);
    digitalWrite(SD_POWER_PIN, HIGH);
    Wire.setSDA(SD_I2C_SDA_PIN);
    Wire.setSCL(SD_I2C_SCL_PIN);
    Wire.begin();
    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
}

bool probe_sd() {
    configure_seeed_sd_bus();
    last_ok = SD.begin(SD_CS_PIN, SD_SPI_HZ, SPI1);
    return last_ok;
}

void send_status(const char *kind) {
    Serial.print("SEEED_SD_SMOKE ");
    Serial.print(kind);
    Serial.print(" ok=");
    Serial.print(last_ok ? 1 : 0);
    Serial.print(" pins=cs13-sck10-mosi11-miso12-pwr18 hz=");
    Serial.print(SD_SPI_HZ);
    Serial.print(" public_rf_tx=0 formats_sd=0");
    Serial.println();
}

void handle_line(char *line) {
    while (*line == ' ' || *line == '\t') {
        ++line;
    }
    if (strcmp(line, "PING") == 0) {
        send_status("ping");
    } else if (strcmp(line, "SD") == 0) {
        probe_sd();
        send_status("sd");
    } else {
        send_status("unknown");
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    probe_sd();
    send_status("boot");
}

void loop() {
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            rx_line[rx_len] = '\0';
            handle_line(rx_line);
            rx_len = 0;
        } else if (rx_len + 1 < sizeof(rx_line)) {
            rx_line[rx_len++] = ch;
        } else {
            rx_len = 0;
            send_status("overflow");
        }
    }
    delay(10);
}
