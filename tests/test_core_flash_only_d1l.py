from pathlib import Path

import pytest

from scripts import core_flash_only_d1l as flash


COMMIT = "a" * 40
RUN_ID = "123456789"
RUN_ATTEMPT = "1"


def retained_state(commit: str = COMMIT, *, name: str = "DeskOS") -> list[dict]:
    return [
        {
            "schema": 1,
            "cmd": "version",
            "ok": True,
            "build_commit": commit,
            "idf": "v5.5.4",
            "release_profile": "core_1_0",
            "sd_history_mode": "disabled",
        },
        {
            "schema": 1,
            "cmd": "health",
            "ok": True,
            "build_commit": commit,
            "release_profile": "core_1_0",
            "sd_history_mode": "disabled",
            "board_ready": True,
            "ui_ready": True,
        },
        {
            "schema": 1,
            "cmd": "settings get",
            "ok": True,
            "node_name": name,
            "path_hash_bytes": 2,
        },
        {
            "schema": 1,
            "cmd": "messages public",
            "ok": True,
            "entries": [{"seq": 1, "text": "retained"}],
        },
        {
            "schema": 1,
            "cmd": "messages dm",
            "ok": True,
            "entries": [{"seq": 2, "text": "retained-dm"}],
        },
        {
            "schema": 1,
            "cmd": "contacts",
            "ok": True,
            "entries": [{"fingerprint": "0123456789ABCDEF"}],
        },
    ]


def fixture_paths(tmp_path: Path):
    run_dir = tmp_path / "artifacts" / "github" / RUN_ID
    package = run_dir / "d1l-release-package" / "release"
    package.mkdir(parents=True)
    capture_receipt = (
        run_dir
        / "core-actions-run-metadata"
        / f"core_actions_run_{RUN_ID}.json"
    )
    raw_log = (
        tmp_path
        / "artifacts"
        / "hardware"
        / "com12"
        / "esp32_flash_test.log"
    )
    return run_dir, package, capture_receipt, raw_log


def install_preflight_mocks(monkeypatch):
    monkeypatch.setattr(
        flash,
        "git_metadata",
        lambda _root: {
            "commit": COMMIT,
            "dirty": False,
            "dirty_entries": [],
        },
    )
    monkeypatch.setattr(
        flash,
        "validate_capture_receipt",
        lambda **_kwargs: {
            "ok": True,
            "receipt": {
                "path": "capture.json",
                "size": 1,
                "sha256": "b" * 64,
            },
        },
    )
    monkeypatch.setattr(
        flash,
        "verify_esp32_flash_inputs",
        lambda _context: {"ok": True, "flash_files": []},
    )
    monkeypatch.setattr(
        flash,
        "verify_core_package",
        lambda **_kwargs: {
            "ok": True,
            "workflow_run_attempt": RUN_ATTEMPT,
        },
    )
    monkeypatch.setattr(
        flash,
        "esptool_flash_command",
        lambda _build, port, _baud: [
            "python",
            "-m",
            "esptool",
            "-p",
            port,
            "write-flash",
            "0x0",
            "bootloader.bin",
        ],
    )


def success_runner(command, _cwd, _timeout):
    return (
        {
            "name": "esp32_flash",
            "ok": True,
            "returncode": 0,
            "args": command,
        },
        b"exact flash log\n",
    )


def test_bootstrap_is_nonclosing_then_retained_reflash_closes(
    tmp_path, monkeypatch
):
    install_preflight_mocks(monkeypatch)
    run_dir, package, capture_receipt, raw_log = fixture_paths(tmp_path)

    bootstrap = flash.run_core_flash_only(
        root=tmp_path,
        github_run_dir=run_dir,
        package_dir=package,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        actions_capture_receipt=capture_receipt,
        port="COM12",
        serial_baud=115200,
        flash_baud=460800,
        serial_timeout=5.0,
        flash_timeout=60,
        settle_sec=0.0,
        raw_log_path=raw_log,
        flash_phase=flash.FLASH_PHASE_BOOTSTRAP,
        flash_runner=success_runner,
        retained_state_reader=lambda *_args: retained_state(),
    )
    assert bootstrap["ok"] is True
    assert bootstrap["closure_eligible"] is False
    assert bootstrap["scope"] == "core-bootstrap-flash-only"
    assert bootstrap["retained_state_before"] is None
    assert bootstrap["retained_state_preserved"] is None

    closing_log = raw_log.with_name("esp32_flash_closing.log")
    closing = flash.run_core_flash_only(
        root=tmp_path,
        github_run_dir=run_dir,
        package_dir=package,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        actions_capture_receipt=capture_receipt,
        port="COM12",
        serial_baud=115200,
        flash_baud=460800,
        serial_timeout=5.0,
        flash_timeout=60,
        settle_sec=0.0,
        raw_log_path=closing_log,
        flash_phase=flash.FLASH_PHASE_RETAINED_REFLASH,
        flash_runner=success_runner,
        retained_state_reader=lambda *_args: retained_state(),
    )
    assert closing["ok"] is True
    assert closing["closure_eligible"] is True
    assert closing["scope"] == "core-retained-reflash-only"
    assert closing["retained_state_preserved"] is True
    assert closing["workflow_run_attempt"] == RUN_ATTEMPT
    assert closing["actions_capture_verification"]["ok"] is True


def test_flash_preflight_fails_before_physical_action(
    tmp_path, monkeypatch
):
    install_preflight_mocks(monkeypatch)
    run_dir, package, capture_receipt, raw_log = fixture_paths(tmp_path)
    calls = []

    def reject_capture(**_kwargs):
        raise ValueError("archive binding mismatch")

    monkeypatch.setattr(flash, "validate_capture_receipt", reject_capture)
    with pytest.raises(ValueError, match="archive binding"):
        flash.run_core_flash_only(
            root=tmp_path,
            github_run_dir=run_dir,
            package_dir=package,
            commit=COMMIT,
            run_id=RUN_ID,
            run_attempt=RUN_ATTEMPT,
            actions_capture_receipt=capture_receipt,
            port="COM12",
            serial_baud=115200,
            flash_baud=460800,
            serial_timeout=5.0,
            flash_timeout=60,
            settle_sec=0.0,
            raw_log_path=raw_log,
            flash_phase=flash.FLASH_PHASE_BOOTSTRAP,
            flash_runner=lambda *_args: calls.append("flash"),
            retained_state_reader=lambda *_args: calls.append("serial"),
        )
    assert calls == []
    assert not raw_log.exists()


@pytest.mark.parametrize("port", ["COM8", "COM11", "COM16", "COM29", "COM30"])
def test_flash_rejects_every_non_com12_port(tmp_path, monkeypatch, port):
    install_preflight_mocks(monkeypatch)
    run_dir, package, capture_receipt, raw_log = fixture_paths(tmp_path)
    with pytest.raises(ValueError, match="COM12"):
        flash.run_core_flash_only(
            root=tmp_path,
            github_run_dir=run_dir,
            package_dir=package,
            commit=COMMIT,
            run_id=RUN_ID,
            run_attempt=RUN_ATTEMPT,
            actions_capture_receipt=capture_receipt,
            port=port,
            serial_baud=115200,
            flash_baud=460800,
            serial_timeout=5.0,
            flash_timeout=60,
            settle_sec=0.0,
            raw_log_path=raw_log,
            flash_phase=flash.FLASH_PHASE_BOOTSTRAP,
        )


def test_closing_reflash_requires_exact_ready_candidate_baseline(
    tmp_path, monkeypatch
):
    install_preflight_mocks(monkeypatch)
    run_dir, package, capture_receipt, raw_log = fixture_paths(tmp_path)
    calls = []
    with pytest.raises(ValueError, match="exact ready candidate"):
        flash.run_core_flash_only(
            root=tmp_path,
            github_run_dir=run_dir,
            package_dir=package,
            commit=COMMIT,
            run_id=RUN_ID,
            run_attempt=RUN_ATTEMPT,
            actions_capture_receipt=capture_receipt,
            port="COM12",
            serial_baud=115200,
            flash_baud=460800,
            serial_timeout=5.0,
            flash_timeout=60,
            settle_sec=0.0,
            raw_log_path=raw_log,
            flash_phase=flash.FLASH_PHASE_RETAINED_REFLASH,
            flash_runner=lambda *_args: calls.append("flash"),
            retained_state_reader=lambda *_args: retained_state("b" * 40),
        )
    assert calls == []


@pytest.mark.parametrize(
    ("run_id", "attempt"), [("0", "1"), ("1", "0"), ("x", "1")]
)
def test_flash_rejects_nonpositive_run_identity(
    tmp_path, monkeypatch, run_id, attempt
):
    install_preflight_mocks(monkeypatch)
    run_dir, package, capture_receipt, raw_log = fixture_paths(tmp_path)
    with pytest.raises(ValueError, match="positive integers"):
        flash.run_core_flash_only(
            root=tmp_path,
            github_run_dir=run_dir,
            package_dir=package,
            commit=COMMIT,
            run_id=run_id,
            run_attempt=attempt,
            actions_capture_receipt=capture_receipt,
            port="COM12",
            serial_baud=115200,
            flash_baud=460800,
            serial_timeout=5.0,
            flash_timeout=60,
            settle_sec=0.0,
            raw_log_path=raw_log,
            flash_phase=flash.FLASH_PHASE_BOOTSTRAP,
        )
