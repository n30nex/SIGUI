from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_d1l_uses_custom_8mb_partition_table():
    defaults = read("sdkconfig.defaults")
    table = read("partitions_d1l.csv")

    assert "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" in defaults
    assert "CONFIG_PARTITION_TABLE_CUSTOM=y" in defaults
    assert 'CONFIG_PARTITION_TABLE_FILENAME="partitions_d1l.csv"' in defaults

    assert "nvs,        data, nvs,     0x9000,   0x6000," in table
    assert "phy_init,   data, phy,     0xf000,   0x1000," in table
    assert "factory,    app,  factory, 0x10000,  0x7F0000," in table


def test_d1l_factory_partition_has_release_headroom():
    table = read("partitions_d1l.csv")
    factory_line = next(line for line in table.splitlines() if line.startswith("factory,"))
    parts = [part.strip() for part in factory_line.split(",")]

    assert parts[:5] == ["factory", "app", "factory", "0x10000", "0x7F0000"]
    factory_offset = int(parts[3], 16)
    factory_size = int(parts[4], 16)

    assert factory_offset == 0x10000
    assert factory_size >= 0x200000
    assert factory_offset + factory_size == 0x800000
