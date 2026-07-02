from pathlib import Path

from tools.rp2040_sd_protocol import (
    FILE_CAPABILITY_FIELDS,
    FILE_LINE_MAX,
    FILE_REQUEST,
    MAX_FILE_CHUNK_BYTES,
    MAX_FILE_PATH_CHARS,
    DIAG_REQUEST,
    MOUNT_REQUEST,
    PING_REQUEST,
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
    assert '#include <SdFat.h>' in sketch
    assert '#include <SDFS.h>' in sketch
    assert '#include <SPI.h>' in sketch
    assert '#include <hardware/gpio.h>' in sketch
    assert "USE_SD_CRC" not in sketch
    assert "USE_SPI_ARRAY_TRANSFER" not in sketch
    assert "requires byte-wise SdFat SPI transfers" not in sketch
    assert "Serial1.setRX(RP2040_ESP32_RX_PIN)" in sketch
    assert "Serial1.setTX(RP2040_ESP32_TX_PIN)" in sketch
    assert "constexpr uint8_t RP2040_ESP32_RX_PIN = 17;" in sketch
    assert "constexpr uint8_t RP2040_ESP32_TX_PIN = 16;" in sketch
    assert "constexpr uint32_t ESP32_BRIDGE_BAUD = 921600;" in sketch
    assert "constexpr uint32_t SD_PROBE_SPI_HZ = 400000U;" in sketch
    assert "constexpr uint8_t SD_CS_PIN = 13;" in sketch
    assert "constexpr uint8_t SD_SCK_PIN = 10;" in sketch
    assert "constexpr uint8_t SD_MOSI_PIN = 11;" in sketch
    assert "constexpr uint8_t SD_MISO_PIN = 12;" in sketch
    assert "constexpr uint8_t SD_POWER_PIN = 18;" in sketch
    assert "constexpr uint16_t SD_POWER_CYCLE_OFF_MS = 500;" in sketch
    assert "constexpr uint16_t SD_POWER_SETTLE_MS = 1000;" in sketch
    assert "constexpr uint16_t SD_SELECTED_READY_WAIT_MS = 500;" in sketch
    assert "digitalWrite(SD_POWER_PIN, power_high ? HIGH : LOW)" in sketch
    assert "digitalWrite(SD_POWER_PIN, power_high ? LOW : HIGH)" in sketch
    assert "bool s_sd_power_high = true;" in sketch
    assert "pinMode(SD_CS_PIN, OUTPUT)" in sketch
    assert "digitalWrite(SD_CS_PIN, HIGH)" in sketch
    assert "delay(SD_POWER_CYCLE_OFF_MS)" in sketch
    assert "delay(SD_POWER_SETTLE_MS)" in sketch
    assert "sd_wait_ready(SD_SELECTED_READY_WAIT_MS)" in sketch
    assert "pinMode(SD_MISO_PIN, INPUT_PULLUP)" in sketch
    assert "gpio_pull_up(SD_MISO_PIN)" in sketch
    assert "gpio_set_input_enabled(SD_MISO_PIN, true)" in sketch
    assert "sample_sd_miso_level()" in sketch
    assert sketch.count("apply_sd_miso_pullup()") >= 4
    assert "SPI1.setSCK(SD_SCK_PIN)" in sketch
    assert "SPI1.end()" in sketch
    assert "SPI1.setMOSI(SD_MOSI_PIN)" in sketch
    assert "SPI1.setMISO(SD_MISO_PIN)" in sketch
    assert "SPI1.setCS(SD_CS_PIN)" not in sketch
    assert "s_sd_pin_cs_ok = true;" in sketch
    assert "bool s_sd_mounted = false;" in sketch
    assert "SD.begin(SD_CS_PIN, SPI1)" in sketch
    assert "SD.begin(SD_CS_PIN, SD_SPI_HZ, SPI1)" not in sketch
    assert "SD.end(false)" in sketch
    assert "mount_sd_seeed_sample_path" in sketch
    assert "configure_seeed_sd_bus(power_high, force_power_cycle)" in sketch
    assert "configure_seeed_sd_bus(s_sd_power_high)" in sketch
    assert "SPI1.begin()" in sketch[sketch.index("void configure_seeed_sd_bus"):sketch.index("void clock_sd_idle_bytes")]
    assert "begin_sd_filesystem(false)" in sketch
    assert "SDFS.info(info)" in sketch
    assert "prepare_sd_card_init(power_high, force_power_cycle)" in sketch
    assert "clock_sd_idle_bytes" in sketch
    assert "configure_sd_bus(power_high, force_power_cycle)" in sketch
    assert "mount_sd_with_probe_config" in sketch
    assert "if (mount_sd_seeed_sample_path(true, false))" in sketch
    assert "mounted_snapshot_from_current_config" in sketch
    assert "last_present_probe" in sketch
    assert "for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)" in sketch
    assert "if (mount_sd_with_probe_config(probes[i]))" in sketch
    assert "SPI1.begin()" in sketch
    assert "delay(50)" in sketch
    assert "SdSpiConfig(SD_CS_PIN, options, SD_SPI_HZ, &SPI1)" in sketch
    assert "SdCardFactory card_factory" in sketch
    assert "manual_probe_card(DEDICATED_SPI, true, false)" in sketch
    assert "manual_probe_card(SHARED_SPI, true, false)" in sketch
    assert "manual_probe_card(DEDICATED_SPI, true)" in sketch
    assert "manual_probe_card(SHARED_SPI, true)" in sketch
    assert "manual_probe_card(DEDICATED_SPI, false)" in sketch
    assert "manual_probe_card(SHARED_SPI, false)" in sketch
    assert 'SdSnapshot snapshot = pending_snapshot("filesystem_mounting")' in sketch
    assert "publish_mount_progress(snapshot)" in sketch
    assert "if (s_worker_busy)" in sketch
    assert 'snapshot = pending_snapshot("probing_card")' in sketch
    assert sketch.index('SdSnapshot snapshot = pending_snapshot("filesystem_mounting")') < sketch.index("if (mount_sd_seeed_sample_path(true, false))")
    assert sketch.index("if (mount_sd_seeed_sample_path(true, false))") < sketch.index('snapshot = pending_snapshot("probing_card")')
    assert sketch.index('snapshot = pending_snapshot("probing_card")') < sketch.index("manual_probe_card(DEDICATED_SPI, true, false)")
    assert sketch.index("manual_probe_card(SHARED_SPI, true)") < sketch.index("if (mount_sd_with_probe_config(probes[i]))")
    assert "s_last_mount_error = card->errorCode()" in sketch
    assert "s_last_mount_data = card->errorData()" in sketch
    assert '" mount_err="' in sketch
    assert '" mount_data="' in sketch
    assert "sd_command(0, 0, 0x95" in sketch
    assert "const bool cmd0_idle = cmd0 == 0x01U" in sketch
    assert "const bool cmd0_ready = cmd0 == 0x00U" in sketch
    assert "if (!cmd0_idle && !cmd0_ready)" in sketch
    assert "probe.error_code = 3" in sketch
    assert "cmd8_echo_ok" in sketch
    assert "probe.error_code = 4" in sketch
    assert "probe.cmd0_response = cmd0" in sketch
    assert "probe.cmd8_response = cmd8" in sketch
    assert "&probe.cmd0_ready_byte, true" in sketch
    assert "&probe.cmd8_ready_byte, true" in sketch
    assert "ignore_leading_zero && response == 0x00U" in sketch
    assert "for (uint8_t i = 0; i < 64; ++i)" in sketch
    assert "probe.cmd8_echo[i] = cmd8_extra[i]" in sketch
    assert "{0, 0, 1, 170}" in sketch
    assert '"_c0r="' in sketch
    assert '"_c8r="' in sketch
    assert '"_c0="' in sketch
    assert '"_c8="' in sketch
    assert '"_r7"' in sketch
    assert '"_miso_pull="' in sketch
    assert '"_miso_spi="' in sketch
    assert '"_miso_idle="' in sketch
    assert '"_idle_ff="' in sketch
    assert '" pin_sck="' in sketch
    assert '" pin_mosi="' in sketch
    assert '" pin_miso="' in sketch
    assert '" pin_cs="' in sketch
    assert "probe.miso_pullup_level = sample_sd_miso_level()" in sketch
    assert "probe.miso_spi_level = sample_sd_miso_level()" in sketch
    assert "probe.miso_idle_level = sample_sd_miso_level()" in sketch
    assert "probe.idle_rx_ff = sd_spi_transfer(0xFF)" in sketch
    assert "sd_command(8, 0x1AA, 0x87" in sketch
    assert "sd_command(41" in sketch
    assert "delete card" in sketch
    assert "SDFS.format()" not in sketch
    assert "SD.format()" not in sketch
    old_request = "DESKOS_SD_" + "FORMAT"
    old_confirmation = "FORMAT-" + "DESKOS-SD"
    assert old_request not in sketch
    assert old_confirmation not in sketch
    assert "FatFormatter" not in sketch
    assert "format_card" not in sketch
    assert "send_format_result" not in sketch
    assert "/deskos" in sketch
    assert "/deskos/manifest.json" in sketch
    assert "/deskos/map/manifest.json" in sketch
    assert "MeshCore DeskOS D1L SD" in sketch
    assert "map_cache" in sketch
    assert "prepare_deskos_structure" in sketch
    assert '"none",\n        false,\n        0,\n        0,' in sketch
    assert '"none",\n        false,\n        false,\n        0,\n        0,' not in sketch

    for token in [STATUS_REQUEST, MOUNT_REQUEST, PING_REQUEST, DIAG_REQUEST, FILE_REQUEST]:
        assert token in sketch
        assert token in readme


def test_rp2040_bridge_target_emits_complete_status_tokens():
    sketch = SKETCH.read_text(encoding="utf-8")

    for field in STATUS_FIELDS:
        assert f"{field}=" in sketch
    for field in FILE_CAPABILITY_FIELDS:
        assert f"{field}=" in sketch
    for state in [
        "no_card",
        "ready",
        "not_fat32_or_unmountable",
        "creating_deskos_files",
        "deskos_manifest_invalid",
        "mount_required",
        "mount_pending",
        "error",
    ]:
        assert state in sketch
    for note in [
        "no_card",
        "ready",
        "mount_not_checked",
        "filesystem_mounting",
        "probing_card",
        "card_detected_mounting",
        "deskos_root_missing",
        "deskos_manifest_invalid",
        "deskos_map_manifest_invalid",
        "structure_created",
        "needs_fat32_on_computer",
        "unsupported_request",
        "line_too_long",
        "bad_path",
        "crc_mismatch",
        "not_ready",
    ]:
        assert note in sketch
        assert " " not in note


def test_rp2040_bridge_target_implements_generic_file_protocol_safely():
    sketch = SKETCH.read_text(encoding="utf-8")

    assert f"constexpr size_t FILE_LINE_MAX = {FILE_LINE_MAX};" in sketch
    assert "constexpr size_t RX_LINE_MAX = FILE_LINE_MAX + 1;" in sketch
    assert f"constexpr size_t FILE_PATH_MAX = {MAX_FILE_PATH_CHARS};" in sketch
    assert f"constexpr size_t FILE_CHUNK_MAX = {MAX_FILE_CHUNK_BYTES};" in sketch
    assert "drop_until_newline = true" in sketch
    assert "if (rx.drop_until_newline)" in sketch
    assert "poll_stream(Serial1, Serial1, bridge_rx)" in sketch
    assert "poll_stream(Serial, Serial, usb_rx)" in sketch
    assert "void setup1()" in sketch
    assert "void loop1()" in sketch
    assert "sd_worker_loop_once()" in sketch
    assert "SD_WORKER_MOUNT" in sketch
    assert "SD_WORKER_DIAG" in sketch
    assert "Stream *reply_stream = &Serial1;" in sketch
    assert "send_ping" in sketch
    assert "send_mount_status" in sketch
    assert "sd_touch=0" in sketch
    assert "send_snapshot(*reply_stream" in sketch
    assert "validate_relative_path" in sketch
    assert "decode_base64url" in sketch
    assert "encode_base64url" in sketch
    assert "crc32_bytes" in sketch
    assert "crc_mismatch" in sketch
    assert '"too_large"' in sketch
    assert "token_len >= key_len + 1U" in sketch
    assert "handle_file_stat" in sketch
    assert "handle_file_read" in sketch
    assert "handle_file_write" in sketch
    assert "handle_file_delete" in sketch
    assert "handle_file_rename" in sketch
    assert "SD.rename(source_path, target_path)" in sketch
    assert "REPLACE_RENAME_PRESERVES_OLD_ON_FAILURE" in sketch
    assert "rename_replace_preserving_old" in sketch
    assert "REPLACE_BACKUP_SUFFIX" in sketch
    assert "SD.rename(target_path, backup_path)" in sketch
    assert "(void)SD.rename(backup_path, target_path)" in sketch
    assert "ensure_parent_dirs" in sketch
    assert "strstr(path, \"..\")" in sketch
    assert "strstr(path, \"//\")" in sketch


def test_rp2040_docs_mark_ci_build_and_store_migration_pending():
    readme = README.read_text(encoding="utf-8")

    assert "GitHub Actions" in readme
    assert "rp2040:rp2040:seeed_indicator_rp2040" in readme
    assert "Do not use the Windows host for firmware compilation" in readme
    assert "Retained Public message history, DM history, route history, and packet history" in readme
    assert "keeps onboard NVS mirrors for these retained" in readme
    assert "DESKOS_SD_FILE" in readme
    assert "192-byte chunks" in readme
