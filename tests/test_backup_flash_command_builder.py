from pathlib import Path

from scripts.backup_flash_d1l import build_read_flash_command, parse_size


def test_parse_size_aliases():
    assert parse_size("8MB") == 8 * 1024 * 1024
    assert parse_size("0x1000") == 4096


def test_build_read_flash_command_requires_explicit_port_shape():
    cmd = build_read_flash_command("COMX", 1024, Path("backup.bin"))
    assert "--port" in cmd
    assert "COMX" in cmd
    assert "read_flash" in cmd
    assert "esp32s3" in cmd
