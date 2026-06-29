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
    }


def test_phase1_smoke_covers_button_rp2040_and_packets():
    assert "button" in SMOKE_COMMANDS
    assert "rp2040 status" in SMOKE_COMMANDS
    assert "packets" in SMOKE_COMMANDS


def test_phase1_lvgl_uses_basic_flush_path():
    defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    assert "CONFIG_LCD_AVOID_TEAR=y" not in defaults
    assert "CONFIG_LCD_LVGL_DIRECT_MODE=y" not in defaults
    assert "# CONFIG_LCD_LVGL_DIRECT_MODE is not set" in defaults
