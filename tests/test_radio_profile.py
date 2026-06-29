import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_uscan_radio_profile_defaults():
    profile = json.loads((ROOT / "boards/seeed_indicator_d1l/radio_profile_uscan.json").read_text())
    assert profile["frequency_hz"] == 910_525_000
    assert profile["bandwidth_khz"] == 62.5
    assert profile["spreading_factor"] == 7
    assert profile["coding_rate"] == 5
    assert profile["tx_power_dbm"] == 20
    assert profile["tcxo"] == "NONE"


def test_pinmap_is_d1l_not_tdeck():
    pinmap = json.loads((ROOT / "boards/seeed_indicator_d1l/pinmap.json").read_text())
    assert pinmap["sx1262"]["spi_sclk_gpio"] == 41
    assert pinmap["sx1262"]["spi_mosi_gpio"] == 48
    assert pinmap["sx1262"]["spi_miso_gpio"] == 47
    assert pinmap["button"]["user_gpio"] == 38
    assert pinmap["sx1262"]["tcxo_default"] == "NONE"
