import json
from pathlib import Path

import pytest

from scripts import autonomous_hardware_validate_d1l as runner


COMMIT = "1600d649223d8f5cbf35cf587d44bd94c0f21293"


def patch_sd_evidence_runners(monkeypatch, ok_step):
    monkeypatch.setattr(runner, "run_sd_raw_diag", lambda ctx, dry_run: ok_step("sd_raw_diag"))
    monkeypatch.setattr(
        runner,
        "run_sd_boot_prepare_scenario",
        lambda ctx, scenario, dry_run: ok_step(f"sd_boot_prepare_{scenario.replace('-', '_')}"),
    )
    monkeypatch.setattr(runner, "run_sd_map_tile_canary", lambda ctx, dry_run: ok_step("sd_map_tile_canary"))
    monkeypatch.setattr(runner, "run_sd_export_canary", lambda ctx, dry_run: ok_step("sd_export_canary"))
    monkeypatch.setattr(runner, "run_sd_diagnostic_export", lambda ctx, dry_run: ok_step("sd_diagnostic_export"))
    monkeypatch.setattr(runner, "run_sd_data_export", lambda ctx, dry_run: ok_step("sd_data_export"))
    monkeypatch.setattr(runner, "run_sd_retained_history", lambda ctx, dry_run: ok_step("sd_retained_history"))
    monkeypatch.setattr(runner, "run_sd_reboot_remount", lambda ctx, dry_run: ok_step("sd_reboot_remount"))


def write_flasher_args(build: Path) -> None:
    (build / "bootloader").mkdir(parents=True, exist_ok=True)
    (build / "partition_table").mkdir(parents=True, exist_ok=True)
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
    artifact_root = build.parent
    rows = []
    for path in sorted(
        item
        for item in artifact_root.rglob("*")
        if item.is_file() and item.name != "SHA256SUMS.txt"
    ):
        relative = path.relative_to(artifact_root).as_posix()
        rows.append(f"{runner.sha256_file(path)}  {relative}")
    (artifact_root / "SHA256SUMS.txt").write_text("\n".join(rows) + "\n", encoding="ascii")


def test_port_guard_blocks_forbidden_ports():
    with pytest.raises(ValueError):
        runner.enforce_port_guard("COM12", "COM29")
    with pytest.raises(ValueError):
        runner.enforce_port_guard("com11")
    with pytest.raises(ValueError):
        runner.enforce_port_guard("COM8")


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


def test_flash_esp32_waits_after_successful_hard_reset(tmp_path, monkeypatch):
    run_dir = tmp_path / "artifacts" / "github" / "28663994079-current"
    write_flasher_args(run_dir / "d1l-firmware-artifacts" / "build")
    ctx = runner.RunContext(
        root=tmp_path,
        commit=COMMIT,
        short_commit=COMMIT[:7],
        github_run_id="28663994079",
        github_run_dir=run_dir,
        d1l_port="COM12",
        rp2040_port="COM16",
        hardware_dir=tmp_path / "artifacts" / "hardware" / "com12",
        rp2040_hardware_dir=tmp_path / "artifacts" / "hardware" / "com16",
        baud=115200,
        esp32_flash_baud=460800,
    )
    sleeps = []

    monkeypatch.setattr(runner, "command_report", lambda *args, **kwargs: {"ok": True})
    monkeypatch.setattr(runner.time, "sleep", lambda seconds: sleeps.append(seconds))

    report = runner.flash_esp32(ctx, dry_run=False)

    assert report["ok"] is True
    assert report["artifact_verification"]["ok"] is True
    assert report["artifact_verification"]["manifest_complete"] is True
    assert len(report["artifact_verification"]["flash_files"]) == 3
    assert report["flashed_at"]
    assert report["post_flash_settle_sec"] == runner.POST_ESP32_FLASH_SETTLE_SEC
    assert sleeps == [runner.POST_ESP32_FLASH_SETTLE_SEC]


def test_flash_esp32_fails_closed_on_incomplete_or_tampered_actions_manifest(
    tmp_path, monkeypatch
):
    run_dir = tmp_path / "artifacts" / "github" / "28663994079-current"
    build = run_dir / "d1l-firmware-artifacts" / "build"
    write_flasher_args(build)
    ctx = runner.RunContext(
        root=tmp_path,
        commit=COMMIT,
        short_commit=COMMIT[:7],
        github_run_id="28663994079",
        github_run_dir=run_dir,
        d1l_port="COM12",
        rp2040_port="COM16",
        hardware_dir=tmp_path / "artifacts" / "hardware" / "com12",
        rp2040_hardware_dir=tmp_path / "artifacts" / "hardware" / "com16",
        baud=115200,
        esp32_flash_baud=460800,
    )
    calls = []
    monkeypatch.setattr(runner, "command_report", lambda *args, **kwargs: calls.append(args))

    manifest = run_dir / "d1l-firmware-artifacts" / "SHA256SUMS.txt"
    manifest.write_text("", encoding="ascii")
    empty = runner.flash_esp32(ctx, dry_run=False)

    assert empty["ok"] is False
    assert empty["artifact_verification"]["ok"] is False
    assert calls == []

    write_flasher_args(build)
    (build / "meshcore_deskos_d1l.bin").write_bytes(b"tampered")
    tampered = runner.flash_esp32(ctx, dry_run=False)

    assert tampered["ok"] is False
    assert "checksum mismatch" in tampered["artifact_verification"]["error"]
    assert calls == []


def test_flash_esp32_rejects_checksummed_extra_flash_role(tmp_path):
    run_dir = tmp_path / "artifacts" / "github" / "28663994079-current"
    artifact_root = run_dir / "d1l-firmware-artifacts"
    build = artifact_root / "build"
    write_flasher_args(build)
    extra = build / "unexpected-nvs.bin"
    extra.write_bytes(b"must not be flashed")
    flasher_path = build / "flasher_args.json"
    flasher = json.loads(flasher_path.read_text(encoding="utf-8"))
    flasher["flash_files"]["0x9000"] = extra.name
    flasher_path.write_text(json.dumps(flasher), encoding="ascii")
    manifest = artifact_root / "SHA256SUMS.txt"
    rows = []
    for path in sorted(
        item
        for item in artifact_root.rglob("*")
        if item.is_file() and item != manifest
    ):
        rows.append(
            f"{runner.sha256_file(path)}  {path.relative_to(artifact_root).as_posix()}"
        )
    manifest.write_text("\n".join(rows) + "\n", encoding="ascii")
    ctx = runner.RunContext(
        root=tmp_path,
        commit=COMMIT,
        short_commit=COMMIT[:7],
        github_run_id="28663994079",
        github_run_dir=run_dir,
        d1l_port="COM12",
        rp2040_port="COM16",
        hardware_dir=tmp_path / "artifacts" / "hardware" / "com12",
        rp2040_hardware_dir=tmp_path / "artifacts" / "hardware" / "com16",
        baud=115200,
        esp32_flash_baud=460800,
    )

    with pytest.raises(runner.FlashGuardError, match="must exactly match"):
        runner.verify_esp32_flash_inputs(ctx)


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
    assert "COM8" not in command
    assert "COM11" not in command
    assert "COM29" not in command


def test_autonomous_smoke_binds_device_version_to_selected_commit(tmp_path, monkeypatch):
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
    captured = {}

    def fake_run(_ctx, kind, args, out, timeout, dry_run):
        captured["kind"] = kind
        captured["args"] = args
        return {"ok": True}

    monkeypatch.setattr(runner, "run_existing_script", fake_run)

    report = runner.run_smoke(ctx, dry_run=True)

    assert report["ok"] is True
    assert captured["kind"] == "smoke"
    index = captured["args"].index("--expected-firmware-commit")
    assert captured["args"][index + 1] == COMMIT


def test_compose_keyboard_capture_command_uses_com12_targets_and_artifact_path(tmp_path, monkeypatch):
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
    captured = {}

    def fake_run_existing_script(ctx_arg, kind, args, out, timeout, dry_run):
        captured.update({"kind": kind, "args": args, "out": out, "timeout": timeout, "dry_run": dry_run})
        return {"schema": 1, "kind": kind, "ok": True}

    monkeypatch.setattr(runner, "run_existing_script", fake_run_existing_script)

    report = runner.run_ui_compose_keyboard_capture(ctx, dry_run=False)

    assert report["ok"] is True
    assert captured["kind"] == "ui_compose_keyboard_capture"
    assert captured["args"][captured["args"].index("--port") + 1] == "COM12"
    assert captured["args"][captured["args"].index("--targets") + 1] == "all"
    assert "COM8" not in json.dumps(captured["args"])
    assert "COM11" not in json.dumps(captured["args"])
    assert "COM29" not in json.dumps(captured["args"])
    assert captured["out"].name == "ui_compose_keyboard_capture_1600d64_actions_28663994079_COM12.json"
    assert captured["timeout"] == 900


def test_pixel_capture_generates_home_reference_and_diff_gate(tmp_path, monkeypatch):
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
    captured = {}

    def fake_command_report(name, command, cwd, timeout=None):
        assert name == "ui_simulator_reference"
        out_dir = Path(command[command.index("--out") + 1])
        out_dir.mkdir(parents=True)
        (out_dir / "home.png").write_bytes(b"png")
        (out_dir / "ui-sim-report.json").write_text("{}", encoding="ascii")
        return {"ok": True}

    def fake_run_existing_script(ctx_arg, kind, args, out, timeout, dry_run):
        captured.update({"kind": kind, "args": args, "out": out, "timeout": timeout, "dry_run": dry_run})
        return {"schema": 1, "kind": kind, "ok": True}

    monkeypatch.setattr(runner, "command_report", fake_command_report)
    monkeypatch.setattr(runner, "run_existing_script", fake_run_existing_script)

    report = runner.run_ui_pixel_capture(ctx, dry_run=False)

    assert report["ok"] is True
    assert report["reference"]["kind"] == "ui_simulator_reference"
    assert report["reference"]["view"] == "home"
    assert captured["kind"] == "ui_pixel_capture"
    assert captured["args"][captured["args"].index("--port") + 1] == "COM12"
    assert captured["args"][captured["args"].index("--prep-command") + 1] == "ui tab home"
    assert captured["args"][captured["args"].index("--reference-view") + 1] == "home"
    assert captured["args"][captured["args"].index("--reference-png") + 1].endswith("home.png")
    assert captured["args"][captured["args"].index("--diff-out") + 1].endswith("_simdiff.json")
    assert "COM8" not in json.dumps(captured["args"])
    assert "COM11" not in json.dumps(captured["args"])
    assert "COM29" not in json.dumps(captured["args"])


def test_smoke_retries_once_after_post_flash_console_timeout(tmp_path, monkeypatch):
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
    attempts = []
    sleeps = []

    def fake_run_existing_script(ctx_arg, kind, args, out, timeout, dry_run):
        attempts.append(out)
        out.parent.mkdir(parents=True, exist_ok=True)
        ok = len(attempts) == 2
        out.write_text(json.dumps({"schema": 1, "kind": kind, "ok": ok}), encoding="ascii")
        return {"schema": 1, "kind": kind, "ok": ok, "path": str(out)}

    monkeypatch.setattr(runner, "run_existing_script", fake_run_existing_script)
    monkeypatch.setattr(runner.time, "sleep", lambda seconds: sleeps.append(seconds))

    report = runner.run_smoke(ctx, dry_run=False)

    assert report["ok"] is True
    assert report["retry_after_failure"] is True
    assert report["smoke_retry_settle_sec"] == runner.SMOKE_RETRY_SETTLE_SEC
    assert sleeps == [runner.SMOKE_RETRY_SETTLE_SEC]
    assert len(attempts) == 2
    assert Path(report["first_attempt_path"]).read_text(encoding="ascii")


def test_sd_file_canary_uses_post_restore_timing_window(tmp_path, monkeypatch):
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
    captured = {}

    def fake_run_existing_script(ctx_arg, kind, args, out, timeout, dry_run):
        captured.update({"kind": kind, "args": args, "out": out, "timeout": timeout, "dry_run": dry_run})
        return {"schema": 1, "kind": kind, "ok": True}

    monkeypatch.setattr(runner, "run_existing_script", fake_run_existing_script)

    report = runner.run_sd_file_canary(ctx, dry_run=False)

    assert report["ok"] is True
    assert captured["kind"] == "sd_file_canary"
    assert captured["args"][captured["args"].index("--port") + 1] == "COM12"
    assert captured["args"][captured["args"].index("--timeout") + 1] == "45"
    assert captured["args"][captured["args"].index("--mount-wait-sec") + 1] == "60"
    assert captured["out"].name == "sd_file_canary_1600d64_actions_28663994079_COM12.json"
    assert captured["timeout"] == 120


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
    assert "COM8" not in json.dumps(plan["steps"])
    assert "COM11" not in json.dumps(plan["steps"])
    assert "COM29" not in json.dumps(plan["steps"])
    assert plan["rp2040_uf2_flash"] is False
    assert plan["rp2040_flash_mode"] == "use_existing_bridge"
    assert "rp2040_autonomous_access_precheck" not in plan["steps"]
    assert "flash_official_rp2040_sd_smoke" not in plan["steps"]
    assert "restore_rp2040_bridge" not in plan["steps"]
    assert "sd_raw_diag" in plan["steps"]
    assert "sd_boot_prepare_correct_structure" in plan["steps"]
    assert "sd_boot_prepare_missing_structure" in plan["steps"]
    assert "sd_boot_prepare_existing_data" in plan["steps"]
    assert "sd_export_canary" in plan["steps"]
    assert "sd_diagnostic_export" in plan["steps"]
    assert "sd_data_export" in plan["steps"]
    assert "d1l_500_cycle_tab_abuse" not in plan["steps"]
    assert "d1l_ui_corruption_probe" not in plan["steps"]
    assert "d1l_scroll_probe" not in plan["steps"]
    assert "d1l_ui_pixel_capture" not in plan["steps"]
    assert "d1l_ui_compose_keyboard_capture" not in plan["steps"]


def test_dry_run_plan_includes_pixel_capture_with_ui_probes(tmp_path):
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
            "--include-ui-probes",
        ]
    )

    ctx = runner.build_context(args)
    plan = runner.plan_report(ctx, args)

    assert "d1l_onboarding_complete_for_ui_probes" in plan["steps"]
    assert "d1l_ui_corruption_probe" in plan["steps"]
    assert "d1l_scroll_probe" in plan["steps"]
    assert "d1l_ui_pixel_capture" in plan["steps"]
    assert "d1l_ui_compose_keyboard_capture" in plan["steps"]
    assert "COM8" not in json.dumps(plan["steps"])
    assert "COM11" not in json.dumps(plan["steps"])
    assert "COM29" not in json.dumps(plan["steps"])


def test_dry_run_plan_can_focus_esp32_ui_without_sd_or_rp2040_flash(tmp_path):
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
            "--skip-sd-suite",
            "--include-ui-probes",
        ]
    )

    ctx = runner.build_context(args)
    plan = runner.plan_report(ctx, args)

    assert plan["rp2040_uf2_flash"] is False
    assert plan["sd_suite_enabled"] is False
    assert "flash_esp32" in plan["steps"]
    assert "rp2040_autonomous_access_precheck" not in plan["steps"]
    assert "restore_rp2040_bridge" not in plan["steps"]
    assert "rp2040_bridge_preflight" not in plan["steps"]
    assert "sd_file_canary" not in plan["steps"]
    assert "d1l_onboarding_complete_for_ui_probes" in plan["steps"]
    assert "d1l_ui_corruption_probe" in plan["steps"]
    assert "d1l_ui_pixel_capture" in plan["steps"]


def test_rp2040_port_discovery_selects_allowed_usb_cdc_and_skips_protected_ports(monkeypatch):
    monkeypatch.setattr(runner, "rp2040_port_snapshot", lambda port: {"port": port, "present": False, "matches": []})
    monkeypatch.setattr(
        runner,
        "serial_port_inventory",
        lambda: [
            {"device": "COM8", "description": "Raspberry Pi Pico", "vid": "2E8A", "pid": "000A"},
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
    assert {"device": "COM8", "reason": "forbidden_port"} in report["skipped"]
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
        "rp2040 baud-probe 700",
        "rp2040 double-reset 50 150 1500",
        "rp2040 double-reset 30 300 1500",
        "rp2040 double-reset 100 500 2000",
        "rp2040 reset",
        "rp2040 ping",
        "rp2040 baud-probe 700",
    ]
    assert payload["state"] == "no_autonomous_bootloader_path"


def test_rp2040_access_precheck_switches_to_baud_probe_match(tmp_path, monkeypatch):
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
    monkeypatch.setattr(
        runner,
        "rp2040_port_discovery",
        lambda port, d1l_port: {"preferred_port": port, "d1l_port": d1l_port, "present": False, "selected_port": None, "candidates": [], "skipped": []},
    )

    def fake_console(port, baud, command, timeout, *, settle_sec=1.0):
        commands.append(command)
        if command == "rp2040 status":
            return {"ok": True, "cmd": command, "uart_ready": True}
        if command == "rp2040 ping":
            return {"ok": False, "cmd": command, "code": "ESP_ERR_TIMEOUT", "protocol_supported": False}
        if command == "rp2040 baud-probe 700":
            return {"ok": True, "cmd": command, "found_deskos": True, "selected_baud": 115200}
        if command == "rp2040 set-baud 115200":
            return {"ok": True, "cmd": command, "baud": 115200}
        return {"ok": True, "cmd": command, "protocol_supported": True}

    def fake_console_with_verified_ping(port, baud, command, timeout, *, settle_sec=1.0):
        if command == "rp2040 ping" and commands and commands[-1] == "rp2040 set-baud 115200":
            commands.append(command)
            return {"ok": True, "cmd": command, "protocol_supported": True}
        return fake_console(port, baud, command, timeout, settle_sec=settle_sec)

    monkeypatch.setattr(runner, "send_d1l_console", fake_console_with_verified_ping)

    report = runner.rp2040_access_precheck(ctx, dry_run=False)

    assert report["ok"] is True
    assert report["state"] == "bridge_protocol_ready_after_baud_probe"
    assert report["selected_rp2040_uart_baud"] == 115200
    assert report["bootloader_entry"] == "rp2040 bootloader"
    assert commands == [
        "rp2040 status",
        "rp2040 ping",
        "rp2040 baud-probe 700",
        "rp2040 set-baud 115200",
        "rp2040 ping",
    ]


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
            "--refresh-rp2040-smoke",
        ]
    )

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, args, allow_download, dry_run: {"schema": 1, "kind": "input_artifact_check", "ok": True})
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
    assert report["ready_for_public_release"] is False
    assert report["release_ready"] is False


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
    snapshots = [
        {"available": False, "candidates": []},
        {"available": True, "candidates": [{"drive": "G:"}]},
    ]

    monkeypatch.setattr(runner, "uf2_volume_snapshot", lambda: snapshots.pop(0))
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
            "--refresh-rp2040-smoke",
        ]
    )

    def ok_step(kind: str) -> dict:
        return {"schema": 1, "kind": kind, "ok": True, "public_rf_tx": False, "formats_sd": False}

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, args, allow_download, dry_run: ok_step("input_artifact_check"))
    monkeypatch.setattr(runner, "rp2040_access_precheck", lambda ctx, dry_run: ok_step("rp2040_autonomous_access_precheck"))
    monkeypatch.setattr(runner, "run_official_sd_smoke", lambda *args, **kwargs: (_ for _ in ()).throw(OSError(22, "Invalid argument")))
    monkeypatch.setattr(runner, "restore_bridge", lambda ctx, volume, uf2_timeout, dry_run: ok_step("rp2040_bridge_restore"))
    monkeypatch.setattr(runner, "run_preflight", lambda ctx, dry_run, verify_artifact: ok_step("rp2040_bridge_preflight"))
    monkeypatch.setattr(runner, "run_sd_file_canary", lambda ctx, dry_run: ok_step("sd_file_canary"))
    patch_sd_evidence_runners(monkeypatch, ok_step)
    monkeypatch.setattr(runner, "run_smoke", lambda ctx, dry_run: ok_step("d1l_smoke"))
    monkeypatch.setattr(
        runner,
        "run_ui_corruption_probe",
        lambda ctx, rounds, dry_run: ok_step("ui_corruption_probe"),
    )
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
            "--refresh-rp2040-smoke",
        ]
    )

    restore_attempts = []

    def failed_restore(ctx, volume, uf2_timeout, dry_run):
        restore_attempts.append(1)
        return {"schema": 1, "kind": "rp2040_bridge_restore", "ok": False, "error": "bridge_restore_not_verified"}

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, args, allow_download, dry_run: {"schema": 1, "kind": "input_artifact_check", "ok": True})
    monkeypatch.setattr(runner, "rp2040_access_precheck", lambda ctx, dry_run: {"schema": 1, "kind": "rp2040_autonomous_access_precheck", "ok": True})
    monkeypatch.setattr(runner, "restore_bridge", failed_restore)
    monkeypatch.setattr(runner, "run_preflight", lambda ctx, dry_run, verify_artifact: (_ for _ in ()).throw(AssertionError("preflight should not run")))
    monkeypatch.setattr(runner, "run_release_gate", lambda ctx, dry_run: {"schema": 1, "kind": "release_gate_audit", "ok": True, "ready_for_public_release": False, "failed_count": 1, "p0_failed_count": 1})

    report = runner.run_validation(args)

    assert report["ok"] is False
    assert report["error"] == "bridge_restore_not_verified"
    assert len(restore_attempts) == 2
    assert [step["kind"] for step in report["runs"]] == [
        "input_artifact_check",
        "rp2040_autonomous_access_precheck",
        "seeed_official_sd_smoke_capture",
        "sd_boot_prepare_rp2040_unavailable",
        "rp2040_bridge_restore",
        "rp2040_bridge_restore",
        "release_gate_audit",
    ]
    assert report["release_gate"]["ready_for_public_release"] is False
    assert report["ready_for_public_release"] is False
    assert report["release_ready"] is False


def test_completed_validation_surfaces_release_not_ready(tmp_path, monkeypatch):
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

    def ok_step(kind: str) -> dict:
        return {"schema": 1, "kind": kind, "ok": True, "public_rf_tx": False, "formats_sd": False}

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, args, allow_download, dry_run: ok_step("input_artifact_check"))
    monkeypatch.setattr(runner, "rp2040_access_precheck", lambda ctx, dry_run: ok_step("rp2040_autonomous_access_precheck"))
    monkeypatch.setattr(runner, "restore_bridge", lambda ctx, volume, uf2_timeout, dry_run: ok_step("rp2040_bridge_restore"))
    monkeypatch.setattr(runner, "run_preflight", lambda ctx, dry_run, verify_artifact: ok_step("rp2040_bridge_preflight"))
    monkeypatch.setattr(runner, "run_sd_file_canary", lambda ctx, dry_run: ok_step("sd_file_canary"))
    patch_sd_evidence_runners(monkeypatch, ok_step)
    monkeypatch.setattr(runner, "run_smoke", lambda ctx, dry_run: ok_step("d1l_smoke"))
    monkeypatch.setattr(
        runner,
        "run_release_gate",
        lambda ctx, dry_run: {
            "schema": 1,
            "kind": "release_gate_audit",
            "ok": True,
            "ready_for_public_release": False,
            "failed_count": 3,
            "p0_failed_count": 2,
        },
    )

    report = runner.run_validation(args)

    assert report["ok"] is True
    assert report["ready_for_public_release"] is False
    assert report["release_ready"] is False
    assert report["release_gate"]["p0_failed_count"] == 2


def test_completed_validation_runs_compose_capture_when_ui_probes_enabled(tmp_path, monkeypatch):
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
            "--include-ui-probes",
        ]
    )

    def ok_step(kind: str) -> dict:
        return {"schema": 1, "kind": kind, "ok": True, "public_rf_tx": False, "formats_sd": False}

    monkeypatch.setattr(runner, "verify_inputs", lambda ctx, args, allow_download, dry_run: ok_step("input_artifact_check"))
    monkeypatch.setattr(runner, "rp2040_access_precheck", lambda ctx, dry_run: ok_step("rp2040_autonomous_access_precheck"))
    monkeypatch.setattr(runner, "restore_bridge", lambda ctx, volume, uf2_timeout, dry_run: ok_step("rp2040_bridge_restore"))
    monkeypatch.setattr(runner, "run_preflight", lambda ctx, dry_run, verify_artifact: ok_step("rp2040_bridge_preflight"))
    monkeypatch.setattr(runner, "run_sd_file_canary", lambda ctx, dry_run: ok_step("sd_file_canary"))
    patch_sd_evidence_runners(monkeypatch, ok_step)
    monkeypatch.setattr(runner, "run_smoke", lambda ctx, dry_run: ok_step("d1l_smoke"))
    monkeypatch.setattr(runner, "run_onboarding_complete", lambda ctx, dry_run: ok_step("onboarding_complete"))
    monkeypatch.setattr(runner, "run_ui_corruption_probe", lambda ctx, rounds, dry_run: ok_step("ui_corruption_probe"))
    monkeypatch.setattr(runner, "run_scroll_probe", lambda ctx, dry_run: ok_step("scroll_probe"))
    monkeypatch.setattr(runner, "run_ui_pixel_capture", lambda ctx, dry_run: ok_step("ui_pixel_capture"))
    monkeypatch.setattr(
        runner,
        "run_ui_compose_keyboard_capture",
        lambda ctx, dry_run: ok_step("ui_compose_keyboard_capture"),
    )
    monkeypatch.setattr(
        runner,
        "run_release_gate",
        lambda ctx, dry_run: {
            "schema": 1,
            "kind": "release_gate_audit",
            "ok": True,
            "ready_for_public_release": False,
            "failed_count": 3,
            "p0_failed_count": 2,
        },
    )

    report = runner.run_validation(args)

    assert [step["kind"] for step in report["runs"]] == [
        "input_artifact_check",
        "rp2040_bridge_preflight",
        "sd_raw_diag",
        "sd_file_canary",
        "sd_boot_prepare_correct_structure",
        "sd_boot_prepare_missing_structure",
        "sd_boot_prepare_existing_data",
        "sd_map_tile_canary",
        "sd_export_canary",
        "sd_diagnostic_export",
        "sd_data_export",
        "sd_retained_history",
        "sd_reboot_remount",
        "d1l_smoke",
        "onboarding_complete",
        "ui_corruption_probe",
        "scroll_probe",
        "ui_pixel_capture",
        "ui_compose_keyboard_capture",
        "release_gate_audit",
    ]
    assert report["ok"] is True
