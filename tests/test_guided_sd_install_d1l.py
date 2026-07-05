import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from scripts import guided_sd_install_d1l as guided


def write_named_artifact(directory: Path, uf2_name: str, payload: bytes) -> str:
    directory.mkdir(parents=True)
    (directory / uf2_name).write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    (directory / "SHA256SUMS.txt").write_text(f"{digest}  ./{uf2_name}\n", encoding="ascii")
    return digest


def make_run_dir(tmp_path: Path) -> Path:
    run_dir = tmp_path / "artifacts" / "github" / "123-current"
    firmware = run_dir / "d1l-firmware-artifacts"
    build = firmware / "build"
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
                "extra_esptool_args": {"after": "hard_reset", "before": "default_reset", "chip": "esp32s3"},
            }
        ),
        encoding="ascii",
    )
    firmware_manifest = []
    for path in sorted(build.rglob("*")):
        if path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            rel = path.relative_to(firmware).as_posix()
            firmware_manifest.append(f"{digest}  ./{rel}")
    (firmware / "SHA256SUMS.txt").write_text("\n".join(firmware_manifest) + "\n", encoding="ascii")
    write_named_artifact(
        run_dir / "rp2040-seeed-official-sd-smoke-firmware",
        guided.OFFICIAL_SMOKE_UF2,
        b"official-smoke",
    )
    write_named_artifact(
        run_dir / "rp2040-sd-bridge-firmware",
        guided.BRIDGE_UF2,
        b"bridge",
    )
    return run_dir


def test_guided_install_dry_run_records_two_manual_bootsel_stages(tmp_path):
    run_dir = make_run_dir(tmp_path)
    ctx = guided.GuidedContext(
        root=tmp_path,
        commit="a" * 40,
        short_commit="a" * 7,
        github_run_id="123",
        github_run_dir=run_dir,
        d1l_port="COM12",
        rp2040_port="COM16",
        baud=115200,
        esp32_flash_baud=460800,
        out=tmp_path / "guided.json",
    )
    args = SimpleNamespace(
        dry_run=True,
        uf2_volume=None,
        uf2_timeout=1.0,
        no_prompt=True,
        post_copy_delay=0.0,
        rp2040_port_timeout=0.0,
        smoke_capture_timeout=0.0,
        skip_canaries=False,
        skip_esp32_flash=False,
        download_artifacts=False,
        auto_bootloader=True,
    )

    report = guided.run_guided_install(ctx, args)

    assert report["ok"] is True
    assert report["manual_user_required"] is True
    assert report["manual_steps"] == [
        "put_rp2040_in_bootsel_for_official_sd_smoke",
        "put_rp2040_in_bootsel_for_bridge_restore",
    ]
    assert report["public_rf_tx"] is False
    assert report["formats_sd"] is False
    assert report["esp32_flash_requested"] is True
    assert report["esp32_flash_ok"] is True
    assert any(run.get("kind") == "esp32_flash" and run.get("mode") == "dry-run" for run in report["runs"])
    copy_stages = [run for run in report["runs"] if run.get("kind") == "guided_rp2040_uf2_copy"]
    assert [stage["stage"] for stage in copy_stages] == ["official_smoke", "bridge_restore"]
    assert all(stage["manual_user_required"] is True for stage in copy_stages)
    assert all(stage["planned_copy"] is True for stage in copy_stages)
    assert any(run.get("kind") == "sd_file_canary" for run in report["runs"])


def test_guided_context_refuses_forbidden_ports(tmp_path):
    make_run_dir(tmp_path)
    args = SimpleNamespace(
        root=str(tmp_path),
        d1l_port="COM8",
        rp2040_port="COM16",
        commit="a" * 40,
        github_run_id="123",
        github_run_dir=None,
        baud=115200,
        out=None,
        esp32_flash_baud=460800,
    )

    with pytest.raises(ValueError, match="COM8"):
        guided.build_context(args)


def test_guided_install_downloads_missing_artifacts_when_requested(tmp_path, monkeypatch):
    run_dir = tmp_path / "artifacts" / "github" / "123-current"
    ctx = guided.GuidedContext(
        root=tmp_path,
        commit="a" * 40,
        short_commit="a" * 7,
        github_run_id="123",
        github_run_dir=run_dir,
        d1l_port="COM12",
        rp2040_port="COM16",
        baud=115200,
        esp32_flash_baud=460800,
        out=tmp_path / "guided.json",
    )
    args = SimpleNamespace(
        dry_run=True,
        uf2_volume=None,
        uf2_timeout=1.0,
        no_prompt=True,
        post_copy_delay=0.0,
        rp2040_port_timeout=0.0,
        smoke_capture_timeout=0.0,
        skip_canaries=False,
        skip_esp32_flash=False,
        download_artifacts=True,
        auto_bootloader=True,
    )

    def fake_download(ctx_arg, dry_run):
        make_run_dir(tmp_path)
        return {"schema": 1, "kind": "github_artifact_download", "mode": "dry-run", "ok": True}

    monkeypatch.setattr(guided, "download_artifacts", fake_download)

    report = guided.run_guided_install(ctx, args)

    assert report["ok"] is True
    assert [run["kind"] for run in report["runs"][:3]] == [
        "input_artifact_check",
        "github_artifact_download",
        "input_artifact_check",
    ]
    assert report["runs"][0]["ok"] is False
    assert report["runs"][2]["ok"] is True
