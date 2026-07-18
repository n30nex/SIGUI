import json
from pathlib import Path

from scripts import manual_ui_review_d1l as review


COMMIT = "a" * 40
RUN_ID = "123456"
RUN_ATTEMPT = "1"


def confirmations(value: bool = True) -> dict[str, bool]:
    return {name: value for name in review.REQUIRED_CONFIRMATIONS}


def ui_receipt() -> dict:
    return {
        "kind": "core_ui_corruption_probe",
        "mode": "hardware",
        "ok": True,
        "closure_eligible": True,
        "physical_observed": True,
        "port": "COM12",
        "release_profile": "core_1_0",
        "sd_history_mode": "disabled",
        "expected_firmware_commit": COMMIT,
        "device_build_commit": COMMIT,
        "github_actions_run": RUN_ID,
        "workflow_run_attempt": RUN_ATTEMPT,
        "rounds": 20,
        "checks": {"exact_candidate": True, "core_profile": True},
        "git": {"commit": COMMIT, "dirty": False, "dirty_entries": []},
        "public_rf_tx": False,
        "network_tx": False,
        "map_network_requests": False,
        "formats_sd": False,
    }


def test_manual_review_accepts_core_confirmations_without_extra_photo_gate():
    report = review.build_review(
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        port="COM12",
        operator="device operator",
        reviewer="release reviewer",
        confirmations=confirmations(),
        automated_ui_receipt={
            "path": "artifacts/hardware/com12/core_ui.json",
            "size": 123,
            "sha256": "b" * 64,
        },
        automated_ui_receipt_valid=True,
    )

    assert report["ok"] is True
    assert report["photo_receipts"] == []
    assert report["photos_optional"] is True
    assert "map_storage" not in report["confirmations"]


def test_manual_review_rejects_same_operator_reviewer_or_unbound_ui():
    report = review.build_review(
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
        port="COM12",
        operator="same",
        reviewer="same",
        confirmations=confirmations(),
        automated_ui_receipt={},
        automated_ui_receipt_valid=False,
    )

    assert report["ok"] is False


def test_ui_receipt_requires_exact_run_attempt_and_clean_candidate():
    receipt = ui_receipt()
    assert review.validate_ui_receipt(
        receipt,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )

    receipt["workflow_run_attempt"] = "2"
    assert not review.validate_ui_receipt(
        receipt,
        commit=COMMIT,
        run_id=RUN_ID,
        run_attempt=RUN_ATTEMPT,
    )


def test_optional_photo_must_be_real_png_and_have_valid_coverage(tmp_path: Path):
    png = tmp_path / "screen.png"
    raw = (
        b"\x89PNG\r\n\x1a\n"
        + b"\x00\x00\x00\rIHDR"
        + (2).to_bytes(4, "big")
        + (3).to_bytes(4, "big")
        + b"\x08\x06\x00\x00\x00"
        + b"x" * 128
    )
    png.write_bytes(raw)

    receipt = review.parse_photo_spec(
        tmp_path, f"display_touch_backlight,home={png.name}"
    )
    assert receipt["coverage"] == ["display_touch_backlight", "home"]
    assert receipt["size"] == len(raw)


def test_main_writes_fail_closed_when_ui_receipt_is_not_hardware(
    tmp_path: Path, monkeypatch
):
    root = Path(review.__file__).resolve().parents[1]
    artifact = root / "artifacts" / "test-manual-ui-invalid.json"
    out = root / "artifacts" / "test-manual-ui-out.json"
    artifact.parent.mkdir(parents=True, exist_ok=True)
    bad = ui_receipt()
    bad["mode"] = "dry-run"
    artifact.write_text(json.dumps(bad), encoding="ascii")
    monkeypatch.setattr(
        review,
        "stamp_report",
        lambda report, _root: report.update(
            {
                "git": {
                    "commit": COMMIT,
                    "dirty": False,
                    "dirty_entries": [],
                }
            }
        ),
    )
    try:
        args = [
            "--expected-firmware-commit",
            COMMIT,
            "--github-run-id",
            RUN_ID,
            "--github-run-attempt",
            RUN_ATTEMPT,
            "--operator",
            "operator",
            "--reviewer",
            "reviewer",
            "--automated-ui-receipt",
            str(artifact),
            "--out",
            str(out),
        ]
        for name in review.REQUIRED_CONFIRMATIONS:
            args.append("--confirm-" + name.replace("_", "-"))
        assert review.main(args) == 1
        assert json.loads(out.read_text(encoding="ascii"))["ok"] is False
    finally:
        artifact.unlink(missing_ok=True)
        out.unlink(missing_ok=True)
