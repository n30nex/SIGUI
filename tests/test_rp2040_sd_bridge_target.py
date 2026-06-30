from pathlib import Path

from tools.rp2040_sd_protocol import (
    FORMAT_CONFIRMATION,
    FORMAT_REQUEST,
    STATUS_FIELDS,
    STATUS_REQUEST,
)


ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "firmware" / "rp2040_sd_bridge" / "deskos_sd_bridge" / "deskos_sd_bridge.ino"
README = ROOT / "firmware" / "rp2040_sd_bridge" / "README.md"


def test_rp2040_bridge_target_has_d1l_pin_and_protocol_contract():
    sketch = SKETCH.read_text(encoding="utf-8")
    readme = README.read_text(encoding="utf-8")

    assert '#include <SD.h>' in sketch
    assert '#include <SDFS.h>' in sketch
    assert '#include <SPI.h>' in sketch
    assert "Serial1.setRX(RP2040_ESP32_RX_PIN)" in sketch
    assert "Serial1.setTX(RP2040_ESP32_TX_PIN)" in sketch
    assert "constexpr uint8_t RP2040_ESP32_RX_PIN = 17;" in sketch
    assert "constexpr uint8_t RP2040_ESP32_TX_PIN = 16;" in sketch
    assert "constexpr uint32_t ESP32_BRIDGE_BAUD = 115200;" in sketch
    assert "constexpr uint8_t SD_CS_PIN = 13;" in sketch
    assert "constexpr uint8_t SD_SCK_PIN = 10;" in sketch
    assert "constexpr uint8_t SD_MOSI_PIN = 11;" in sketch
    assert "constexpr uint8_t SD_MISO_PIN = 12;" in sketch
    assert "SPI1.setSCK(SD_SCK_PIN)" in sketch
    assert "SPI1.setTX(SD_MOSI_PIN)" in sketch
    assert "SPI1.setRX(SD_MISO_PIN)" in sketch
    assert "SD.begin(SD_CS_PIN, SD_SPI_HZ, SPI1)" in sketch
    assert "SDFS.format()" in sketch
    assert "/deskos" in sketch

    for token in [STATUS_REQUEST, FORMAT_REQUEST, FORMAT_CONFIRMATION]:
        assert token in sketch
        assert token in readme


def test_rp2040_bridge_target_emits_complete_status_tokens():
    sketch = SKETCH.read_text(encoding="utf-8")

    for field in STATUS_FIELDS:
        assert f"{field}=" in sketch
    for state in [
        "ready",
        "setup_required",
        "confirmation_required",
        "error",
    ]:
        assert state in sketch
    for note in [
        "ready",
        "deskos_root_missing",
        "mount_failed_or_unformatted",
        "format_complete",
        "confirmation_required",
        "format_failed",
        "unsupported_request",
        "line_too_long",
    ]:
        assert note in sketch
        assert " " not in note


def test_rp2040_docs_mark_ci_build_and_store_migration_pending():
    readme = README.read_text(encoding="utf-8")

    assert "GitHub Actions" in readme
    assert "rp2040:rp2040:seeed_indicator_rp2040" in readme
    assert "Do not use the Windows host for firmware compilation" in readme
    assert "Retained DeskOS stores still remain on ESP32 onboard NVS" in readme
