import json
from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def load_pinmap() -> dict:
    return json.loads((ROOT / "boards" / "seeed_indicator_d1l" / "pinmap.json").read_text(encoding="utf-8"))


def test_rp2040_bridge_pin_contract():
    pinmap = load_pinmap()
    assert pinmap["rp2040_bridge"] == {
        "esp_rx_gpio": 20,
        "esp_tx_gpio": 19,
        "expander_reset": 8,
        "uart_port": 2,
        "baud": 921600,
    }


def test_phase1_smoke_covers_button_rp2040_and_packets():
    assert "button" in SMOKE_COMMANDS
    assert "rp2040 status" in SMOKE_COMMANDS
    assert "rp2040 ping" not in SMOKE_COMMANDS
    assert "rp2040 reset" not in SMOKE_COMMANDS
    assert "packets" in SMOKE_COMMANDS


def test_console_exposes_explicit_rp2040_reset_command():
    console = (ROOT / "main" / "comms" / "usb_console.c").read_text(encoding="utf-8")
    rp2040_header = (ROOT / "main" / "hal" / "rp2040_bridge.h").read_text(encoding="utf-8")
    rp2040_source = (ROOT / "main" / "hal" / "rp2040_bridge.c").read_text(encoding="utf-8")
    assert "d1l_rp2040_bridge_reset" in rp2040_header
    assert "d1l_rp2040_bridge_ping" in rp2040_header
    assert "D1L_RP2040_PING_QUERY" in rp2040_source
    assert "tca9535_set_direction(pins->expander_reset, true)" in rp2040_source
    assert "tca9535_set_level(pins->expander_reset, false)" in rp2040_source
    assert "tca9535_set_level(pins->expander_reset, true)" in rp2040_source
    assert 'ok_begin("rp2040 reset")' in console
    assert '"rp2040 ping"' in console
    assert 'cmd_rp2040_ping' in console
    assert 'strcmp(line, "rp2040 ping")' in console
    assert '"rp2040 reset"' in console
    assert 'strcmp(line, "rp2040 reset")' in console
    assert "public_rf_tx" in console
    assert "formats_sd" in console


def test_phase1_lvgl_uses_basic_flush_path():
    defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    assert "CONFIG_LCD_AVOID_TEAR=y" not in defaults
    assert "CONFIG_LCD_LVGL_DIRECT_MODE=y" not in defaults
    assert "# CONFIG_LCD_LVGL_DIRECT_MODE is not set" in defaults


def test_touch_path_uses_pressed_state_not_uninitialized_btn_val():
    ui_source = (ROOT / "main" / "ui" / "ui_phase1.c").read_text(encoding="utf-8")
    board_source = (ROOT / "main" / "hal" / "indicator_board.c").read_text(encoding="utf-8")
    seeed_patch = (ROOT / "patches" / "sensecap_indicator_touch_fix.patch").read_text(encoding="utf-8")
    console_source = (ROOT / "main" / "comms" / "usb_console.c").read_text(encoding="utf-8")

    assert "uint8_t btn_val = 0;" in seeed_patch
    assert "bool pressed = (tp_num > 0) || (btn_val != 0);" in seeed_patch
    assert "sample.pressed" in ui_source
    assert "last_x" in ui_source
    assert "tp_dev_id = 0;" in seeed_patch
    assert "d1l_board_touch_read" in board_source
    assert '\\"pressed\\":%s' in console_source
    assert '\\"coordinate_valid\\":%s' in console_source


def test_cmake_applies_parent_owned_seeed_touch_patch():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    patch = (ROOT / "patches" / "sensecap_indicator_touch_fix.patch").read_text(encoding="utf-8")

    assert 'COMMAND git rev-parse --short HEAD' in cmake
    assert 'set(PROJECT_VER "${D1L_GIT_SHORT_SHA}")' in cmake
    assert "SEEED_BSP_TOUCH_PATCH" in cmake
    assert "git apply --unidiff-zero --check" in cmake
    assert "git apply --unidiff-zero --reverse --check" in cmake
    assert "components/bsp/src/indev/indev.c" in patch
    assert "data->btn_val = pressed ? 1 : 0;" in patch


def test_build_script_is_host_only_and_rewrites_stale_unsafe_lcd_config():
    script = (ROOT / "scripts" / "build_d1l.ps1").read_text(encoding="utf-8")
    assert "Set-D1lSafeSdkconfig" in script
    assert "CONFIG_LCD_AVOID_TEAR=y" in script
    assert "CONFIG_LCD_LVGL_DIRECT_MODE=y" in script
    assert "# CONFIG_LCD_AVOID_TEAR is not set" in script
    assert "# CONFIG_LCD_LVGL_DIRECT_MODE is not set" in script
    assert "disabled_local_github_actions_only" in script
    assert "Use GitHub Actions d1l-ci firmware-build" in script
    assert "& idf.py build" not in script
