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


def test_official_sd_smoke_exception_writes_gate_visible_artifact(tmp_path, monkeypatch):
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
            "--skip-esp32-flash",
        ]
    )

    def ok_step(kind: str) -> dict:
        return {"schema": 1, "kind": kind, "ok": True, "public_rf_tx": False, "formats_sd": False}

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, allow_download, dry_run: ok_step("input_artifact_check"))
    monkeypatch.setattr(runner, "run_official_sd_smoke", lambda *args, **kwargs: (_ for _ in ()).throw(OSError(22, "Invalid argument")))
    monkeypatch.setattr(runner, "restore_bridge", lambda ctx, volume, uf2_timeout, dry_run: ok_step("rp2040_bridge_restore"))
    monkeypatch.setattr(runner, "run_preflight", lambda ctx, dry_run: ok_step("rp2040_bridge_preflight"))
    monkeypatch.setattr(runner, "run_smoke", lambda ctx, dry_run: ok_step("d1l_smoke"))
    monkeypatch.setattr(runner, "run_tab_abuse", lambda ctx, cycles, dry_run: ok_step("tab_abuse"))
    monkeypatch.setattr(runner, "run_scroll_probe", lambda ctx, dry_run: ok_step("scroll_probe"))
    monkeypatch.setattr(runner, "run_release_gate", lambda ctx, dry_run: ok_step("release_gate_audit"))

    report = runner.run_validation(args)
    artifact = runner.official_sd_smoke_out(runner.build_context(args))
    payload = json.loads(artifact.read_text(encoding="ascii"))

    assert report["ok"] is False
    assert artifact.exists()
    assert payload["kind"] == "seeed_official_sd_smoke_capture"
    assert payload["ok"] is False
    assert payload["commit"] == COMMIT
    assert payload["github_actions_run"] == "28663994079"
    assert payload["port"] == "COM16"
    assert payload["public_rf_tx"] is False
    assert payload["formats_sd"] is False
    assert "Invalid argument" in payload["error"]


def test_bridge_restore_verifies_ping_before_reporting_ok(tmp_path, monkeypatch):
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

    monkeypatch.setattr(runner, "enter_rp2040_bootloader", lambda port: {"ok": True, "port": port})
    monkeypatch.setattr(runner, "copy_named_uf2", lambda **_kwargs: {"ok": True, "copied": True})
    monkeypatch.setattr(runner.time, "sleep", lambda _seconds: None)
    monkeypatch.setattr(
        runner,
        "send_d1l_console",
        lambda port, baud, command, timeout: {
            "ok": command == "rp2040 ping",
            "cmd": command,
            "protocol_supported": command == "rp2040 ping",
        },
    )

    report = runner.restore_bridge(ctx, volume=None, uf2_timeout=1.0, dry_run=False)

    assert report["ok"] is True
    assert report["reset"]["cmd"] == "rp2040 reset"
    assert report["ping"]["cmd"] == "rp2040 ping"
    assert json.loads(runner.bridge_restore_out(ctx).read_text(encoding="ascii"))["ok"] is True


def test_validation_stops_before_preflight_when_bridge_restore_not_verified(tmp_path, monkeypatch):
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
            "--skip-esp32-flash",
            "--skip-rp2040-official-smoke",
        ]
    )

    restore_attempts = []

    def failed_restore(ctx, volume, uf2_timeout, dry_run):
        restore_attempts.append(1)
        return {"schema": 1, "kind": "rp2040_bridge_restore", "ok": False, "error": "bridge_restore_not_verified"}

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, allow_download, dry_run: {"schema": 1, "kind": "input_artifact_check", "ok": True})
    monkeypatch.setattr(runner, "restore_bridge", failed_restore)
    monkeypatch.setattr(runner, "run_preflight", lambda ctx, dry_run: (_ for _ in ()).throw(AssertionError("preflight should not run")))
    monkeypatch.setattr(runner, "run_release_gate", lambda ctx, dry_run: {"schema": 1, "kind": "release_gate_audit", "ok": True, "ready_for_public_release": False, "failed_count": 1, "p0_failed_count": 1})

    report = runner.run_validation(args)

    assert report["ok"] is False
    assert report["error"] == "bridge_restore_not_verified"
    assert len(restore_attempts) == 2
    assert [step["kind"] for step in report["runs"]] == [
        "input_artifact_check",
        "rp2040_bridge_restore",
        "rp2040_bridge_restore",
        "release_gate_audit",
    ]
    assert report["release_gate"]["ready_for_public_release"] is False
