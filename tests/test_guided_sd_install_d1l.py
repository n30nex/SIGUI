import hashlib
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
    copy_stages = [run for run in report["runs"] if run.get("kind") == "guided_rp2040_uf2_copy"]
    assert [stage["stage"] for stage in copy_stages] == ["official_smoke", "bridge_restore"]
    assert all(stage["manual_user_required"] is True for stage in copy_stages)
    assert all(stage["planned_copy"] is True for stage in copy_stages)
    assert any(run.get("kind") == "sd_file_canary" for run in report["runs"])


def test_guided_context_refuses_forbidden_ports(tmp_path):
    make_run_dir(tmp_path)
    args = SimpleNamespace(
        root=str(tmp_path),
        d1l_port="COM11",
        rp2040_port="COM16",
        commit="a" * 40,
        github_run_id="123",
        github_run_dir=None,
        baud=115200,
        out=None,
    )

    with pytest.raises(ValueError, match="COM11"):
        guided.build_context(args)
