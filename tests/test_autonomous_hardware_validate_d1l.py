import json
from pathlib import Path

import pytest

from scripts import autonomous_hardware_validate_d1l as runner


COMMIT = "1600d649223d8f5cbf35cf587d44bd94c0f21293"


def write_flasher_args(build: Path) -> None:
    (build / "bootloader").mkdir(parents=True)
    (build / "partition_table").mkdir(parents=True)
    (build / "bootloader" / "bootloader.bin").write_bytes(b"boot")
    (build / "partition_table" / "partition-table.bin").write_bytes(b"part")
    (build / "meshcore_deskos_d1l.bin").write_bytes(b"app")
    (build / "flasher_args.json").write_text(
        json.dumps(
            {
                "write_flash_args": ["--flash_mode", "dio", "--flash_size", "8MB", "--flash_freq", "80m"],
                "flash_files": {
                    "0x10000": "meshcore_deskos_d1l.bin",
                    "0x0": "bootloader/bootloader.bin",
                    "0x8000": "partition_table/partition-table.bin",
                },
                "extra_esptool_args": {
                    "after": "hard_reset",
                    "before": "default_reset",
                    "chip": "esp32s3",
                },
            }
        ),
        encoding="ascii",
    )


def test_port_guard_blocks_forbidden_ports():
    with pytest.raises(ValueError):
        runner.enforce_port_guard("COM12", "COM29")
    with pytest.raises(ValueError):
        runner.enforce_port_guard("com11")


def test_esptool_command_uses_actions_build_files(tmp_path):
    build = tmp_path / "build"
    write_flasher_args(build)

    command = runner.esptool_flash_command(build, "COM12", 460800)

    assert "--chip" in command
    assert "esp32s3" in command
    assert "-p" in command
    assert "COM12" in command
    assert "--flash-mode" in command
    assert "--flash_size" not in command
    assert command.index("0x0") < command.index("0x8000") < command.index("0x10000")
    assert str((build / "meshcore_deskos_d1l.bin").resolve()) in command


def test_release_gate_command_disables_meshbot_port(tmp_path):
    ctx = runner.RunContext(
        root=tmp_path,
        commit=COMMIT,
        short_commit=COMMIT[:7],
        github_run_id="28663994079",
        github_run_dir=tmp_path / "artifacts" / "github" / "28663994079-current",
        d1l_port="COM12",
        rp2040_port="COM16",
        hardware_dir=tmp_path / "artifacts" / "hardware" / "com12",
        rp2040_hardware_dir=tmp_path / "artifacts" / "hardware" / "com16",
        baud=115200,
        esp32_flash_baud=460800,
    )

    command = runner.release_gate_command(ctx, tmp_path / "gate.json")

    assert "--meshbot-port" in command
    assert command[command.index("--meshbot-port") + 1] == "COM_DISABLED"
    assert "COM11" not in command
    assert "COM29" not in command


def test_dry_run_plan_is_noninteractive_and_port_safe(tmp_path):
    run_dir = tmp_path / "artifacts" / "github" / "28663994079-current"
    args = runner.parse_args(
        [
            "--root",
            str(tmp_path),
            "--commit",
            COMMIT,
            "--github-run-id",
            "28663994079",
            "--github-run-dir",
            str(run_dir),
            "--dry-run",
            "--skip-esp32-flash",
            "--skip-rp2040-official-smoke",
        ]
    )

    ctx = runner.build_context(args)
    plan = runner.plan_report(ctx, args)

    assert plan["manual_user_required"] is False
    assert plan["ports"]["d1l"] == "COM12"
    assert plan["ports"]["rp2040"] == "COM16"
    assert "COM11" not in json.dumps(plan["steps"])
    assert "COM29" not in json.dumps(plan["steps"])
