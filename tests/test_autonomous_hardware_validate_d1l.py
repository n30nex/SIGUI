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
    assert "rp2040_autonomous_access_precheck" in plan["steps"]
    assert "d1l_500_cycle_tab_abuse" not in plan["steps"]
    assert "d1l_scroll_probe" not in plan["steps"]


def test_rp2040_port_discovery_selects_allowed_usb_cdc_and_skips_protected_ports(monkeypatch):
    monkeypatch.setattr(runner, "rp2040_port_snapshot", lambda port: {"port": port, "present": False, "matches": []})
    monkeypatch.setattr(
        runner,
        "serial_port_inventory",
        lambda: [
            {"device": "COM12", "description": "USB-SERIAL CH340", "vid": "1A86", "pid": "7523"},
            {"device": "COM29", "description": "Adafruit Feather RP2040", "vid": "239A", "pid": "8029"},
            {"device": "COM17", "description": "Raspberry Pi Pico", "vid": "2E8A", "pid": "000A"},
        ],
    )

    report = runner.rp2040_port_discovery("COM16", "COM12")

    assert report["present"] is True
    assert report["selected_port"] == "COM17"
    assert report["selected_reason"] == "rp2040_usb_descriptor"
    assert report["candidates"][0]["match_reasons"] == ["keyword:pico", "keyword:raspberry pi pico", "vid:2E8A"]
    assert {"device": "COM12", "reason": "d1l_console_port"} in report["skipped"]
    assert {"device": "COM29", "reason": "forbidden_port"} in report["skipped"]


def test_rp2040_access_precheck_fails_fast_without_volume_port_or_ping(tmp_path, monkeypatch):
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
    commands = []

    monkeypatch.setattr(runner, "uf2_volume_snapshot", lambda: {"available": False, "candidates": []})
    monkeypatch.setattr(runner, "rp2040_port_snapshot", lambda port: {"port": port, "present": False, "matches": []})
    monkeypatch.setattr(
        runner,
        "rp2040_port_discovery",
        lambda port, d1l_port: {"preferred_port": port, "d1l_port": d1l_port, "present": False, "selected_port": None, "candidates": [], "skipped": []},
    )
    monkeypatch.setattr(runner.time, "sleep", lambda _seconds: None)

    def fake_console(port, baud, command, timeout, *, settle_sec=1.0):
        commands.append(command)
        if command == "rp2040 status":
            return {"ok": True, "cmd": command, "uart_ready": True}
        if command.startswith("rp2040 double-reset") or command == "rp2040 reset":
            return {"ok": True, "cmd": command}
        return {"ok": False, "cmd": command, "code": "ESP_ERR_TIMEOUT", "protocol_supported": False}

    monkeypatch.setattr(runner, "send_d1l_console", fake_console)

    report = runner.rp2040_access_precheck(ctx, dry_run=False)
    payload = json.loads(runner.rp2040_access_precheck_out(ctx).read_text(encoding="ascii"))

    assert report["ok"] is False
    assert report["state"] == "no_autonomous_bootloader_path"
    assert report["error"] == "rp2040_bootloader_unavailable"
    assert report["manual_user_required"] is False
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert commands == [
        "rp2040 status",
        "rp2040 ping",
        "rp2040 double-reset 50 150 1500",
        "rp2040 double-reset 30 300 1500",
        "rp2040 double-reset 100 500 2000",
        "rp2040 reset",
        "rp2040 ping",
    ]
    assert payload["state"] == "no_autonomous_bootloader_path"


def test_validation_stops_at_access_precheck_before_official_smoke_or_restore(tmp_path, monkeypatch):
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

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, allow_download, dry_run: {"schema": 1, "kind": "input_artifact_check", "ok": True})
    monkeypatch.setattr(
        runner,
        "rp2040_access_precheck",
        lambda ctx, dry_run: {
            "schema": 1,
            "kind": "rp2040_autonomous_access_precheck",
            "ok": False,
            "error": "rp2040_bootloader_unavailable",
        },
    )
    monkeypatch.setattr(runner, "run_official_sd_smoke", lambda *args, **kwargs: (_ for _ in ()).throw(AssertionError("official smoke should not run")))
    monkeypatch.setattr(runner, "restore_bridge", lambda *args, **kwargs: (_ for _ in ()).throw(AssertionError("restore should not run")))
    monkeypatch.setattr(runner, "run_release_gate", lambda ctx, dry_run: {"schema": 1, "kind": "release_gate_audit", "ok": True, "ready_for_public_release": False, "failed_count": 1, "p0_failed_count": 1})

    report = runner.run_validation(args)

    assert report["ok"] is False
    assert report["error"] == "rp2040_bootloader_unavailable"
    assert [step["kind"] for step in report["runs"]] == [
        "input_artifact_check",
        "rp2040_autonomous_access_precheck",
        "release_gate_audit",
    ]


def test_bootloader_entry_prefers_bridge_command_when_ping_ready(tmp_path, monkeypatch):
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
    commands = []

    monkeypatch.setattr(runner, "uf2_volume_snapshot", lambda: {"available": False, "candidates": []})
    monkeypatch.setattr(runner, "rp2040_port_snapshot", lambda port: (_ for _ in ()).throw(AssertionError("COM16 fallback should not run")))
    monkeypatch.setattr(runner, "enter_rp2040_bootloader_usb_touch", lambda port: (_ for _ in ()).throw(AssertionError("USB touch should not run")))
    monkeypatch.setattr(runner.time, "sleep", lambda _seconds: None)

    def fake_console(port, baud, command, timeout, *, settle_sec=1.0):
        commands.append(command)
        if command == "rp2040 ping":
            return {"ok": True, "cmd": command, "protocol_supported": True}
        if command == "rp2040 bootloader":
            return {"ok": True, "cmd": command}
        return {"ok": False, "cmd": command}

    monkeypatch.setattr(runner, "send_d1l_console", fake_console)

    report = runner.enter_rp2040_bootloader(ctx, volume=None)

    assert report["ok"] is True
    assert report["method"] == "rp2040_bridge_command"
    assert commands == ["rp2040 ping", "rp2040 bootloader"]
    assert report["bridge_bootloader"]["cmd"] == "rp2040 bootloader"


def test_bootloader_entry_uses_discovered_rp2040_usb_cdc_port(tmp_path, monkeypatch):
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
    commands = []
    touched = []

    monkeypatch.setattr(runner, "uf2_volume_snapshot", lambda: {"available": False, "candidates": []})
    monkeypatch.setattr(
        runner,
        "rp2040_port_discovery",
        lambda port, d1l_port: {
            "preferred_port": port,
            "d1l_port": d1l_port,
            "present": True,
            "selected_port": "COM17",
            "selected_reason": "rp2040_usb_descriptor",
            "candidates": [{"device": "COM17", "match_reasons": ["vid:2E8A"]}],
            "skipped": [],
        },
    )
    monkeypatch.setattr(runner.time, "sleep", lambda _seconds: None)
    monkeypatch.setattr(runner, "enter_rp2040_bootloader_usb_touch", lambda port: touched.append(port) or {"ok": True, "port": port})

    def fake_console(port, baud, command, timeout, *, settle_sec=1.0):
        commands.append(command)
        return {"ok": False, "cmd": command, "code": "ESP_ERR_TIMEOUT", "protocol_supported": False}

    monkeypatch.setattr(runner, "send_d1l_console", fake_console)

    report = runner.enter_rp2040_bootloader(ctx, volume=None)

    assert report["ok"] is True
    assert report["method"] == "rp2040_1200_baud_touch"
    assert report["selected_rp2040_port"] == "COM17"
    assert touched == ["COM17"]
    assert commands == ["rp2040 ping"]


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
    monkeypatch.setattr(runner, "rp2040_access_precheck", lambda ctx, dry_run: ok_step("rp2040_autonomous_access_precheck"))
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

    monkeypatch.setattr(runner, "enter_rp2040_bootloader", lambda ctx, volume: {"ok": True, "port": ctx.rp2040_port})
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
    monkeypatch.setattr(runner, "rp2040_access_precheck", lambda ctx, dry_run: {"schema": 1, "kind": "rp2040_autonomous_access_precheck", "ok": True})
    monkeypatch.setattr(runner, "restore_bridge", failed_restore)
    monkeypatch.setattr(runner, "run_preflight", lambda ctx, dry_run: (_ for _ in ()).throw(AssertionError("preflight should not run")))
    monkeypatch.setattr(runner, "run_release_gate", lambda ctx, dry_run: {"schema": 1, "kind": "release_gate_audit", "ok": True, "ready_for_public_release": False, "failed_count": 1, "p0_failed_count": 1})

    report = runner.run_validation(args)

    assert report["ok"] is False
    assert report["error"] == "bridge_restore_not_verified"
    assert len(restore_attempts) == 2
    assert [step["kind"] for step in report["runs"]] == [
        "input_artifact_check",
        "rp2040_autonomous_access_precheck",
        "rp2040_bridge_restore",
        "rp2040_bridge_restore",
        "release_gate_audit",
    ]
    assert report["release_gate"]["ready_for_public_release"] is False
